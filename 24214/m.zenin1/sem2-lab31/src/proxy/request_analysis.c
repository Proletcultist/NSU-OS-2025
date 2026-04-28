#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "proxy/request_analysis.h"

#define istspecial(c) ((c) == '(' || (c) == ')' || (c) ==  '<' || (c) == '>' || (c) == '@' || \
                       (c) == ',' || (c) == ';' || (c) == ':' || (c) == '\\' || (c) == '"' || \
                       (c) == '/' || (c) == '[' || (c) == ']' || (c) == '?' || (c) == '=' || \
                       (c) == '{' || (c) ==  '}' || (c) == ' ' || (c) == '\t')

// strncmp but for http lines
// \0, \r\n and \n are considered as line ends
static bool linencmp(char *s1, char *s2, size_t n) {
    while (n --> 0) {
        if ((*s1 == '\0' || (*s1 == '\r' && *(s1 + 1) == '\n') || *s1 == '\n') &&
            (*s2 == '\0' || (*s2 == '\r' && *(s2 + 1) == '\n') || *s2 == '\n')) {
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

void try_analyze_req_line(request_analysis_data_t *data) {
    char *cursor = data->data.arr + data->analyzed;

    // Checking method
    if (linencmp(cursor, "GET ", 4)) {
        cursor += 3;
        data->method = GET;
    }
    else if (linencmp(cursor, "POST ", 5)) {
        cursor += 4;
        data->method = POST;
    }
    else if (linencmp(cursor, "HEAD ", 5)) {
        cursor += 4;
        data->method = HEAD;
    }
    else {
        if (istspecial(*cursor) || *cursor == '\n' || *cursor == '\r') {
            data->state = MALFORMED;
            return;
        }
        size_t space_dist = strcspn(cursor, " \r\n");
        cursor += space_dist;
        if (*cursor != ' ') {
            data->state = MALFORMED;
            return;
        }
        data->method = UNKNOWN_METHOD;
    }

    cursor += strspn(cursor, " ");
    if (*cursor == '\r' || *cursor == '\n') {
        data->state = MALFORMED;
        return;
    }

    // Parsing URI into hostname and port
    if (!linencmp(cursor, "http://", 7)) {
        data->state = MALFORMED;
        return;
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
        data->state = MALFORMED;
        return;
    }
    path = cursor;
    path_size = strcspn(cursor, " \n");
    cursor += path_size;
    cursor += strspn(cursor, " ");

    if (*cursor == '\r' || *cursor == '\n') {
        data->state = MALFORMED;
        return;
    }

    // Checking HTTP version
    if (linencmp(cursor, "HTTP/1.0", 8)) {
        cursor += 8;
        data->version = HTTP_1_0;
    }
    else if (linencmp(cursor, "HTTP/1.1", 8)) {
        cursor += 8;
        data->version = HTTP_1_1;
    }
    else {
        data->state = MALFORMED;
        return;
    }

    if (*cursor != ' ' && !(*cursor == '\r' && *(cursor + 1) == '\n') && *cursor != '\n') {
        data->state = MALFORMED;
        return;
    }

    cursor += strspn(cursor, " ");
    if (!(*cursor == '\r' && *(cursor + 1) == '\n') && *cursor != '\n') {
        data->state = MALFORMED;
        return;
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

    data->state = READ_REQUEST_LINE;
}

static void try_get_header(char **cursor, header_t *out) {
}

void try_analyze_header(request_analysis_data_t *data) {
    char *cursor = data->data.arr + data->analyzed;
    if ((*cursor == '\r' && *(cursor + 1) == '\n') || *cursor == '\n') {
        data->state = MALFORMED;
        return;
    }
    
    size_t header_name_size = strcspn(cursor, ":\r");
}

void try_analyze_next_line(request_analysis_data_t *data) {
    switch (data->state) {
        case READING_REQUEST_LINE:
            try_analyze_req_line(data);
            break;
        case READ_REQUEST_LINE:
        case READING_HEADERS:
        case HEADER_AVAILABLE:
            try_analyze_header(data);
            break;
        case MALFORMED:
        case COMPLETE:
            break;
    }
}

void request_analysis_data_t_destruct(request_analysis_data_t *data) {
    if (data->uri.buffer != NULL) {
        free(data->uri.buffer);
    }
    vector_char_t_destruct(&data->data);
}
