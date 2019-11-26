#include "rdma_producer.h"
#include <unistd.h>

int main()
{
   init("10.0.0.1");
   produceRecord("1","Arjun");
   produceRecord("2","Danish");
   sleep(5);
   produceRecord("3","Danish");
   terminate();
   return 0;
}
