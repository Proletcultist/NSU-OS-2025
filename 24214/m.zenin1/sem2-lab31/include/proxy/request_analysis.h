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
                                            .uri = NULL, \
                                            .ip = INADDR_ANY, \
                                            .port = 0, \
                                            .cacheable = false, \
                                            .content_size = 0, \
                                            .analyzed = 0, \
                                            .data = (vector_char_t) VECTOR_INITIALIZER \
                                           })

typedef enum request_analysis_state {
    READING_REQUEST_LINE,
    READING_HEADERS
} request_analysis_state_t;

typedef struct request_analysis_data {
    request_analysis_state_t state;   
    char *uri;
    struct in_addr ip;
    in_port_t port;
    bool cacheable;
    size_t content_size;

    size_t analyzed;
    vector_char_t data;
} request_analysis_data_t;

typedef enum request_analyzis_result {
    INCOMPLETE, // Request + headers not ended yet
    MALFORMED,  // Bad request
    COMPLETE    // Request + headers all read, can start transfer body if there is any
} request_analyzis_result_t;

request_analyzis_result_t try_analyze_req_line(request_analysis_data_t *data);
request_analyzis_result_t try_analyze_header(request_analysis_data_t *data);
request_analyzis_result_t try_analyze(request_analysis_data_t *data);
void request_analysis_data_t_destruct(request_analysis_data_t *data);
