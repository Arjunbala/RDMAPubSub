#include "rdma_consumer.h"
#include "common.h"
#include <unistd.h>
#include <pthread.h>

void *client(void *ptr) {
   char* thread_name = (char*)ptr;
   init("10.0.0.3");
   struct ProducerMessage* m = consumeRecord();
   printf("%s got key: %s, value:%s\n", thread_name, m->key, m->value);
   struct ProducerMessage* m1 = consumeRecord();
   printf("%s got key: %s, value:%s\n", thread_name, m1->key, m1->value);
   terminate();
   pthread_exit(0);
}

int main()
{
   pthread_t thread1, thread2;
   char* thread_one = "ThreadOne";
   char* thread_two = "ThreadTwo";
   pthread_create(&thread1, NULL, client, (void*)thread_one);
   pthread_create(&thread2, NULL, client, (void*)thread_two);
   pthread_join(thread1, NULL);
   pthread_join(thread2, NULL);
   return 0;
}
