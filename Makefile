CC = gcc
CFLAGS = -Wno-unused-variable -Wall -g -DMASTER
LIBS = -libverbs -lrdmacm -lpthread

INCLUDE_DIR = include
SRC_DIR = src
RDMA_SRC_DIR = rdma-mesh
RDMA_THREAD_DIR = rdma-thread
MSG_QUEUE_DIR = msg

SRCS = $(SRC_DIR)/main.c \
       $(RDMA_SRC_DIR)/rdma_mesh.c \
	   $(RDMA_THREAD_DIR)/rdma_client.c \
	   $(RDMA_THREAD_DIR)/rdma_common.c \
	   $(RDMA_THREAD_DIR)/rdma_listen.c \
	   $(RDMA_THREAD_DIR)/rdma_server.c \
	   $(MSG_QUEUE_DIR)/msg_queue.c \

OBJS = $(SRCS:.c=.o)

TARGET = mesh

DEPS = $(SRCS:.c=.d)

all: $(TARGET)

$(TARGET): $(OBJS) 
	$(CC) $(OBJS) $(LIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) $(LIBS) -c $< -o $@

%.d: %.c
	$(CC) -MM $(CFLAGS) -I$(INCLUDE_DIR) $< > $@

-include $(DEPS)

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET) $(OTHERS)

install: $(TARGET)
	# copy executable file to specified directory
	# cp $(TARGET) /usr/local/bin/

bear:
	bear -- make