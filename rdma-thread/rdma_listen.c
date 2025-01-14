#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <infiniband/verbs.h>
#include "rdma_comm.h"

// 处理接收完成的函数
static void handle_recv_completion(struct ibv_wc *wc) {
    if (wc->status != IBV_WC_SUCCESS) {
        printf("Recv completion failed with status: %d\n", wc->status);
        return;
    }
    // 通知打印线程有新数据到达
    sem_post(&recv_sem);
}

// 下发接收WR的线程函数
void* rdma_listen_thread(void *arg) {
    struct ibv_recv_wr *wr_list = malloc(batching_num * sizeof(struct ibv_recv_wr));
    struct ibv_sge *sge_list = malloc(batching_num * sizeof(struct ibv_sge));
    struct ibv_wc wc;
    int i, ret;
    
    while (1) {
        // 准备一批接收WR
        for (i = 0; i < batching_num; i++) {
            sge_list[i].addr = (uint64_t)in_mr[i]->addr;
            sge_list[i].length = in_mr[i]->length;
            sge_list[i].lkey = in_mr[i]->lkey;
            
            wr_list[i].sg_list = &sge_list[i];
            wr_list[i].num_sge = 1;
            wr_list[i].next = (i < batching_num - 1) ? &wr_list[i + 1] : NULL;
            wr_list[i].wr_id = i;
        }
        
        // 批量下发接收WR
        struct ibv_recv_wr *bad_wr;
        ret = ibv_post_recv(cm_id_array[1].qp, wr_list, &bad_wr);
        if (ret) {
            printf("Failed to post recv WR batch\n");
            continue;
        }

        // 等待接收完成
        int recv_num = 0;
        while (recv_num < batching_num) {
            ret = ibv_poll_cq(cm_id_array[1].recv_cq, 1, &wc);
            if (ret < 0) {
                printf("Failed to poll CQ\n");
                continue;
            } else if (ret == 0) {
                continue;
            }
            
            // 处理接收完成
            handle_recv_completion(&wc);
            recv_num++;
        }
    }
    
    free(wr_list);
    free(sge_list);
    return NULL;
}