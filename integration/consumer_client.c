#include "rdma_consumer.h"
#include "common.h"
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>

#ifndef NUM_RECORDS
    #define NUM_RECORDS 1000
#endif

#ifndef KEY_SIZE
    #define KEY_SIZE 32 // in bytes
#endif

#ifndef VAL_SIZE
    #define VAL_SIZE 64 // in bytes
#endif

float get_time_elapsed_sec(struct timeval tv1, struct timeval tv2) {
    struct timeval tvdiff = { tv2.tv_sec - tv1.tv_sec, tv2.tv_usec - tv1.tv_usec };
    if (tvdiff.tv_usec < 0) { tvdiff.tv_usec += 1000000; tvdiff.tv_sec -= 1; }
    return tvdiff.tv_sec + (float)(tvdiff.tv_usec)/(1000*1000);
}


int main(int argc, char **argv)
{
   init(argv[1]);
   sleep(5);
   struct timeval tv1, tv2;
   int i;
   consumeRecord();
   gettimeofday(&tv1, NULL);
   for(i=0;i<NUM_RECORDS;i++) {
       consumeRecord();
       printf("%d\n", i);
   }
   gettimeofday(&tv2, NULL);

   float dataSentBytes = (NUM_RECORDS)*(KEY_SIZE+VAL_SIZE);
   float timeElapsedSec = get_time_elapsed_sec(tv1,tv2);
   float throughputMBps = dataSentBytes/(timeElapsedSec*1000000);

   printf("Read throughput: %f\n MBps", throughputMBps); 
   terminate();
   return 0;
}
