#include "exchange_buffer.h"

exchange_buffer_t exchange_buffer_construct() {
    return (exchange_buffer_t) {.buffer = vector_byte_t_construct(), .active = false, .callback = NULL};
}

char* exchange_buffer_reserve(exchange_buffer_t *eb, size_t reserving) {
    vector_byte_t_reserve(&eb->buffer, eb->buffer.size + reserving);
    return eb->buffer.arr + eb->buffer.size;
}

void exchange_buffer_destruct(exchange_buffer_t *eb) {
    vector_byte_t_destruct(&eb->buffer);
    // TODO: Free all callbacks
}
