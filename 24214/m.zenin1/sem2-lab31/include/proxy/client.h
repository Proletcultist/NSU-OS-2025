#pragma once

#include <time.h>
#include "scheduler/aio_scheduler.h"
#include "cache/cache.h"
#include "cache/cache_block.h"

#define CLIENT_TIMEOUT 5

#define MAX_HEADERS_SIZE (64 * 1024)    // 64KB
#define MAX_LINE_SIZE (8 * 1024)    // 8KB

#define MIN(a, b) ((a) < (b) ? (a) : (b))

struct cache_entry;

typedef enum proxy_client_state {
    CLIENT_SENDING_REQUEST,         // Receiving clients' request, if failed or timed out - start disconnection
    CLIENT_READING_CACHED,          // Reading data from cache, if failed or timed out - start disconnection
    CLIENT_WAITS_FOR_DATA,          // Waiting for data from server, if timed out - do nothing and wait
    CLIENT_RECEIVING_SERVER_DATA,   // Receiving data from server - if timed out - change state to disconnected
    CLIENT_DISCONNECTING,           // Don't start disconnection again, but check for timeouts
    CLIENT_DISCONNECTED             // Don't do anything
} proxy_client_state_t;

struct client_health_check_timer;

typedef struct proxy_client {
    proxy_client_state_t state;
    int fd;
    char client_ip[16];
    aio_scheduler_t *sched;
    struct cache_entry *entry;

    struct client_health_check_timer *health_check_timer;

    struct proxy_client *next;
} proxy_client_t;

typedef struct client_task {
    task_t task;
    proxy_client_t *client;
} client_task_t;

typedef struct client_health_check_timer {
    task_t task;
    proxy_client_t *client;

    bool cleanup_client;
    time_t last_update;
} client_health_check_timer_t;

typedef struct client_read_cache_task {
    task_t task;
    proxy_client_t *client;

    cache_block_t *current_block;
} client_read_cache_task_t;

typedef struct process_request_task {
    task_t task;
    proxy_client_t *client;

    size_t bytes_received;
    http_state_machine_t sm;

    bool bad_request;
    char *msg;
    size_t msg_size;
} process_request_task_t;

typedef client_task_t send_to_client_task_t;

void client_delegate_callback(int err, void *udata);
void client_health_check_callback(int err, time_t time, void *udata);
void client_silent_disconnect(client_task_t *task);
void client_respond_error(client_task_t *task, char *msg, size_t msg_size);
void process_request_callback(ssize_t r, int err, void *udata);
void client_write_cached_last_callback(ssize_t r, int err, void *udata);
void client_write_cached_callback(ssize_t r, int err, void *udata);
