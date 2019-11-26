#include <assert.h>
#include <fcntl.h>
#include <libgen.h>
#include <pthread.h>

#include "common.h"
#include "messages.h"
#include "rdma_consumer.h"

#define PRODUCER_RECORD_BACKLOG 1000
#define VAL_LENGTH sizeof(int)

struct client_context {
    // For receiving consumer records
    char *buffer;
    struct ibv_mr *buffer_mr;
    // For receiving acks
    struct message *msg;
    struct ibv_mr *msg_mr;
    // Hold remote addr and keys
    uint64_t peer_addr;
    uint32_t peer_rkey;
    // Length of buffer to read from server
    int size;
    // State of the client
    enum {
	READ_POLLING,
	READ_READY
    } read_status;
};

// Record to be returned to the client
struct ProducerMessage *producer_record = NULL;
int shouldDisconnect = 0;

// Mutexes and conditional variables to synchronize polling and 
// consumeRecord client calls
pthread_cond_t consumer_cond_variable = PTHREAD_COND_INITIALIZER;
pthread_mutex_t polling_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t polling_cond_variable = PTHREAD_COND_INITIALIZER;
pthread_mutex_t consumer_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Create a ProducerMessage node with the given key and value
 * Note: Creates deep copies of both key and value
 */
struct ProducerMessage* createNode(char *key, char *value) {
    char *k = malloc(sizeof(key) + 1);
    char *v = malloc(sizeof(value) + 1);
    strcpy(k, key);
    strcpy(v, value);
    struct ProducerMessage *node = malloc(sizeof(struct ProducerMessage));
    node->key = k;
    node->value = v;
    node->next = NULL;
    return node;
}

static void post_receive(struct rdma_cm_id *id) {
    struct client_context *ctx = (struct client_context *) id->context;
    struct ibv_recv_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;
    memset(&wr, 0, sizeof(wr));
    // User defined work request ID
    wr.wr_id = (uintptr_t)id;
    // Pointed to the s/g array
    wr.sg_list = &sge;
    // Size of s/g array
    wr.num_sge = 1;
    // Start address of local memory buffer
    sge.addr = (uintptr_t)ctx->msg;
    // Length of the buffer
    sge.length = sizeof(*ctx->msg);
    // Key of the local memory region
    sge.lkey = ctx->msg_mr->lkey;
    // Post the linked list of work requests to the receive queue of the queue pair
    // WRs from this list will be stopped processing on first failure, the failed WR
    // will be returned in bad_wr
    TEST_NZ(ibv_post_recv(id->qp, &wr, &bad_wr));
}

static void on_pre_conn(struct rdma_cm_id *id) {
    // Get the context from the connection identifier
    struct client_context *ctx = (struct client_context *) id->context;
    // Allocate local memory
    posix_memalign((void **)&ctx->buffer, sysconf(_SC_PAGESIZE), BUFFER_SIZE);
    // Register the local memory region, enable local write access
    TEST_Z(ctx->buffer_mr = ibv_reg_mr(rc_get_pd(), ctx->buffer, BUFFER_SIZE, IBV_ACCESS_LOCAL_WRITE));
    // Allocate and register memory for exchanging keys
    posix_memalign((void **)&ctx->msg, sysconf(_SC_PAGESIZE), sizeof(*ctx->msg));
    TEST_Z(ctx->msg_mr = ibv_reg_mr(rc_get_pd(), ctx->msg, sizeof(*ctx->msg), IBV_ACCESS_LOCAL_WRITE));
    // Post work request on the receive queue
    post_receive(id);
}

struct ProducerMessage* consumeRecord() {
    // Signal RDMA polling to begin
    pthread_cond_signal(&consumer_cond_variable);
    // Wait on polling to complete
    pthread_mutex_lock(&consumer_mutex);
    pthread_cond_wait(&polling_cond_variable, &consumer_mutex);
    pthread_mutex_unlock(&consumer_mutex);
    // Return record obtained after polling
    return producer_record;
}

