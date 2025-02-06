#ifndef RDMA_MESH_H
#define RDMA_MESH_H

#include <pthread.h>

#ifdef MASTER
#define cm_id 1
#else
#define cm_id 0
#endif

struct connection { // not used
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