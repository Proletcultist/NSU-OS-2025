#include "exchange_buffer.h"

exchange_buffer_t exchange_buffer_construct() {
    return (exchange_buffer_t) {.readers_amount = 0, .writing = false, .buffer = vector_byte_t_construct()};
}

void exchange_buffer_start_writing(exchange_buffer_t *eb) {
    eb->writing = true;
}

void exchange_buffer_start_reading(exchange_buffer_t *eb) {
    eb->readers_amount++;
}

void exchange_buffer_push(exchange_buffer_t *eb, char *data, size_t size) {
    vector_byte_t_insert(&eb->buffer, eb->buffer.size, data, size);
}

void exchange_buffer_destruct(exchange_buffer_t *eb) {
    vector_byte_t_destruct(&eb->buffer);
}
