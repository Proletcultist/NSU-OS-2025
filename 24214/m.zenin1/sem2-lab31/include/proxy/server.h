#pragma once

#include <stddef.h>
#include "scheduler/task.h"
#include "cache/cache.h"
#include "http.h"

typedef struct try_connect_to_server_task {
    task_t task;
    uri_t uri;
    cache_entry_t *cache_entry;
    struct addrinfo *first;
    struct addrinfo *next_try;
} try_connect_to_server_task_t;

typedef struct response_analysis_task {
    task_t task;
    uri_t uri;
    cache_entry_t *cache_entry;
    http_state_machine_t sm;
} response_analysis_task_t;

typedef struct request_writing_task {
    task_t task;
    uri_t uri;
    cache_entry_t *cache_entry;
} request_writing_task_t;

void establish_connect_with_server(uri_t uri, cache_entry_t *entry);
void try_connect_callback(ssize_t r, int err, void *udata);
void write_request_callback(ssize_t w, int err, void *udata);
void analyze_response_callback(ssize_t r, int err, void *udata);
