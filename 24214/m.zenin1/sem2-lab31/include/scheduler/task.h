#pragma once

#include <time.h>
#include <stddef.h>
#include <sys/types.h>

typedef enum task_type {
    ACCEPT_CONNECTION_REQUESTS,
    WAIT_FOR_CONNECTION,
    READ_REQUEST,
    WRITE_REQUEST
} task_type_t;

typedef struct task {
    task_type_t type;

    int fd;
    void *buffer;
    size_t size;

    time_t last_update;
    double timeout;

    void *data;
    void (*callback)(ssize_t, int, void*);

    size_t written;
    struct task *next;
} task_t;
