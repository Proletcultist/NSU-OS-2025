#pragma once

#include <stddef.h>
#include "scheduler/task.h"
#include "cache/cache.h"

typedef struct try_connect_to_server_task {
    task_t task;
    struct addrinfo *first;
    struct addrinfo *next_try;
} try_connect_to_server_task_t;

typedef struct response_analysis_task {
    task_t task;
    uri_t uri;
    http_state_machine_t sm;
    cache_entry_t *cache_entry;
} response_analysis_task_t;

void try_connect_callback(ssize_t r, int err, void *udata);
void write_request_callback(ssize_t w, int err, void *udata);
void analyze_response_callback(ssize_t r, int err, void *udata);
