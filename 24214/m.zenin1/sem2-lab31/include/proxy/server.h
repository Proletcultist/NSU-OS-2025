#pragma once

#include <time.h>
#include <stddef.h>
#include "scheduler/task.h"
#include "cache/cache.h"
#include "scheduler/aio_scheduler.h"
#include "http.h"

typedef enum proxy_server_state {
    SERVER_CONNECTION_IN_PROGRESS,
    SERVER_RECEIVING_HEADERS,
    SERVER_RECEIVING_BODY,
    SERVER_DISCONNECTED
} proxy_server_state_t;

struct server_health_check_timer;

typedef struct proxy_server {
    proxy_server_state_t state;
    int fd;
    aio_scheduler_t *sched;
    uri_t uri;

    struct server_health_check_timer *health_check_timer;

    cache_entry_t *cache_entry;
} proxy_server_t;

typedef struct server_task {
    task_t task;
    proxy_server_t *server;
} server_task_t;

typedef struct server_health_check_timer {
    task_t task;
    proxy_server_t *server;

    time_t last_update;
} server_health_check_timer_t;

typedef struct try_connect_to_server_task {
    task_t task;
    proxy_server_t *server;

    struct addrinfo *first;
    struct addrinfo *next_try;
} try_connect_to_server_task_t;

typedef struct response_analysis_task {
    task_t task;
    proxy_server_t *server;

    ssize_t content_length;
    http_state_machine_t sm;
} response_analysis_task_t;

typedef server_task_t request_writing_task_t;

void establish_connect_with_server(aio_scheduler_t *sched, uri_t uri, cache_entry_t *entry);
void try_connect_callback(ssize_t r, int err, void *udata);
void write_request_callback(ssize_t w, int err, void *udata);
void analyze_response_callback(ssize_t r, int err, void *udata);
