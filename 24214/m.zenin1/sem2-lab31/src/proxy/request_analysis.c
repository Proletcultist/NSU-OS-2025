#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "proxy/request_analysis.h"

// strncmp but for http lines
// \0 and \r\n are considered as line ends
static bool linencmp(char *s1, char *s2, size_t n) {
    while (n --> 0) {
        if ((*s1 == '\0' || (*s1 == '\r' && *(s1 + 1) == '\n') || *s1 == '\n') && (*s2 == '\0' || (*s2 == '\r' && *(s2 + 1) == '\n') || *s2 == '\n')) {
            break;
        }
        if (*s1 != *s2) {
            return false;
        }
        s1++;
        s2++;
    }
    
    return true;
}

request_analysis_result_t try_analyze_req_line(request_analysis_data_t *data) {
    char *cursor = data->data.arr + data->analyzed;

    // Checking method
    if (linencmp(cursor, "GET ", 4)) {
        cursor += 3;
        data->cacheable = true;
    }
    else if (linencmp(cursor, "POST ", 5)) {
        cursor += 4;
        data->cacheable = false;
    }
    else if (linencmp(cursor, "HEAD ", 5)) {
        cursor += 4;
        data->cacheable = false;
    }
    else if (*(cursor + strcspn(cursor, " \n")) == ' ') {
        return METHOD_NOT_IMPLEMENTED;
    }
    else {
        return MALFORMED;
    }

    cursor += strspn(cursor, " ");
    if (*cursor == '\r' || *cursor == '\n') {
        return MALFORMED;
    }

    // Parsing URI into hostname and port
    if (!linencmp(cursor, "http://", 7)) {
        return MALFORMED;
    }
    cursor += 7;

    char *hostname = cursor;
    size_t hostname_size = strcspn(cursor, ":/ \n");
    char *port = NULL;
    char *path = NULL;
    size_t port_size = 0, path_size = 0;

    cursor += hostname_size;
    if (*cursor == ':') {
        cursor++;
        port = cursor;
        port_size = strspn(cursor, "0123456789");
        cursor += port_size;
    }

    if (*cursor == '\r' || *cursor == '\n') {
        return MALFORMED;
    }
    path = cursor;
    path_size = strcspn(cursor, " \n");
    cursor += path_size;
    cursor += strspn(cursor, " ");

    if (*cursor == '\r' || *cursor == '\n') {
        return MALFORMED;
    }

    // Checking HTTP version
    if (linencmp(cursor, "HTTP/1.0", 8)) {
        cursor += 8;
    }
    else if (linencmp(cursor, "HTTP/1.1", 8)) {
        return VERSION_NOT_SUPPORTED;
    }
    else {
        return MALFORMED;
    }

    if (*cursor != ' ' && !(*cursor == '\r' && *(cursor + 1) == '\n') && *cursor != '\n') {
        return MALFORMED;
    }

    cursor += strspn(cursor, " ");
    if (!(*cursor == '\r' && *(cursor + 1) == '\n') && *cursor != '\n') {
        return MALFORMED;
    }
    
    // Placing cursor right on the LF
    if (*cursor == '\r') {
        cursor++;
    }
    data->analyzed = cursor -(data->data.arr + data->analyzed);

    data->uri.buffer = malloc(hostname_size + 1 + port_size + 1 + path_size + 1);

    data->uri.hostname = data->uri.buffer;
    memcpy(data->uri.buffer, hostname, hostname_size);
    data->uri.buffer[hostname_size] = '\0';

    data->uri.port = data->uri.buffer + hostname_size + 1;
    memcpy(data->uri.buffer + hostname_size + 1, port, port_size);
    data->uri.buffer[hostname_size + 1 + port_size] = '\0';

    data->uri.path = data->uri.buffer + hostname_size + 1 + port_size + 1;
    memcpy(data->uri.buffer + hostname_size + 1 + port_size + 1, path, path_size);
    data->uri.buffer[hostname_size + 1 + port_size + 1 + path_size] = '\0';

    fprintf(stderr, "[Info] Parsed hostname: \"%s\" port: \"%s\", path: \"%s\"\n", data->uri.hostname, data->uri.port, data->uri.path);

    data->state = READING_HEADERS;
    return INCOMPLETE;
}

request_analysis_result_t try_analyze_header(request_analysis_data_t *data) {
    return INCOMPLETE;
}

request_analysis_result_t try_analyze_next_line(request_analysis_data_t *data) {
    switch (data->state) {
        case READING_REQUEST_LINE:
            return try_analyze_req_line(data);
        case READING_HEADERS:
            return try_analyze_header(data);
    }
}

void request_analysis_data_t_destruct(request_analysis_data_t *data) {
    if (data->uri.buffer != NULL) {
        free(data->uri.buffer);
    }
    vector_char_t_destruct(&data->data);
}
