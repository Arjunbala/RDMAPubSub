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
};


static char* buffer = NULL;
static struct ibv_mr *buffer_mr;
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

  if (buffer == NULL) {
      posix_memalign((void **)&ctx->buffer, sysconf(_SC_PAGESIZE), BUFFER_SIZE);
      TEST_Z(ctx->buffer_mr = ibv_reg_mr(rc_get_pd(), ctx->buffer, BUFFER_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ));
      buffer = ctx->buffer;
      buffer_mr = ctx->buffer_mr;
      printf("Registering to new memory...\n");
  } else {
      printf("Registering to the existing memory...\n");
      ctx->buffer = buffer;
      ctx->buffer_mr = buffer_mr;
  }
  printf("Assigned buffer....\n");

  posix_memalign((void **)&ctx->msg, sysconf(_SC_PAGESIZE), sizeof(*ctx->msg));
  TEST_Z(ctx->msg_mr = ibv_reg_mr(rc_get_pd(), ctx->msg, sizeof(*ctx->msg), 0));

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
  ibv_dereg_mr(ctx->buffer_mr);
  ibv_dereg_mr(ctx->msg_mr);
  free(ctx->buffer);
  free(ctx->msg);
  free(ctx);
}

static void on_completion(struct ibv_wc *wc)
{
  struct rdma_cm_id *id = (struct rdma_cm_id *)(uintptr_t)wc->wr_id;
  struct conn_context *ctx = (struct conn_context *)id->context;

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
void *produce_data() {
    printf("1");
    while (buffer == NULL);
    printf("2");
    int size = 0;
    sprintf(buffer,"%04d", size);
    // Write data
    size = 13;
    sprintf(buffer,"%04d", size);
    sprintf(buffer + strlen(buffer), "Hello/Danish");
    printf("Look at the buffer: %s\n", buffer);
    pthread_exit(0);
}
int main(int argc, char **argv)
{
  pthread_t producer_thread;
  rc_init(
    on_pre_conn,
    on_connection,
    on_completion,
    on_disconnect);
  printf("3");

  pthread_create(&producer_thread, NULL, produce_data, NULL);
  printf("waiting for connections. interrupt (^C) to exit.\n");

  rc_server_loop(DEFAULT_PORT);

  return 0;
}


