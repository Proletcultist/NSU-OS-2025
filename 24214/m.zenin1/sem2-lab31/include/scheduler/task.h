#pragma once

#include <stddef.h>
#include <sys/types.h>

typedef enum task_type {
    ACCEPT_CONNECTION_REQUESTS,
    READ_REQUEST,
    WRITE_REQUEST
} task_type_t;

typedef struct task {
    task_type_t type;

    int fd;
    void *buffer;
    size_t size;

    void *data;
    void (*callback)(ssize_t, int, void*);

    size_t written;
    struct task *next;
} task_t;
