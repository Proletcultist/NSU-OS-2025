#pragma once

#include <stdbool.h>

#define NAME vector_char_t
#define TYPE char
#define VECTOR_DECL
#include "template/vector.h"
#undef VECTOR_DECL
#undef TYPE
#undef NAME

#define HTTP_STATE_MACHINE_INITIALIZER ((http_state_machine_t) \
                                        { \
                                         .state = READING_REQUEST_LINE, \
                                         .analyzed = 0, \
                                         .data = (vector_char_t) VECTOR_INITIALIZER \
                                        })
typedef enum http_state_machine_state {
    READING_REQUEST_LINE,
    READ_REQUEST_LINE,
    READING_HEADERS,
    HEADER_AVAILABLE,
    MALFORMED,
    COMPLETE
} http_state_machine_state_t;

typedef struct uri {
    char *buffer;
    char *hostname;
    char *port;
    char *path;
} uri_t;

typedef struct header {
    char *name;
    char *value;
    size_t name_size, value_size;
} header_t;

typedef enum http_method {
    GET,
    POST,
    HEAD,
    UNKNOWN_METHOD
} http_method_t;

typedef enum http_version {
    HTTP_1_0,
    HTTP_1_1
} http_version_t;

typedef struct http_state_machine {
    http_state_machine_state_t state;   
    http_method_t method;
    uri_t uri;
    http_version_t version;
    header_t last_header;

    size_t analyzed;
    vector_char_t data;
} http_state_machine_t;

void http_state_machine_analyze_next_line(http_state_machine_t *data);
void http_state_machine_destruct(http_state_machine_t *data);
