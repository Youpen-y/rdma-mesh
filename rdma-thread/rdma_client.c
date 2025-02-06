#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <infiniband/verbs.h>
#include "msg_queue.h"
#include "rdma_comm.h"

#define QUEUESIZE 16

void* rdma_client_thread(void *arg) {
    struct ibv_wc wc;
    int i, ret;
    
    while (1) {
        sem_wait(&(outqueue.busy_count));

        struct ibv_sge sge = {
            .addr = (uint64_t)out_mr[outqueue.head]->addr,
            .length = out_mr[outqueue.head]->length,
            .lkey = out_mr[outqueue.head]->lkey
        };

        struct ibv_send_wr send_wr = {
            .sg_list = &sge,
            .num_sge = 1,
            .next = NULL,
            .wr_id = outqueue.head,
            .opcode = IBV_WR_SEND,
            .send_flags = IBV_SEND_SIGNALED
        };
        
        struct ibv_send_wr *bad_wr;
        ret = ibv_post_send(cm_id_array[1].qp, &send_wr, &bad_wr);
        if (ret) {
            printf("Failed to post send WR batch\n");
            continue;
        }
        
        // 等待发送完成
        while(1){
            ret = ibv_poll_cq(cm_id_array[1].qp->send_cq, 1, &wc);
            if (ret < 0) {
                printf("Failed to poll send CQ\n");
                continue;
            } else if (ret == 0) {
                continue;
            }else {
                break;
            }
        }
        
        // 处理发送完成
        if (wc.status != IBV_WC_SUCCESS) {
            printf("Send completion failed with status: %d\n", wc.status);
        }
        outqueue.head = (outqueue.head + 1) % QUEUESIZE;

        sem_post(&(outqueue.free_count));
    }
    
    return NULL;
}