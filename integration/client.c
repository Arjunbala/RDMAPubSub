#include "rdma_producer.h"
#include <unistd.h>

int main(int argc, char **argv)
{
   init(argv[0]);
   produceRecord("1","Arjun");
   produceRecord("2","Danish");
   sleep(5);
   produceRecord("3","Danish");
   terminate();
   return 0;
}
