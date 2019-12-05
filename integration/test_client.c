#include "rdma_producer.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>

#ifndef NUM_RECORDS
    #define NUM_RECORDS 1024
#endif

#ifndef KEY_SIZE
    #define KEY_SIZE 32 // in bytes
#endif

#ifndef VAL_SIZE
    #define VAL_SIZE 64 // in bytes
#endif

char *rand_string(char *str, size_t size)
{
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJK.1234567890";
    if (size) {
        --size;
        size_t n;
        for (n = 0; n < size; n++) {
            int key = rand() % (int) (sizeof charset - 1);
            str[n] = charset[key];
        }
        str[size] = '\0';
    }
    return str;
}

float get_time_elapsed_sec(struct timeval tv1, struct timeval tv2) {
    struct timeval tvdiff = { tv2.tv_sec - tv1.tv_sec, tv2.tv_usec - tv1.tv_usec };
    if (tvdiff.tv_usec < 0) { tvdiff.tv_usec += 1000000; tvdiff.tv_sec -= 1; }
    return tvdiff.tv_sec + (float)(tvdiff.tv_usec)/(1000*1000);
}

int main(int argc, char *argv[])
{
    int i; 
    // Generate keys and values
    char **keys;
    keys = (char**) malloc(NUM_RECORDS*sizeof(char*));
    for(i=0;i<NUM_RECORDS;i++) {
        keys[i] = (char*)malloc(KEY_SIZE*sizeof(char));
	rand_string(keys[i], KEY_SIZE);
    }

    char **values;
    values = (char**) malloc(NUM_RECORDS*sizeof(char*));
    for(i=0;i<NUM_RECORDS;i++) {
	values[i] = (char*)malloc(VAL_SIZE*sizeof(char));
        rand_string(values[i], VAL_SIZE);
    }

    // Now, connect to the PubSub server
    init(argv[1]);
    sleep(5);

    // Produce the records
    struct timeval tv1, tv2;
    gettimeofday(&tv1, NULL);
    for(i=0;i<NUM_RECORDS;i++) {
        produceRecord(keys[i], values[i]);
    }    
    gettimeofday(&tv2, NULL);

    float dataSentBytes = (NUM_RECORDS)*(KEY_SIZE+VAL_SIZE);
    float timeElapsedSec = get_time_elapsed_sec(tv1,tv2);
    float throughputMBps = dataSentBytes/(timeElapsedSec*1000000);

    printf("Write throughput: %f\n MBps", throughputMBps); 

    // Terminate
    terminate();
    return 0;
}
