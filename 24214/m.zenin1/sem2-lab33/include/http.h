#pragma once

#include <stdbool.h>

#define NAME vector_char_t
#define TYPE char
#define VECTOR_DECL
#include "template/vector.h"
#undef VECTOR_DECL
#undef TYPE
#undef NAME

#define HTTP_STATE_MACHINE_REQ_INITIALIZER ((http_state_machine_t) \
                                            { \
                                             .state = READING_REQUEST_LINE, \
                                             .available_lines = 0, \
                                             .analyzed = 0, \
                                             .discarding = false, \
                                             .data = (vector_char_t) VECTOR_INITIALIZER \
                                            })
#define HTTP_STATE_MACHINE_RES_INITIALIZER ((http_state_machine_t) \
                                            { \
                                             .state = READING_STATUS_LINE, \
                                             .available_lines = 0, \
                                             .analyzed = 0, \
                                             .discarding = false, \
                                             .data = (vector_char_t) VECTOR_INITIALIZER \
                                            })

typedef enum http_state_machine_state {
    READING_REQUEST_LINE,
    READ_REQUEST_LINE,
    READING_STATUS_LINE,
    READ_STATUS_LINE,
    READING_HEADER,
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
    size_t name_off, name_size;
    size_t value_off, value_size;
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

    unsigned int status;

    bool discarding;

    size_t available_lines;
    size_t analyzed;
    vector_char_t data;
} http_state_machine_t;

int http_state_machine_alloc(http_state_machine_t *sm, void **buffer, size_t *size);
void http_state_machine_feed(http_state_machine_t *sm, size_t size);
bool http_state_machine_step(http_state_machine_t *sm);
void http_state_machine_destruct(http_state_machine_t *sm);

void http_state_machine_get_header_name(http_state_machine_t *sm, header_t header, char **buff, size_t *size);
void http_state_machine_get_header_value(http_state_machine_t *sm, header_t header, char **buff, size_t *size);
void http_state_machine_delete_header(http_state_machine_t *sm, header_t header);
void http_state_machine_add_header(http_state_machine_t *sm, char *name, size_t name_size, char *value, size_t value_size);
