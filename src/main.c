#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <infiniband/verbs.h>
#include "rdma_mesh.h"

// queue capacity
#define QUEUESIZE 16

unsigned char **msg_queue;

unsigned char **creat_queue(int size, int pagesize) {
    int ret;

    unsigned char **queue = (unsigned char **)malloc(sizeof(unsigned char *) * size);
    if (queue == NULL) {
        fprintf(stderr, "Failed to allocated memory for queue!");
        exit(-1);
    }

    for (int i = 0; i < size; i++) {
        ret = posix_memalign((void **)&queue[i], pagesize, 40960);
        if (ret != 0) {
            fprintf(stderr, "Allocated queue failed!");
            exit(-1);
        }
    }
    return queue;
}

void free_queue(unsigned char **queue, int size) {
    for (int i = 0; i < size; i++) {
        free(queue[i]);
    }
    free(queue);
}


int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <host_id> <total_hosts>\n", argv[0]);
        return 1;
    }

    struct host_context ctx;
    ctx.host_id = atoi(argv[1]);
    ctx.total_hosts = atoi(argv[2]);

    if (ctx.host_id >= ctx.total_hosts) {
        fprintf(stderr, "Invalid host_id\n");
        return 1;
    }

    // 获取 pagesize 大小
    long pagesize = sysconf(_SC_PAGESIZE);

    msg_queue = creat_queue(QUEUESIZE, pagesize);

    // 创建服务器线程
    if (ctx.host_id != 0) {
        pthread_create(&ctx.server_thread, NULL, run_server, &ctx);
    }

    // 创建客户端线程（如果需要）
    int num_clients = ctx.total_hosts - ctx.host_id - 1;
    if (num_clients > 0) {
        ctx.client_threads = malloc(num_clients * sizeof(pthread_t));
        for (int i = 0; i < num_clients; i++) {
            int *target_host = (int *)malloc(sizeof(int));
            *target_host = ctx.host_id + i + 1;
            pthread_create(&ctx.client_threads[i], NULL, run_client, target_host);
        }
    }

    // 等待服务器线程
    if (ctx.host_id != 0) {
        pthread_join(ctx.server_thread, NULL);
    }

    // 等待所有客户端线程
    if (num_clients > 0) {
        for (int i = 0; i < num_clients; i++) {
            pthread_join(ctx.client_threads[i], NULL);
        }
        free(ctx.client_threads);
    }

    free_queue(msg_queue, QUEUESIZE);
    return 0;
}
