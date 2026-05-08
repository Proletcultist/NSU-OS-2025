#pragma once

#include "scheduler/aio_scheduler.h"

#define MAX_HEADERS_SIZE (64 * 1024)    // 64KB
#define MAX_LINE_SIZE (8 * 1024)    // 8KB

#define MIN(a, b) ((a) < (b) ? (a) : (b))

typedef enum proxy_client_state {
    CLIENT_CONNECTED,
    CLIENT_DISCONNECTED
} proxy_client_state_t;

typedef struct proxy_client {
    proxy_client_state_t state;
    int fd;
    aio_scheduler_t *sched;
    char client_ip[16];

    struct proxy_client *next;
} proxy_client_t;

typedef struct process_request_task {
    task_t task;
    proxy_client_t *client;

    size_t bytes_received;
    http_state_machine_t sm;

    bool bad_request;
    char *msg;
    size_t msg_size;
} process_request_task_t;

typedef struct send_to_client_task {
    task_t task;
    proxy_client_t *client;
} send_to_client_task_t;

void client_respond_error(proxy_client_t *client, char *msg, size_t msg_size);
void process_request_callback(ssize_t r, int err, void *udata);
