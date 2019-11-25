#include "rdma_consumer.h"
#include <unistd.h>

int main()
{
   init("10.0.0.3");
   consumeRecord();
   terminate();
   return 0;
}
