#pragma once

#define MAX_HEADERS_SIZE (64 * 1024)    // 64KB
#define MAX_LINE_SIZE (8 * 1024)    // 8KB

#define MIN(a, b) ((a) < (b) ? (a) : (b))

typedef enum proxy_client_state {
    CLIENT_CONNECTED,
    CLIENT_DISCONNECTED
} proxy_client_state_t;

typedef struct proxy_client {
    proxy_client_state_t state;
    char client_ip[16];
} proxy_client_t;

typedef struct request_analysis_task {
    task_t task;
    proxy_client_t *client;

    size_t bytes_received;
    http_state_machine_t sm;

    bool bad_request;
    char *msg;
    size_t msg_size;
} request_analysis_task_t;

void analyze_request_callback(ssize_t r, int err, void *udata);
void respond_error_callback(ssize_t r, int err, void *udata);
