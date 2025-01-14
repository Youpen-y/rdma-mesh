#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include "rdma_mesh.h"

#define MAX_HOSTS 16
#define DEFAULT_PORT 40000
#define MAX_CONNECTIONS (MAX_HOSTS * (MAX_HOSTS-1) / 2)
#define MAX_RETRY 1000

extern struct host_context ctx;

struct rdma_cm_id cm_id_array[MAX_HOSTS] = {0};

void *run_server(void *arg) {
    struct host_context *ctx = (struct host_context *)arg;
    struct rdma_cm_id *listener = NULL;
    struct rdma_event_channel *ec = NULL;
    struct sockaddr_in addr;
    struct rdma_cm_event *event = NULL;
    struct ibv_qp_init_attr qp_attr;
    struct rdma_conn_param conn_param;  // 在 rdma_accept 时传递给客户端的 conn param 
    struct ibv_comp_channel *io_completion_channel;
    int ret;
    int completion_num = 0;     // 已建连的数目
    int client_id;              // 用于暂存发起连接的客户端标识

    // 创建事件通道
    ec = rdma_create_event_channel();
    if (!ec) {
        fprintf(stderr, "Host %d: Failed to create event channel\n", ctx->host_id);
        return NULL;
    }

    // 创建RDMA CM ID
    ret = rdma_create_id(ec, &listener, NULL, RDMA_PS_TCP);
    if (ret) {
        fprintf(stderr, "Host %d: Failed to create RDMA CM ID\n", ctx->host_id);
        goto cleanup;
    }

    // 绑定地址
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DEFAULT_PORT + ctx->host_id);
    addr.sin_addr.s_addr = inet_addr("192.168.103.1");  // 本机地址

    ret = rdma_bind_addr(listener, (struct sockaddr *)&addr);
    if (ret) {
        fprintf(stderr, "Host %d: Failed to bind address\n", ctx->host_id);
        goto cleanup;
    }

    // 开始监听
    ret = rdma_listen(listener, MAX_HOSTS);
    if (ret) {
        fprintf(stderr, "Host %d: Failed to listen\n", ctx->host_id);
        goto cleanup;
    }

    printf("Host %d: Server listening on port %d\n", ctx->host_id, DEFAULT_PORT + ctx->host_id);

    while (1) {
        ret = rdma_get_cm_event(ec, &event);
        if (ret) {
            continue;
        }

        switch (event->event) {
            case RDMA_CM_EVENT_CONNECT_REQUEST:
                // 获取 client 发来的其私有数据（即其 id 号），当然也可以一个 struct (条件是 < 56 bytes)
                client_id = *(int *)event->param.conn.private_data;
                // 测试是否收到
                printf("Received Connect Request from host %d\n", client_id);

                io_completion_channel = ibv_create_comp_channel(event->id->verbs);

                // 设置 QP 属性，这里可以指定通信模式
                memset(&qp_attr, 0, sizeof(qp_attr));
                qp_attr.qp_context = NULL;
                qp_attr.cap.max_send_wr = 10;
                qp_attr.cap.max_recv_wr = 10;
                qp_attr.cap.max_send_sge = 1;
                qp_attr.cap.max_recv_sge = 1;
                qp_attr.cap.max_inline_data = 88;
                qp_attr.qp_type = IBV_QPT_RC;
                qp_attr.send_cq = ibv_create_cq(event->id->verbs, 16, NULL, io_completion_channel, 0);
                qp_attr.recv_cq = ibv_create_cq(event->id->verbs, 16, NULL, io_completion_channel, 0);

                ibv_req_notify_cq(qp_attr.send_cq, 0);
                ibv_req_notify_cq(qp_attr.recv_cq, 0);

                ret = rdma_create_qp(event->id, NULL, &qp_attr);
                if (!ret) {
                    // server 可将自己的想要传的私有数据传递给 client
                    memset(&conn_param, 0, sizeof(conn_param));
                    conn_param.private_data = &(ctx->host_id);
                    conn_param.private_data_len = sizeof(ctx->host_id);
                    conn_param.responder_resources = 4;
                    conn_param.initiator_depth = 1;
                    conn_param.rnr_retry_count = 7;

                    ret = rdma_accept(event->id, &conn_param);
                    if (!ret) {
                        printf("Host %d: Accepted connection\n", ctx->host_id);
                    }
                }
                break;

            case RDMA_CM_EVENT_ESTABLISHED:
                printf("Host %d: Connection established\n", ctx->host_id);
                cm_id_array[client_id] = *(event->id);
                completion_num++;
                if (completion_num == ctx->host_id) {   
                    // server 完成了和 (0.. host_id-1) 主机的建连，可以结束了
                    goto cleanup;
                }
                break;

            case RDMA_CM_EVENT_DISCONNECTED:
                rdma_destroy_qp(event->id);
                rdma_destroy_id(event->id);
                printf("Host %d: Connection disconnected\n", ctx->host_id);
                break;

            default:
                break;
        }

        rdma_ack_cm_event(event);
    }

cleanup:
    if (listener) {
        rdma_destroy_id(listener);
    }
    if (ec) {
        rdma_destroy_event_channel(ec);
    }
    return NULL;
}

