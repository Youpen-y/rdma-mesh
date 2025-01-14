#include "rdma_comm.h"
#include <stdio.h>
#include <semaphore.h>


void* rdma_server(void *arg) {
    int ret;
    
    while (1) {
        // 等待接收完成信号
        ret = sem_wait(&recv_sem);
        if (ret) {
            continue;
        }
        
        // 打印接收到的数据
        for (int i = 0; i < ctx.mr_count; i++) {
            printf("Received data at in_mr[%d] address %p: ", 
                   i, ctx.in_mr[i]->addr);
            // 假设数据是字符串
            printf("%s\n", (char*)ctx.in_mr[i]->addr);
        }
    }
    
    return NULL;
}