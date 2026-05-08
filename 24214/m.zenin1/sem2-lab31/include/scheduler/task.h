#pragma once

#include <time.h>
#include <stddef.h>
#include <sys/types.h>
#include "scheduler/timer.h"

typedef enum task_type {
    DELEGATE,
    ADD_TIMER,
    ACCEPT_CONNECTION_REQUESTS,
    WAIT_FOR_CONNECTION,
    READ_REQUEST,
    WRITE_REQUEST,
    UNDELEGATE
} task_type_t;

typedef union task_attrs {
    struct {
        int fd;
        bool as_first;
        void *buffer;
        size_t size;

        void *data;
        void (*callback)(ssize_t, int, void*);

        size_t written;
    } io;

    struct {
        int fd;

        void *data;
        void (*callback)(int, void*);
    } ctl;

    aio_timer_t timer;
} task_attrs_t;

typedef struct task {
    task_type_t type;
    task_attrs_t attrs;
    struct task *next;
} task_t;

