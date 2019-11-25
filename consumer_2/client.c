#include "rdma_consumer.h"
#include "common.h"
#include <unistd.h>

int main()
{
   init("10.0.0.3");
   struct ProducerMessage* m = consumeRecord();
   printf("Client got key: %s, value:%s\n", m->key, m->value);
   struct ProducerMessage* m1 = consumeRecord();
   printf("Client got key: %s, value:%s\n", m1->key, m1->value);
   terminate();
   return 0;
}
