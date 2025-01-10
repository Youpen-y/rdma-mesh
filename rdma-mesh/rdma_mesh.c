#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "rdma_mesh.h"

#define MAX_HOSTS 16
#define DEFAULT_PORT 40000
#define MAX_CONNECTIONS (MAX_HOSTS * (MAX_HOSTS-1) / 2)

void *run_server(void *arg) {
    struct host_context *ctx = (struct host_context *)arg;
    struct rdma_cm_id *listener = NULL;
    struct rdma_event_channel *ec = NULL;
    struct sockaddr_in addr;
    struct rdma_cm_event *event = NULL;
    struct ibv_qp_init_attr qp_attr;
    int ret;

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
    addr.sin_addr.s_addr = inet_addr("192.168.103.1");

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
                // 处理连接请求
                memset(&qp_attr, 0, sizeof(qp_attr));
                qp_attr.qp_context = NULL;
                qp_attr.cap.max_send_wr = 10;
                qp_attr.cap.max_recv_wr = 10;
                qp_attr.cap.max_send_sge = 1;
                qp_attr.cap.max_recv_sge = 1;
                qp_attr.cap.max_inline_data = 88;
                qp_attr.qp_type = IBV_QPT_RC;
                qp_attr.send_cq = ibv_create_cq(event->id->verbs, 10, NULL, NULL, 0);
                qp_attr.recv_cq = qp_attr.send_cq;

                ret = rdma_create_qp(event->id, NULL, &qp_attr);
                if (!ret) {
                    ret = rdma_accept(event->id, NULL);
                    if (!ret) {
                        printf("Host %d: Accepted connection\n", ctx->host_id);
                    }
                }
                break;

            case RDMA_CM_EVENT_ESTABLISHED:
                printf("Host %d: Connection established\n", ctx->host_id);
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
    struct host_context *ctx = (struct host_context *)arg;
    struct rdma_cm_id *id = NULL;
    struct rdma_event_channel *ec = NULL;
    struct sockaddr_in addr;
    struct rdma_cm_event *event = NULL;
    struct ibv_qp_init_attr qp_attr;
    int target_host = *(int *)arg;
    int ret;

    // 创建事件通道
    ec = rdma_create_event_channel();
    if (!ec) {
        fprintf(stderr, "Host %d: Failed to create event channel for client\n", ctx->host_id);
        return NULL;
    }

    // 创建RDMA CM ID
    ret = rdma_create_id(ec, &id, NULL, RDMA_PS_TCP);
    if (ret) {
        fprintf(stderr, "Host %d: Failed to create RDMA CM ID for client\n", ctx->host_id);
        goto cleanup;
    }

    // 连接到目标主机
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DEFAULT_PORT + target_host);
    addr.sin_addr.s_addr = inet_addr("192.168.103.2");  // 这里假设所有主机都在本地运行

    ret = rdma_resolve_addr(id, NULL, (struct sockaddr *)&addr, 2000);
    if (ret) {
        fprintf(stderr, "Host %d: Failed to resolve address for host %d\n", ctx->host_id, target_host);
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
                memset(&qp_attr, 0, sizeof(qp_attr));
                qp_attr.qp_context = NULL;
                qp_attr.cap.max_send_wr = 10;
                qp_attr.cap.max_recv_wr = 10;
                qp_attr.cap.max_send_sge = 1;
                qp_attr.cap.max_recv_sge = 1;
                qp_attr.cap.max_inline_data = 64;
                qp_attr.qp_type = IBV_QPT_RC;
                qp_attr.send_cq = ibv_create_cq(event->id->verbs, 10, NULL, NULL, 0);
                qp_attr.recv_cq = qp_attr.send_cq;

                ret = rdma_create_qp(event->id, NULL, &qp_attr);
                if (!ret) {
                    ret = rdma_connect(event->id, NULL);
                }
                break;

            case RDMA_CM_EVENT_ESTABLISHED:
                printf("Host %d: Connected to host %d\n", ctx->host_id, target_host);
                goto cleanup;

            case RDMA_CM_EVENT_DISCONNECTED:
                goto cleanup;

            default:
                break;
        }

        rdma_ack_cm_event(event);
    }

cleanup:
    if (id) {
        rdma_destroy_id(id);
    }
    if (ec) {
        rdma_destroy_event_channel(ec);
    }
    return NULL;
}
