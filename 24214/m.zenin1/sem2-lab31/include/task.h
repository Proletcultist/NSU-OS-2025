#pragma once

#include <stddef.h>
#include <stdint.h>
#include "exchange_buffer.h"

typedef enum task_type {
    READ_REQUEST,
    SEND_REQUEST,
    ENCACHE_RESPONSE,
    SEND_CACHED_RESPONSE
} task_type_t;

typedef struct task {
    task_type_t type;
    struct task *next;
} task_t;

// CLIENT SOCKET TASKS

// Read request from socket to buffer
typedef struct read_request_task {
    task_t super;
    
    exchange_buffer_t *buffer;
} read_request_task_t;

// Send response from cache entry
typedef struct send_cached_response_task {
    task_t super;

    size_t read_from_buffer;
    exchange_buffer_t *buffer;
} send_cached_response_task_t;

// SERVER SOCKET TASKS

// Send request to server
typedef struct send_request_task {
    task_t super;

    size_t read_from_buffer;
    exchange_buffer_t *buffer;
} send_request_task_t;

typedef struct encache_response_task {
    task_t super;

    exchange_buffer_t *buffer;
} encache_response_task_t;


