.PHONY: clean

CFLAGS  := -Wall -Werror -g
LD      := gcc
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread

APPS    := producer_client consumer_client server

all: ${APPS}

producer_client: common.o rdma_producer_client.o client.o
	${LD} -o $@ $^ ${LDLIBS}

consumer_client: common.o rdma_consumer_client.o consumer_client.o
	${LD} -o $@ $^ ${LDLIBS}

server: common.o server.o
	${LD} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o ${APPS}
