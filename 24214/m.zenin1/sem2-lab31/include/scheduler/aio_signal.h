#pragma once

#include <stdint.h>

typedef struct signal_handler {
    uint8_t signum;
    void (*callback)(int, void*);
    void *data;

    struct signal_handler *next;
} signal_handler_t;

