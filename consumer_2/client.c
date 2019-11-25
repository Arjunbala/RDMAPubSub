#include "rdma_consumer.h"
#include "common.h"
#include <unistd.h>

int main()
{
   init("10.0.0.3");
   struct ProducerMessage* m = consumeRecord();
   printf("Client got key: %s, value:%s\n", m->key, m->value);
   terminate();
   return 0;
}
