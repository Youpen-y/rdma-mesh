#include "msg_queue.h"
#include "rdma_comm.h"
#include "rdma_mesh.h"
#include <infiniband/verbs.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

extern struct host_context ctx;
extern pthread_cond_t cond_server;
pthread_cond_t cond_listen = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock_listen = PTHREAD_MUTEX_INITIALIZER;

// 下发接收WR的线程函数
void *rdma_listen_thread(void *arg) {
    struct ibv_recv_wr *wr_list =
        malloc(batching_num * sizeof(struct ibv_recv_wr));
    struct ibv_sge *sge_list = malloc(batching_num * sizeof(struct ibv_sge));
    struct ibv_wc wc;
    int i, ret;
    int j = 0;
    while (1) {
        pthread_mutex_lock(&lock_listen);

        if (atomic_load(&(inqueue.free_value)) < batching_num) {
            pthread_cond_wait(&cond_listen, &lock_listen);
        }

        // 准备一批接收WR
        for (i = 0; i < batching_num; i++) {
            sge_list[i].addr = (uint64_t)in_mr[j]->addr;
            sge_list[i].length = in_mr[j]->length;
            sge_list[i].lkey = in_mr[j]->lkey;

            wr_list[i].sg_list = &sge_list[i];
            wr_list[i].num_sge = 1;
            wr_list[i].next = (i < batching_num - 1) ? &wr_list[i + 1] : NULL;
            wr_list[i].wr_id = i;
            j = (j + 1) % inqueue.size;
        }

        // 批量下发接收WR
        struct ibv_recv_wr *bad_wr;
        ret = ibv_post_recv(cm_id_array[cm_id].qp, wr_list, &bad_wr);
        if (ret) {
            printf("Failed to post recv WR batch\n");
            continue;
        }

        // 等待接收完成
        int recv_num = 0;
        while (recv_num < batching_num) {
            ret = ibv_poll_cq(cm_id_array[cm_id].qp->recv_cq, 1, &wc);
            if (ret < 0) {
                printf("Failed to poll CQ\n");
                continue;
            } else if (ret == 0) {
                continue;
            }

            if (wc.status != IBV_WC_SUCCESS) {
                printf("Recv completion failed with status: %d\n", wc.status);
            }
            recv_num++;
            atomic_fetch_sub(&(inqueue.free_value), 1);
            atomic_fetch_add(&(inqueue.busy_value), 1);
            if (atomic_load(&(inqueue.busy_value)) > 0) {
                pthread_cond_signal(&cond_server);
            }
        }
        pthread_mutex_unlock(&lock_listen);
    }

    free(wr_list);
    free(sge_list);
    return NULL;
}