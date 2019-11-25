#include "common.h"

const int TIMEOUT_IN_MS = 500;

struct context {
    // Device context
    struct ibv_context *ctx;
    // Protection domain
    struct ibv_pd *pd;
    // Completion queue
    struct ibv_cq *cq;
    // Completion channel
    struct ibv_comp_channel *comp_channel;
    // Completion queue polling thread
    pthread_t cq_poller_thread;
};

// The connection context
static struct context *s_ctx = NULL;
// Callback function to call on pre-connection
static pre_conn_cb_fn s_on_pre_conn_cb = NULL;
// Callback function to call on connection
static connect_cb_fn s_on_connect_cb = NULL;
// Callback function to call on completion
static completion_cb_fn s_on_completion_cb = NULL;
// Callback function to call on disconnection
static disconnect_cb_fn s_on_disconnect_cb = NULL;

static void build_context(struct ibv_context *verbs);
static void build_qp_attr(struct ibv_qp_init_attr *qp_attr);
static void event_loop(struct rdma_event_channel *ec, int exit_on_disconnect);
static void *poll_cq(void *);

// Builds the connection 
void build_connection(struct rdma_cm_id *id) {
    struct ibv_qp_init_attr qp_attr;
    // Build the context and queue pair attributes
    build_context(id->verbs);
    build_qp_attr(&qp_attr);
    // Create the queue pair associated with mentioned protection domain
    TEST_NZ(rdma_create_qp(id, s_ctx->pd, &qp_attr));
}

void build_context(struct ibv_context *verbs) {
    // Fail if a different context already exists
    if (s_ctx) {
      if (s_ctx->ctx != verbs)
        rc_die("cannot handle events in more than one context.");
      return;
    }
    // Allocate context object
    s_ctx = (struct context *)malloc(sizeof(struct context));
    // Set the device context as given
    s_ctx->ctx = verbs;
    // Create protection domain
    TEST_Z(s_ctx->pd = ibv_alloc_pd(s_ctx->ctx));
    // Create completion event channel, used to deliver work completion
    // notifications to a userspace process
    TEST_Z(s_ctx->comp_channel = ibv_create_comp_channel(s_ctx->ctx));
    // Create completion queue for the given device context
    // The above created completion event channel will be used to indicate that a new
    // work completion has been added to this completion queue
    TEST_Z(s_ctx->cq = ibv_create_cq(s_ctx->ctx, 10, NULL, s_ctx->comp_channel, 0)); /* cqe=10 is arbitrary */
    // Request completion notification on the given completion queue
    // This should be read using ibv_get_cq_event()
    TEST_NZ(ibv_req_notify_cq(s_ctx->cq, 0));
    // Create a thread for polling the completion queue and handling events
    TEST_NZ(pthread_create(&s_ctx->cq_poller_thread, NULL, poll_cq, NULL));
}

void build_qp_attr(struct ibv_qp_init_attr *qp_attr) {
    memset(qp_attr, 0, sizeof(*qp_attr));
    // Completion queue to be associated with the send and receive queues
    qp_attr->send_cq = s_ctx->cq;
    qp_attr->recv_cq = s_ctx->cq;
    // Requested transport service type of this queue pair
    qp_attr->qp_type = IBV_QPT_RC; // Reliable Connection
    // Maximum number of outstanding work requests that can be posted to the
    // Send and Receive queues in the queue pair
    qp_attr->cap.max_send_wr = 10;
    qp_attr->cap.max_recv_wr = 10;
    // Maximum number of scatter/gather elements in any work request that can
    // be posted to the Send and Receive queues in the queue pair
    qp_attr->cap.max_send_sge = 1;
    qp_attr->cap.max_recv_sge = 1;
}

void build_params(struct rdma_conn_param *params) {
  memset(params, 0, sizeof(*params));
  params->initiator_depth = params->responder_resources = 1;
  params->rnr_retry_count = 7; /* infinite retry */
}

void event_loop(struct rdma_event_channel *ec, int exit_on_disconnect) {
    // RDMA Connection manager event
    struct rdma_cm_event *event = NULL;
    // RDMA connection manager connection parameters
    struct rdma_conn_param cm_params;
    // Build connection parameters
    build_params(&cm_params);
    // Get RDMA events, retrives the next pending communication event on the
    // given event channel
    while (rdma_get_cm_event(ec, &event) == 0) {
        struct rdma_cm_event event_copy;
        memcpy(&event_copy, event, sizeof(*event));
        // Free the communication event after the copy has been created
        // All events which are allocated by rdma_get_cm_event must be freed
        rdma_ack_cm_event(event);
        // Address resolution completed successfully
        // Build connection and call on pre connection callback
        if (event_copy.event == RDMA_CM_EVENT_ADDR_RESOLVED) {
            build_connection(event_copy.id);
            if (s_on_pre_conn_cb)
                s_on_pre_conn_cb(event_copy.id);
            // Resolve the route information to needed to establish a connection
            TEST_NZ(rdma_resolve_route(event_copy.id, TIMEOUT_IN_MS));
        } else if (event_copy.event == RDMA_CM_EVENT_ROUTE_RESOLVED) {
            // Route resolution completed successfully
            // Initiate an active connection request
            TEST_NZ(rdma_connect(event_copy.id, &cm_params));
        } else if (event_copy.event == RDMA_CM_EVENT_CONNECT_REQUEST) {
            // Generated on passive side to notify the user of a new connection
            // request. This is where the server side connection will be built and
            // on pre connection callback called.
            build_connection(event_copy.id);
            if (s_on_pre_conn_cb)
                s_on_pre_conn_cb(event_copy.id);
            // Accept the connection request
            TEST_NZ(rdma_accept(event_copy.id, &cm_params));
        } else if (event_copy.event == RDMA_CM_EVENT_ESTABLISHED) {
            // Indicates that a connection has been established with the remote endpoint
            // Call on connection callback
            if (s_on_connect_cb)
                s_on_connect_cb(event_copy.id);
        } else if (event_copy.event == RDMA_CM_EVENT_DISCONNECTED) {
            // The connection has been disconnected
            // Deallocate the queue pair allocated on the given rdma_cm_id
            rdma_destroy_qp(event_copy.id);
            // Call on disconnection callback
            if (s_on_disconnect_cb)
                s_on_disconnect_cb(event_copy.id);
            // Destroy the specified rdma_cm_id and cancels any outstanding asynchronous
            // operation 
            rdma_destroy_id(event_copy.id);
            // Exit on disconnect, if specified
            if (exit_on_disconnect)
                break;
        } else {
            // Event is unknown
            rc_die("Unknown event\n");
        }
    }
}

