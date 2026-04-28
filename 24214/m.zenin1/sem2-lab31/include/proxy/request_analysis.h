#pragma once

#include <stdbool.h>
#include <netinet/in.h>

#define NAME vector_char_t
#define TYPE char
#define VECTOR_DECL
#include "template/vector.h"
#undef VECTOR_DECL
#undef TYPE
#undef NAME

#define REQUEST_ANALYSIS_DATA_INITIALIZER ((request_analysis_data_t) \
                                           { \
                                            .state = READING_REQUEST_LINE, \
                                            .analyzed = 0, \
                                            .data = (vector_char_t) VECTOR_INITIALIZER \
                                           })
typedef enum request_analysis_state {
    READING_REQUEST_LINE,
    READ_REQUEST_LINE,
    READING_HEADERS,
    HEADER_AVAILABLE,
    MALFORMED,
    COMPLETE
} request_analysis_state_t;

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

typedef struct request_analysis_data {
    request_analysis_state_t state;   
    http_method_t method;
    uri_t uri;
    http_version_t version;
    header_t last_header;

    size_t analyzed;
    vector_char_t data;
} request_analysis_data_t;

void try_analyze_req_line(request_analysis_data_t *data);
void try_analyze_header(request_analysis_data_t *data);
void try_analyze_next_line(request_analysis_data_t *data);
void request_analysis_data_t_destruct(request_analysis_data_t *data);
