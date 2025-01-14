#include "msg_queue.h"
#include "rdma_comm.h"
#include <stdio.h>
#include <stdatomic.h>
#include <semaphore.h>

extern pthread_cond_t cond_listen;
pthread_cond_t cond_server = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock_server = PTHREAD_MUTEX_INITIALIZER;

void* rdma_server_thread(void *arg) {
    while (1) {
        pthread_mutex_lock(&lock_server);
        if (atomic_load(&(inqueue.busy_value)) == 0) {
            pthread_cond_wait(&cond_server, &lock_server);
        }
        
        // 打印接收到的数据
        printf("Received data at in_mr[%d] address %p: ", 
                   inqueue.head, in_mr[inqueue.head]->addr);
        // 假设数据是字符串
        printf("%s\n", ((jia_msg_t *)in_mr[inqueue.head]->addr)->data);

        inqueue.head = (inqueue.head + 1) % inqueue.size;
        atomic_fetch_sub(&(inqueue.busy_value), 1);
        atomic_fetch_add(&(inqueue.free_value), 1);

        if (atomic_load(&(inqueue.free_value)) >= batching_num) {
            pthread_cond_signal(&cond_listen);
        }
        pthread_mutex_unlock(&lock_server);
    }
    
    return NULL;
}