// Copyright (c) 2025 Youpen-y. Licensed under MIT.
#include "msg_queue.h"
#include "rdma_comm.h"
#include "rdma_mesh.h"
#include "tools.h"
#include <infiniband/verbs.h>
#include <pthread.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// queue capacity
#define MAX_HOSTS 16
#define QUEUESIZE 16
#define MR_SIZE 16
#define RANDOM_MSG_SIZE 10

FILE *logfile;

pthread_t rdma_client_tid;
pthread_t rdma_listen_tid;
pthread_t rdma_server_tid;

extern msg_queue_t inqueue;
extern msg_queue_t outqueue;
extern struct rdma_cm_id cm_id_array[MAX_HOSTS];

struct ibv_mr *in_mr[MR_SIZE];
struct ibv_mr *out_mr[MR_SIZE];

struct host_context ctx = {0};

void generate_random_string(char *dest, size_t length);
int move_msg_to_outqueue(jia_msg_t *msg, msg_queue_t *outqueue);
unsigned char **creat_queue(int size, int pagesize);
void free_queue(unsigned char **queue, int size);

const char *ip_array[2] = {
    "192.168.103.1",
    "192.168.103.2"
};

int main(int argc, char **argv) {
    if (open_logfile("jiajia.log")) {
        log_err("Unable to open jiajia.log");
        exit(-1);
    }
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
            pthread_create(&ctx.client_threads[i], NULL, run_client,
                           target_host);
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

    // 构建输入和输出队列
    init_msg_queue(&inqueue, QUEUESIZE);
    init_msg_queue(&outqueue, QUEUESIZE);

    // 注册内存区域
    for (int i = 0; i < MR_SIZE; i++) {
        in_mr[i] = rdma_reg_msgs(&cm_id_array[cm_id], inqueue.queue[i], 40960);
        out_mr[i] = rdma_reg_msgs(&cm_id_array[cm_id], outqueue.queue[i], 40960);
    }

    if (sem_init(&recv_sem, 0, 0) != 0) {
        perror("Failed to initialize semaphore");
        return -1;
    }

    pthread_create(&rdma_listen_tid, NULL, rdma_listen_thread, NULL);
    pthread_create(&rdma_client_tid, NULL, rdma_client_thread, NULL);
    pthread_create(&rdma_server_tid, NULL, rdma_server_thread, NULL);

    jia_msg_t msg;
    while (1) {
        msg.frompid = 0;
        msg.topid = 1;
        msg.temp = -1;
        msg.seqno = 0;
        msg.index = 0;
        msg.scope = 0;
        msg.size = 16;
        generate_random_string((char *)msg.data, RANDOM_MSG_SIZE);

        move_msg_to_outqueue(&msg, &outqueue);
        sleep(1);
    }

    return 0;
}

void generate_random_string(char *dest, size_t length) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxy"
                           "z0123456789";      // 字符集
    size_t charset_size = sizeof(charset) - 1; // 不包括末尾的 '\0'

    // 获取时间和进程 ID 作为种子
    srand((unsigned int)(time(NULL) ^ getpid()));

    // 生成随机字符串并存储到 dest 中
    for (size_t i = 0; i < length - 1; ++i) { // 留出最后一个字符的位置给 '\0'
        dest[i] = charset[rand() % charset_size];
    }

    dest[length - 1] = '\0'; // 添加字符串结束符
    fprintf(stdout, "generate string: %s\n", dest);
}

int move_msg_to_outqueue(jia_msg_t *msg, msg_queue_t *outqueue) {
    int ret = enqueue(outqueue, msg);
    if (ret == -1) {
        perror("enqueue");
        return ret;
    }
    return 0;
}

unsigned char **creat_queue(int size, int pagesize) {
    int ret;

    unsigned char **queue =
        (unsigned char **)malloc(sizeof(unsigned char *) * size);
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