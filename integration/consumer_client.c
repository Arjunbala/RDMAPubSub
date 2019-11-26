#include "rdma_consumer.h"
#include "common.h"
#include <unistd.h>
#include <pthread.h>

int main()
{
   char* thread_name = "Consumer";
   init("10.0.0.3");
   struct ProducerMessage* m = consumeRecord();
   printf("%s got key: %s, value:%s\n", thread_name, m->key, m->value);
   struct ProducerMessage* m1 = consumeRecord();
   printf("%s got key: %s, value:%s\n", thread_name, m1->key, m1->value);
   terminate();
   return 0;
}
