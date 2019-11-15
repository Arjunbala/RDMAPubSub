#include <fcntl.h>
#include <libgen.h>

#include "common.h"
#include "messages.h"

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

    uint64_t peer_addr;
    uint32_t peer_rkey;

    struct ProducerMessage *head;
};

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
 * Add the given node to end of linked list pointed to by head
 */
void insertAtEnd(struct ProducerMessage *head, struct ProducerMessage *node)
{
    struct ProducerMessage *h = head;
    if(h == NULL) {
        h = node;
    } else {
        while(h->next != NULL) {
            h = h->next;
        }
        h->next = node;
    }
}

static void rdma_send(struct rdma_cm_id *id, uint32_t len)
{
    struct client_context *ctx = (struct client_context *) id->context;
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;
    
    memset(&wr, 0, sizeof(wr));

    wr.wr_id = (uintptr_t)id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.imm_data = htonl(len);
    wr.wr.rdma.remote_addr = ctx->peer_addr;
    wr.wr.rdma.rkey = ctx->peer_rkey;

    sge.addr = (uintptr_t)ctx->buffer;
    sge.length = len;
    sge.lkey = ctx->buffer_mr->lkey;

    TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));
}

static void send_producer_record(struct rdma_cm_id *id)
{
    struct client_context *ctx = (struct client_context *)id->context;
    // Now look at head of ProducerMessage linked list
    struct ProducerMessage *h = ctx->head;
    // We need to send message associated with the head and advance the pointer
    if(h != NULL) {
        // Serialize the msg as key/value
        // TODO: Use a better serialization logic
        char *str = (char*) malloc(strlen(h->key) + strlen(h->value) + 2);
        strcpy(str, h->key);
        strcat(str, "/");
        strcat(str, h->value);
        str[strlen(str)] = '\0';
        ctx->buffer = str;
        printf("%s\n", ctx->buffer);
        rdma_send(id, strlen(str)+1);
    } else {
        // busy loop for now - ideally we would need to wait till a new entry is added
        while(1);
    }
    // Get ready to send next message
    ctx->head = ctx->head->next;
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
        }
    }
}

int main(int argc, char **argv)
{
    struct client_context ctx;

    if(argc != 2) {
        fprintf(stderr, "usage: %s <server-address>", argv[0]);
        return 1;
    }

    // Create some sample messages
    // TODO: Need an async mechanism to fill this queue
    struct ProducerMessage *head = NULL;
    insertAtEnd(head, createNode("1","Arjun"));
    insertAtEnd(head, createNode("2", "Danish"));

    ctx.head = head;

    rc_init(
        on_pre_conn,
        NULL, //on connect
        on_completion,
        NULL); // on disconnect

    rc_client_loop(argv[1], DEFAULT_PORT, &ctx);

    // TODO: Cleanup linked list
    return 0;
}
