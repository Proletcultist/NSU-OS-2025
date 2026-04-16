#pragma once

#include "exchange_buffer.h"

typedef enum client_status {
    READ_REQUEST,
    SEND_RESPONSE
} client_status_t;

typedef struct client_connection {
    client_status_t status;

    size_t bytes_sended_back;

    exchange_buffer_t *request_exchange;
    exchange_buffer_t *response_exchange;
} client_connection_t;

typedef enum server_status {
    SEND_REQUEST,
    WRITE_RESPONSE
} server_status_t;

typedef struct server_connection {
    server_status_t status;

    size_t bytes_sended;

    exchange_buffer_t *request_exchange;
    exchange_buffer_t *response_exchange;
} server_connection_t;

typedef enum connection_type {
    CLIENT_CONNECTION,
    SERVER_CONNECTION
} connection_type_t;

typedef struct connection {
    connection_type_t type;
    union {
        client_connection_t client;
        server_connection_t server;
    } c;
} connection_t;
