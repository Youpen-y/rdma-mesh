#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <infiniband/verbs.h>
#include "rdma_comm.h"

void* rdma_client_thread(void *arg) {
    struct ibv_send_wr *wr_list = malloc(batching_num * sizeof(struct ibv_send_wr));
    struct ibv_sge *sge_list = malloc(batching_num * sizeof(struct ibv_sge));
    struct ibv_wc wc;
    int i, ret;
    
    while (1) {
        // 准备一批发送WR
        for (i = 0; i < batching_num; i++) {
            sge_list[i].addr = (uint64_t)out_mr[i]->addr;
            sge_list[i].length = out_mr[i]->length;
            sge_list[i].lkey = out_mr[i]->lkey;
            
            wr_list[i].sg_list = &sge_list[i];
            wr_list[i].num_sge = 1;
            wr_list[i].next = (i < batching_num - 1) ? &wr_list[i + 1] : NULL;
            wr_list[i].wr_id = i;
            wr_list[i].opcode = IBV_WR_SEND;
            wr_list[i].send_flags = IBV_SEND_SIGNALED;
        }
        
        // 批量下发发送WR
        struct ibv_send_wr *bad_wr;
        ret = ibv_post_send(cm_id_array[1].qp, wr_list, &bad_wr);
        if (ret) {
            printf("Failed to post send WR batch\n");
            continue;
        }
        
        // 等待发送完成
        int send_num = 0;
        while (send_num < batching_num) {
            ret = ibv_poll_cq(cm_id_array[1].send_cq, 1, &wc);
            if (ret < 0) {
                printf("Failed to poll send CQ\n");
                continue;
            } else if (ret == 0) {
                continue;
            }
            
            // 处理发送完成
            if (wc.status != IBV_WC_SUCCESS) {
                printf("Send completion failed with status: %d\n", wc.status);
            }
            send_num++;
        }
    }
    
    free(wr_list);
    free(sge_list);
    return NULL;
}