void *run_client(void *arg) {
    int target_host = *(int *)arg;
    struct rdma_cm_id *id = NULL;
    struct rdma_event_channel *ec = NULL;
    struct sockaddr_in addr;
    struct rdma_cm_event *event = NULL;
    struct ibv_qp_init_attr qp_attr;
    struct rdma_conn_param conn_param;
    struct ibv_comp_channel *io_completion_channel;

    int ret;
    bool retry_flag = true;
    int retry_count = 0;

    // 创建事件通道
    ec = rdma_create_event_channel();
    if (!ec) {
        fprintf(stderr, "Host %d: Failed to create event channel for client[%d]\n", ctx.host_id, target_host);
        return NULL;
    }

    while(retry_flag && retry_count < MAX_RETRY) {
        retry_flag = false;
        // 创建RDMA CM ID
        ret = rdma_create_id(ec, &id, NULL, RDMA_PS_TCP);
        if (ret) {
            fprintf(stderr, "Host %d: Failed to create RDMA CM ID for client[%d]\n", ctx.host_id, target_host);
            goto cleanup;
        }

        // 连接到目标主机
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(DEFAULT_PORT + target_host);
        addr.sin_addr.s_addr = inet_addr("192.168.103.2");

        ret = rdma_resolve_addr(id, NULL, (struct sockaddr *)&addr, 2000);
        if (ret) {
            fprintf(stderr, "Host %d: Failed to resolve address for host %d\n", ctx.host_id, target_host);
            goto cleanup;
        }

        while (1) {
            ret = rdma_get_cm_event(ec, &event);
            if (ret) {
                continue;
            }

            switch (event->event) {
                case RDMA_CM_EVENT_ADDR_RESOLVED:
                    ret = rdma_resolve_route(event->id, 2000);
                    break;

                case RDMA_CM_EVENT_ROUTE_RESOLVED:

                    io_completion_channel = ibv_create_comp_channel(event->id->verbs);

                    memset(&qp_attr, 0, sizeof(qp_attr));
                    qp_attr.qp_context = NULL;
                    qp_attr.cap.max_send_wr = 10;
                    qp_attr.cap.max_recv_wr = 10;
                    qp_attr.cap.max_send_sge = 1;
                    qp_attr.cap.max_recv_sge = 1;
                    qp_attr.cap.max_inline_data = 64;
                    qp_attr.qp_type = IBV_QPT_RC;
                    qp_attr.send_cq = ibv_create_cq(event->id->verbs, 16, NULL, io_completion_channel, 0);
                    qp_attr.recv_cq = ibv_create_cq(event->id->verbs, 16, NULL, io_completion_channel, 0);

                    ibv_req_notify_cq(qp_attr.send_cq, 0);
                    ibv_req_notify_cq(qp_attr.recv_cq, 0);                   

                    ret = rdma_create_qp(event->id, NULL, &qp_attr);
                    if (!ret) {
                        memset(&conn_param, 0, sizeof(conn_param));
                        
                        // 设置私有数据（即发起连接方的身份标识）
                        conn_param.private_data = &ctx.host_id;
                        conn_param.private_data_len = sizeof(ctx.host_id);
                        conn_param.initiator_depth = 1;

                        // 设置资源参数
                        conn_param.responder_resources = 2;     // 可同时处理 2 个 RDMA Read
                        conn_param.initiator_depth = 2;         // 可以发起 2 个并发的 RDMA Read
                        conn_param.flow_control = 1;            // 启用流控
                        conn_param.retry_count = 7;             // 发送重传 7 次
                        conn_param.rnr_retry_count = 7;         // RNR 重传 7 次

                        // 不使用 SRQ ，使用系统分配的 QP 号
                        conn_param.srq = 0;
                        conn_param.qp_num = 0;
            
                        ret = rdma_connect(event->id, &conn_param);
                    }
                    break;

                case RDMA_CM_EVENT_REJECTED:    // 当 client 早于远端 server 建立时，返回 RDMA_CM_EVENT_REJECTED
                    printf("Connect to host %d failed, event: %s", target_host, rdma_event_str(event->event));
                    // if connected failed, release the qp and id (need to reconsider)
                    if (retry_count < MAX_RETRY) {
                        retry_count++;
                        retry_flag = true;
                    }
                    if (id) {
                        rdma_destroy_qp(id);
                    }
                    id = NULL;  // 重置 id
                    goto next_try;

                case RDMA_CM_EVENT_ESTABLISHED:
                    printf("After retried %d connect, Host %d: Connected to host %d\n", retry_count, ctx.host_id, target_host);
                    cm_id_array[target_host] = *(event->id);
                    // 这里可以通过 event->param.conn.private_data 查看 server 返回的私有数据
                    printf("Connection setup with host %d\n", *(int *)event->param.conn.private_data);
                    goto cleanup;

                case RDMA_CM_EVENT_CONNECT_ERROR:
                case RDMA_CM_EVENT_UNREACHABLE:
                case RDMA_CM_EVENT_DISCONNECTED:
                    goto cleanup;

                default:
                    break;
            }

            rdma_ack_cm_event(event);
        } // while(1)

next_try:
    if (retry_flag) {
        if (id) {
            rdma_destroy_id(id);
            id = NULL;
        }
        continue;
    }
}   // while(retry_flag && retry_count < MAX_RETRY)

cleanup:
    if (ec) {
        rdma_destroy_event_channel(ec);
    }
    return NULL;
}
