#ifndef RDMA_MESH_H
#define RDMA_MESH_H

#include <pthread.h>

struct connection { // 暂未使用
    struct rdma_cm_id *id;
    struct ibv_qp *qp;
    int connected;
};

struct host_context {
    int host_id;
    int total_hosts;
    struct connection *connections;
    pthread_t server_thread;
    pthread_t *client_threads;
};

void *run_server(void *arg);

void *run_client(void *arg);
#endif