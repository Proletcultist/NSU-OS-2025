#include <stddef.h>
#include <stdbool.h>

#define NAME vector_byte_t
#define TYPE char
#define VECTOR_DECL
#include "template/vector.h"
#undef VECTOR_DECL
#undef NAME
#undef TYPE

typedef struct exchange_buffer {
    size_t readers_amount;
    bool writing;
    vector_byte_t buffer;
} exchange_buffer_t;

exchange_buffer_t exchange_buffer_construct();
void exchange_buffer_start_writing(exchange_buffer_t *eb);
void exchange_buffer_start_reading(exchange_buffer_t *eb);
void exchange_buffer_push(exchange_buffer_t *eb, char *data, size_t size);
void exchange_buffer_destruct(exchange_buffer_t *eb);
