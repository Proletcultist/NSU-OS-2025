#pragma once

#include <stddef.h>
#include <stdbool.h>

#define NAME vector_byte_t
#define TYPE char
#define VECTOR_DECL
#include "template/vector.h"
#undef VECTOR_DECL
#undef NAME
#undef TYPE

typedef struct exchange_callback {
    int a;
} exchange_callback_t;

typedef struct exchange_buffer {
    vector_byte_t buffer;
    bool active;

    exchange_callback_t *callback;
} exchange_buffer_t;

exchange_buffer_t exchange_buffer_construct();
char* exchange_buffer_reserve(exchange_buffer_t *eb, size_t reserving);
void exchange_buffer_destruct(exchange_buffer_t *eb);