static void create_and_post_work_request(struct rdma_cm_id *id) {
    struct client_context *ctx = (struct client_context *)id->context;
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = (uintptr_t)id;
    wr.opcode = IBV_WR_RDMA_READ;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = (uintptr_t)ctx->peer_addr;
    wr.wr.rdma.rkey = ctx->peer_rkey;
    
    sge.addr = (uintptr_t)ctx->buffer;
    sge.length = ctx->size;
    sge.lkey = ctx->buffer_mr->lkey;
    // Post a list of work requests to the send queue 
    TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));
}

static void issue_one_sided_read(struct rdma_cm_id *id) {
    struct client_context *ctx = (struct client_context *)id->context;
    printf("Started issuing...\n");
    // Check if in polling state
    if (ctx->read_status == READ_POLLING) {
        // Check if buffer has valid length
        printf("ATOI:%d\n", atoi(ctx->buffer));
        if (atoi(ctx->buffer) > 0) {
            // Transition to READ_READY, set the size
	    ctx->read_status = READ_READY;
	    ctx->size = atoi(ctx->buffer);
            printf("The size to be read has been set as %d\n", ctx->size);
	    // Update remote addr to read from
            ctx->peer_addr += VAL_LENGTH; // TODO: Wrap around
            printf("Forwarding peer address by %zu\n", VAL_LENGTH);
	    // Issue one sided operation to read the data
            create_and_post_work_request(id);
        } else {
            // Issue one sided operation, polling logic
            create_and_post_work_request(id);
	}
    } else {
	printf("Server wrote into the buffer: %s\n", ctx->buffer);
	// Deserialize the buffer into producer record
        char* separator;
        separator = strstr(ctx->buffer, "/");
	printf("Separator is %s\n", separator);
        char* key = (char*)malloc(sizeof(char) *  (separator - ctx->buffer + 1));
        printf("Key allocated of length: %zu\n", (separator - ctx->buffer + 1));
        strncpy(key, ctx->buffer, (separator - ctx->buffer));
        printf("Key: %s\n", key);
        producer_record = createNode(key, separator + 1);        
	pthread_cond_signal(&polling_cond_variable);
        pthread_mutex_lock(&polling_mutex);
	pthread_cond_wait(&consumer_cond_variable, &polling_mutex);
	pthread_mutex_unlock(&polling_mutex);
	// Transition state to READ_POLLING, change peer_addr and size
	ctx->read_status = READ_POLLING;
	ctx->peer_addr += ctx->size;
	ctx->size = VAL_LENGTH;
	// Issue one sided operation to read the length
        create_and_post_work_request(id);
    } 
    
}

static void on_completion(struct ibv_wc *wc) {
    // Get the connection identifier and context from the work completion
    struct rdma_cm_id *id = (struct rdma_cm_id *)(uintptr_t)(wc->wr_id);
    struct client_context *ctx = (struct client_context *)id->context;
    // Status of the work completion is from receive queue
    // Set the peer address and key thus obtained 
    if (wc->opcode & IBV_WC_RECV) {
        if (ctx->msg->id == MSG_READY) {
            ctx->peer_addr = ctx->msg->data.mr.addr;
            ctx->peer_rkey = ctx->msg->data.mr.rkey;
	    // Start one sided polling
            issue_one_sided_read(id);
        } // put error here
    } else {
        // Do one sided polling
        issue_one_sided_read(id);
    }
}

void *run_client_loop(void *s) {
    char *server = (char *)s;
    struct client_context ctx;

    ctx.size = VAL_LENGTH;
    ctx.read_status = READ_POLLING;
    rc_init(
        on_pre_conn,
        NULL, //on connect
        on_completion,
        NULL); // on disconnect

    rc_client_loop(server, DEFAULT_PORT, &ctx, CONSUMER_ROLE);
    return 0;
}

void init(char *server) {
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, run_client_loop, (void *)server);
}

void terminate() {
    shouldDisconnect = 1;
    printf("Finished termination\n");
}
