#pragma once

typedef struct signal {
    void (*callback)(int, void*);
    void *data;

    struct signal *next;
} signal_t;