void *poll_cq(void *ctx) {
    struct ibv_cq *cq;
    struct ibv_wc wc;
    while (1) {
        // Wait for the next completion event on the given completion event channel
        // Blocking call
        TEST_NZ(ibv_get_cq_event(s_ctx->comp_channel, &cq, &ctx));
        // Acknowledge completion queue event
        ibv_ack_cq_events(cq, 1);
        // Request to receive notification on next completion event on the given
        // completion queue
        TEST_NZ(ibv_req_notify_cq(cq, 0));
        // poll the given completion queue for work completion and return the first
        // work completion
        while (ibv_poll_cq(cq, 1, &wc)) {
            // Operations completed successfully, the memory buffers referred to by this
            // work request can be used/reused
            if (wc.status == IBV_WC_SUCCESS)
                // Call on completion callback with this work request
                s_on_completion_cb(&wc);
            else {
                printf("Work request completion status: %d\n", wc.status);
                rc_die("poll_cq: status is not IBV_WC_SUCCESS");
            }
        }
    }
    return NULL;
}

void rc_init(pre_conn_cb_fn pc, connect_cb_fn conn, completion_cb_fn comp, disconnect_cb_fn disc) {
    // Initialized callback functions
    s_on_pre_conn_cb = pc;
    s_on_connect_cb = conn;
    s_on_completion_cb = comp;
    s_on_disconnect_cb = disc;
}

void rc_client_loop(const char *host, const char *port, void *context) {
    struct addrinfo *addr;
    struct rdma_cm_id *conn = NULL;
    struct rdma_event_channel *ec = NULL;
    struct rdma_conn_param cm_params;
    // Get addrinfo structure, given the host and port
    TEST_NZ(getaddrinfo(host, port, NULL, &addr));
    // Open a channel to report communication events
    TEST_Z(ec = rdma_create_event_channel());
    // Allocate a communication identifier, given the communication channel
    // on which to report communications associated with the identifier
    // RDMA_PS_TCP implies using the connection oriented QP communication
    TEST_NZ(rdma_create_id(ec, &conn, NULL, RDMA_PS_TCP));
    // Resolve given destination IP address to RDMA address. If successful, the connection
    // identifier is bound to a local device
    TEST_NZ(rdma_resolve_addr(conn, NULL, addr->ai_addr, TIMEOUT_IN_MS));
    // Free the addrinfo
    freeaddrinfo(addr);
    // Set the context in the communication identifier to the context passed
    conn->context = context;
    // Build connection manager parameters
    build_params(&cm_params);
    // Start the event loop on the given event channel
    event_loop(ec, 1); // exit on disconnect
    // Close the given event communication channel
    rdma_destroy_event_channel(ec);
}

void rc_server_loop(const char *port) {
    struct sockaddr_in6 addr;
    struct rdma_cm_id *listener = NULL;
    struct rdma_event_channel *ec = NULL;
  
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(atoi(port));
  
    // Open a channel to report communication events
    TEST_Z(ec = rdma_create_event_channel());
    // Allocate a communication identifier, given the communication channel
    // on which to report communications associated with the identifier
    // RDMA_PS_TCP implies using the connection oriented QP communication
    TEST_NZ(rdma_create_id(ec, &listener, NULL, RDMA_PS_TCP));
    // Bind the given RDMA identifier to the given source address
    // If binding to a specific local address, the connection identifier
    // will also be bound to a local RDMA device
    TEST_NZ(rdma_bind_addr(listener, (struct sockaddr *)&addr));
    // Listen for incoming connection requests
    TEST_NZ(rdma_listen(listener, 10)); /* backlog=10 is arbitrary */
    // Start the event loop on the given event channel
    event_loop(ec, 0); // don't exit on disconnect
    // Release the given communication identifier
    rdma_destroy_id(listener);
    // Close the given event communication channel
    rdma_destroy_event_channel(ec);
}

void rc_disconnect(struct rdma_cm_id *id) {
    // Disconnect the connection, transition any associated QP to error state
    // which will flush any posted work requests to the completion queue
    // This should be called by both client and server side of the connection
    rdma_disconnect(id);
}

void rc_die(const char *reason) {
    fprintf(stderr, "%s\n", reason);
    exit(EXIT_FAILURE);
}

struct ibv_pd* rc_get_pd() {
    return s_ctx->pd;
}
