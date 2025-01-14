#ifndef RDMA_COMM_H
#define RDMA_COMM_H

#include <pthread.h>
#include <semaphore.h>
#include <rdma/rdma_cma.h>

#define MR_SIZE 16
#define MAX_HOSTS 16

extern struct ibv_mr *in_mr[MR_SIZE];
extern struct ibv_mr *out_mr[MR_SIZE];
extern struct rdma_cm_id cm_id_array[MAX_HOSTS];



extern sem_t recv_sem;
extern int batching_num;
extern pthread_t rdma_client_tid;
extern pthread_t rdma_listen_tid;
extern pthread_t rdma_server_tid;

/**
 * @brief rdma_listen -- rdma listen thread (post recv wr by doorbell batching)
 */
void *rdma_listen_thread(void *arg);

/**
 * @brief rdma_client -- rdma client thread (post send wr)
 */
void *rdma_client_thread(void *arg);

/**
 * @brief rdma_server -- rdma server thread (handle msg)
 */
void *rdma_server_thread(void *arg);
#endif