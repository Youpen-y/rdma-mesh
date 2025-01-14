#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include "rdma_mesh.h"
#include "rdma_comm.h"

// queue capacity
#define MAX_HOSTS 16

#define QUEUESIZE 16

#define MR_SIZE 16

pthread_t rdma_client_tid;
pthread_t rdma_listen_tid;
pthread_t rdma_server_tid;


extern struct rdma_cm_id cm_id_array[MAX_HOSTS];

unsigned char **inqueue;
unsigned char **outqueue;

struct ibv_mr *in_mr[MR_SIZE];
struct ibv_mr *out_mr[MR_SIZE];

struct host_context ctx = {0};

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

    ctx.host_id = atoi(argv[1]);
    ctx.total_hosts = atoi(argv[2]);

    if (ctx.host_id >= ctx.total_hosts) {
        fprintf(stderr, "Invalid host_id\n");
        return 1;
    }

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

    // 获取 pagesize 大小
    long pagesize = sysconf(_SC_PAGESIZE);

    // 构建输入和输出队列
    inqueue = creat_queue(QUEUESIZE, pagesize);
    outqueue = creat_queue(QUEUESIZE, pagesize);

    // 注册内存区域
    for (int i = 0; i < MR_SIZE; i++) {
        in_mr[i] = rdma_reg_msgs(&cm_id_array[1], inqueue[i], 40960);
        out_mr[i] = rdma_reg_msgs(&cm_id_array[1], outqueue[i], 40960);
    }

    if (sem_init(&recv_sem, 0, 0) != 0) {
        perror("Failed to initialize semaphore");
        return -1;
    }

    pthread_create(&rdma_listen_tid, NULL, rdma_listen_thread, NULL);
    pthread_create(&rdma_client_tid, NULL, rdma_client_thread, NULL);
    pthread_create(&rdma_server_tid, NULL, rdma_server_thread, NULL);
    return 0;
}
