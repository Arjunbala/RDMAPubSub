#include <assert.h>
#include <fcntl.h>
#include <libgen.h>
#include <pthread.h>

#include "common.h"
#include "messages.h"
#include "rdma_producer.h"

struct ProducerMessage
{
    char *key;
    char *value;
    struct ProducerMessage *next;
};

struct client_context
{
    // For sending producer records
    char *buffer;
    struct ibv_mr *buffer_mr;

    // For receiving acks
    struct message *msg;
    struct ibv_mr *msg_mr;

    // Hold remote addr and keys
    uint64_t peer_addr;
    uint32_t peer_rkey;

    int index;
};

#define PRODUCER_RECORD_BACKLOG 1000

struct ProducerMessage *producer_records[PRODUCER_RECORD_BACKLOG] = {NULL};
int head = -1;
int shouldDisconnect = 0;
pthread_mutex_t producer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t producer_cond_variable = PTHREAD_COND_INITIALIZER;
pthread_mutex_t terminate_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t terminate_cond_variable = PTHREAD_COND_INITIALIZER;

/**
 * Create a ProducerMessage node with the given key and value
 * Note: Creates deep copies of both key and value
 */
struct ProducerMessage* createNode(char *key, char *value)
{
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

/**
 * Add the given node to the list of producer records
 */
void insertAtEnd(struct ProducerMessage *node)
{
    pthread_mutex_lock(&producer_mutex);
    printf("Adding producer record\n");
    head = (head + 1) % PRODUCER_RECORD_BACKLOG;
    // assert that we are not over-flowing
    assert(producer_records[head] == NULL);
    producer_records[head] = node;
    pthread_mutex_unlock(&producer_mutex);
}

static void rdma_send(struct rdma_cm_id *id, uint32_t len)
{
    struct client_context *ctx = (struct client_context *) id->context;
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;
    
    memset(&wr, 0, sizeof(wr));

    wr.wr_id = (uintptr_t)id;
    wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.imm_data = htonl(len);
    wr.wr.rdma.remote_addr = ctx->peer_addr;
    wr.wr.rdma.rkey = ctx->peer_rkey;

    if (len > 0) {
        wr.sg_list = &sge;
        wr.num_sge = 1;
        sge.addr = (uintptr_t)ctx->buffer;
        sge.length = len;
        sge.lkey = ctx->buffer_mr->lkey;
    }

    TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));
}

static void post_receive(struct rdma_cm_id *id)
{
    struct client_context *ctx = (struct client_context *) id->context;
    struct ibv_recv_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = (uintptr_t)id;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    sge.addr = (uintptr_t)ctx->msg;
    sge.length = sizeof(*ctx->msg);
    sge.lkey = ctx->msg_mr->lkey;

    TEST_NZ(ibv_post_recv(id->qp, &wr, &bad_wr));
}

static void send_producer_record(struct rdma_cm_id *id)
{
    struct client_context *ctx = (struct client_context *)id->context;
    // Now look at head of ProducerMessage linked list
    pthread_mutex_lock(&producer_mutex);
    struct ProducerMessage *h = producer_records[ctx->index];
    // We need to send message associated with the head.
    if(h == NULL) {
        printf("Record is null; checking if should disconnect\n");
        if(shouldDisconnect == 1) {
            printf("Disconnecting..\n");
            rdma_send(id, 0);
            return;
        }
        // Wait till we have a node added to the linked list
        printf("Waiting for record to be produced\n");
        pthread_cond_wait(&producer_cond_variable, &producer_mutex);
        h = producer_records[ctx->index];
        if(h == NULL && shouldDisconnect == 1) {
            printf("Disconnecting..\n");
            rdma_send(id, 0);
            return;
        }
        printf("Got a producer record now\n");
    }
    pthread_mutex_unlock(&producer_mutex);
    // Serialize the msg as key/value
    // TODO: Use a better serialization logic
    char *str = (char*) malloc(strlen(h->key) + strlen(h->value) + 2);
    strcpy(str, h->key);
    strcat(str, "/");
    strcat(str, h->value);
    str[strlen(str)] = '\0';
    strcpy(ctx->buffer, str);
    printf("sending %s via RDMA\n", ctx->buffer);
    rdma_send(id, strlen(str)+1);
    
    // Get ready to send next message
    ctx->index = ctx->index + 1;
}

static void on_pre_conn(struct rdma_cm_id *id)
{
    struct client_context *ctx = (struct client_context *) id->context;

    posix_memalign((void **)&ctx->buffer, sysconf(_SC_PAGESIZE), BUFFER_SIZE);
    TEST_Z(ctx->buffer_mr = ibv_reg_mr(rc_get_pd(), ctx->buffer, BUFFER_SIZE, 0));

    posix_memalign((void **)&ctx->msg, sysconf(_SC_PAGESIZE), sizeof(*ctx->msg));
    TEST_Z(ctx->msg_mr = ibv_reg_mr(rc_get_pd(), ctx->msg, sizeof(*ctx->msg), IBV_ACCESS_LOCAL_WRITE));

    post_receive(id);
}

static void on_completion(struct ibv_wc *wc)
{
    struct rdma_cm_id *id = (struct rdma_cm_id *)(uintptr_t)(wc->wr_id);
    struct client_context *ctx = (struct client_context *)id->context;
    
    if (wc->opcode & IBV_WC_RECV) {
        if (ctx->msg->id == MSG_READY) {
            ctx->peer_addr = ctx->msg->data.mr.addr;
            ctx->peer_rkey = ctx->msg->data.mr.rkey;
            printf("received ready, sending next producer record\n");
            send_producer_record(id);
            post_receive(id);
        } else if (ctx->msg->id == MSG_DONE) {
            printf("received DONE, disconnecting\n");
            rc_disconnect(id);
            pthread_cond_signal(&terminate_cond_variable);
        }
    }
}

void *run_client_loop(void *s)
{
    char *server = (char *)s;
    struct client_context ctx;

    ctx.index = 0;
    rc_init(
        on_pre_conn,
        NULL, //on connect
        on_completion,
        NULL); // on disconnect

    rc_client_loop(server, DEFAULT_PORT, &ctx);
    return 0;
}

void init(char *server)
{
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, run_client_loop, (void *)server);
}

// TODO: Should give the caller a callback function option
void produceRecord(char *key, char *value)
{
    insertAtEnd(createNode(key, value));
    pthread_cond_signal(&producer_cond_variable);
}

void terminate()
{
    shouldDisconnect = 1;
    // Signal RDMA thread to remove it from waiting
    pthread_cond_signal(&producer_cond_variable);
    // Wait for all producer records to be sent
    pthread_mutex_lock(&terminate_mutex);
    pthread_cond_wait(&terminate_cond_variable, &terminate_mutex);
    printf("Finished termination\n");
    pthread_mutex_unlock(&terminate_mutex); 
}
