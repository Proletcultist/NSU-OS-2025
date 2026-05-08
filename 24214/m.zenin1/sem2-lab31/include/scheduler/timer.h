#pragma once

#include <time.h>

typedef struct aio_timer {
    time_t time;

    void *data;
    void (*callback)(void*);
} aio_timer_t;

