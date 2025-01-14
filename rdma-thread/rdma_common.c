#include "rdma_comm.h"
#include <semaphore.h>

sem_t recv_sem;
int batching_num = 8;