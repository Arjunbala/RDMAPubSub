#include <fcntl.h>
#include <sys/stat.h>

#include "common.h"
#include "messages.h"

struct conn_context
{
  char *buffer;
  struct ibv_mr *buffer_mr;

  struct message *msg;
  struct ibv_mr *msg_mr;

  char *role;
};

// Number of client connections to the server
static int num_clients = 0;
// The buffer shared remotely by the server
static char* consumer_buffer = NULL;
// Memory region of the buffer shared remotely by the server
static struct ibv_mr *consumer_buffer_mr;

static void send_message(struct rdma_cm_id *id)
{
  struct conn_context *ctx = (struct conn_context *)id->context;

  struct ibv_send_wr wr, *bad_wr = NULL;
  struct ibv_sge sge;

  memset(&wr, 0, sizeof(wr));

  wr.wr_id = (uintptr_t)id;
  wr.opcode = IBV_WR_SEND;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.send_flags = IBV_SEND_SIGNALED;

  sge.addr = (uintptr_t)ctx->msg;
  sge.length = sizeof(*ctx->msg);
  sge.lkey = ctx->msg_mr->lkey;

  TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));
}

static void post_receive(struct rdma_cm_id *id)
{
  struct ibv_recv_wr wr, *bad_wr = NULL;

  memset(&wr, 0, sizeof(wr));

  wr.wr_id = (uintptr_t)id;
  wr.sg_list = NULL;
  wr.num_sge = 0;

  TEST_NZ(ibv_post_recv(id->qp, &wr, &bad_wr));
}

static void on_pre_conn(struct rdma_cm_id *id)
{
  struct conn_context *ctx = (struct conn_context *)malloc(sizeof(struct conn_context));

  id->context = ctx;

  ctx->role = getRole();
  printf("ROLE:%s\n", ctx->role);
  if (strcmp(ctx->role, PRODUCER_ROLE) == 0) {
    posix_memalign((void **)&ctx->buffer, sysconf(_SC_PAGESIZE), BUFFER_SIZE);
    TEST_Z(ctx->buffer_mr = ibv_reg_mr(rc_get_pd(), ctx->buffer, BUFFER_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

    posix_memalign((void **)&ctx->msg, sysconf(_SC_PAGESIZE), sizeof(*ctx->msg));
    TEST_Z(ctx->msg_mr = ibv_reg_mr(rc_get_pd(), ctx->msg, sizeof(*ctx->msg), 0));
  } else {
    ++num_clients;
    posix_memalign((void **)&ctx->msg, sysconf(_SC_PAGESIZE), sizeof(*ctx->msg));
    TEST_Z(ctx->msg_mr = ibv_reg_mr(rc_get_pd(), ctx->msg, sizeof(*ctx->msg), 0));
  }
  if (consumer_buffer == NULL) {
      posix_memalign((void **)&ctx->buffer, sysconf(_SC_PAGESIZE), BUFFER_SIZE);
      TEST_Z(ctx->buffer_mr = ibv_reg_mr(rc_get_pd(), ctx->buffer, BUFFER_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ));
      consumer_buffer = ctx->buffer;
      consumer_buffer_mr = ctx->buffer_mr;
      printf("Registering to new memory...\n");
  } else {
      printf("Registering to the existing memory...\n");
      ctx->buffer = consumer_buffer;
      ctx->buffer_mr = consumer_buffer_mr;
  }
  printf("Assigned buffer....\n");
  printf("Number of clients: %d\n", num_clients);

  post_receive(id);
}

static void on_connection(struct rdma_cm_id *id)
{
  struct conn_context *ctx = (struct conn_context *)id->context;

  ctx->msg->id = MSG_READY;
  ctx->msg->data.mr.addr = (uintptr_t)ctx->buffer_mr->addr;
  ctx->msg->data.mr.rkey = ctx->buffer_mr->rkey;

  send_message(id);
}

static void on_disconnect(struct rdma_cm_id *id)
{
  struct conn_context *ctx = (struct conn_context *)id->context;
  if (strcmp(ctx->role, PRODUCER_ROLE) == 0) {
    ibv_dereg_mr(ctx->buffer_mr);
    ibv_dereg_mr(ctx->msg_mr);
    free(ctx->buffer);
    free(ctx->msg);
    free(ctx);
  } else {
    --num_clients;
    printf("Number of clients remaining: %d\n", num_clients);
    if (num_clients == 0) {
        ibv_dereg_mr(ctx->buffer_mr);
        free(ctx->buffer);
    }
    ibv_dereg_mr(ctx->msg_mr);
    free(ctx->msg);
    free(ctx);
  }
}

static void on_completion(struct ibv_wc *wc)
{
  struct rdma_cm_id *id = (struct rdma_cm_id *)(uintptr_t)wc->wr_id;
  struct conn_context *ctx = (struct conn_context *)id->context;
  
  if (strcmp(ctx->role, PRODUCER_ROLE) == 0) {
    if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
      uint32_t size = ntohl(wc->imm_data);
      if(size == 0) {
          ctx->msg->id = MSG_DONE;
          send_message(id);
          return;
      }
      printf("Received data %s\n", ctx->buffer);
      int data_size = strlen(ctx->buffer);
      sprintf(consumer_buffer + strlen(consumer_buffer), "%04d", data_size);
      sprintf(consumer_buffer + strlen(consumer_buffer), "%s", ctx->buffer);
      printf("Consumer buffer: %s\n", consumer_buffer);
      post_receive(id);
      ctx->msg->id = MSG_READY;
      send_message(id);
    }
  } else {
    if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
      uint32_t size = ntohl(wc->imm_data);
      if(size == 0) {
          ctx->msg->id = MSG_DONE;
          send_message(id);
          return;
      }
      printf("Received data %s\n", ctx->buffer);
      post_receive(id);
      ctx->msg->id = MSG_READY;
      send_message(id);
    }
  }
}

int main(int argc, char **argv)
{
  rc_init(
    on_pre_conn,
    on_connection,
    on_completion,
    on_disconnect);

  printf("waiting for connections. interrupt (^C) to exit.\n");

  rc_server_loop(DEFAULT_PORT);

  return 0;
}
