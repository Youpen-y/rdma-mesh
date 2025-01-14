#ifndef RDMA_COMM_H
#define RDMA_COMM_H

#include <pthread.h>
#include <semaphore.h>

extern sem_t recv_sem;
extern pthread_t rdma_client_tid;
extern pthread_t rdma_listen_tid;
extern pthread_t rdma_server_tid;

/**
 * @brief rdma_listen -- rdma listen thread (post recv wr by doorbell batching)
 */
void *rdma_listen(void *arg);

/**
 * @brief rdma_client -- rdma client thread (post send wr)
 */
void *rdma_client(void *arg);

/**
 * @brief rdma_server -- rdma server thread (handle msg)
 */
void *rdma_server(void *arg);
#endif