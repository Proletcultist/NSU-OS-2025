#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "http.h"

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

void analyze_req_line(http_state_machine_t *sm) {
    char *cursor = sm->data.arr + sm->analyzed;

    // Checking method
    if (linencmp(cursor, "GET ", 4)) {
        cursor += 3;
        sm->method = GET;
    }
    else if (linencmp(cursor, "POST ", 5)) {
        cursor += 4;
        sm->method = POST;
    }
    else if (linencmp(cursor, "HEAD ", 5)) {
        cursor += 4;
        sm->method = HEAD;
    }
    else {
        if (istspecial(*cursor) || *cursor == '\n' || *cursor == '\r') {
            sm->state = MALFORMED;
            return;
        }
        size_t space_dist = strcspn(cursor, " \r\n");
        cursor += space_dist;
        if (*cursor != ' ') {
            sm->state = MALFORMED;
            return;
        }
        sm->method = UNKNOWN_METHOD;
    }

    cursor += strspn(cursor, " ");
    if (*cursor == '\r' || *cursor == '\n') {
        sm->state = MALFORMED;
        return;
    }

    // Parsing URI into hostname and port
    if (!linencmp(cursor, "http://", 7)) {
        sm->state = MALFORMED;
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
        if (*cursor != ' ' && *cursor != '/') {
            sm->state = MALFORMED;
            return;
        }
    }

    if (*cursor == '\r' || *cursor == '\n') {
        sm->state = MALFORMED;
        return;
    }
    path = cursor;
    path_size = strcspn(cursor, " \n");
    cursor += path_size;
    cursor += strspn(cursor, " ");

    if (*cursor == '\r' || *cursor == '\n') {
        sm->state = MALFORMED;
        return;
    }

    // Checking HTTP version
    if (linencmp(cursor, "HTTP/1.0", 8)) {
        cursor += 8;
        sm->version = HTTP_1_0;
    }
    else if (linencmp(cursor, "HTTP/1.1", 8)) {
        cursor += 8;
        sm->version = HTTP_1_1;
    }
    else {
        sm->state = MALFORMED;
        return;
    }

    if (*cursor != ' ' && !(*cursor == '\r' && *(cursor + 1) == '\n') && *cursor != '\n') {
        sm->state = MALFORMED;
        return;
    }

    cursor += strspn(cursor, " ");
    if (!(*cursor == '\r' && *(cursor + 1) == '\n') && *cursor != '\n') {
        sm->state = MALFORMED;
        return;
    }
    
    // Placing cursor right on the LF
    if (*cursor == '\r') {
        cursor++;
    }
    sm->analyzed = cursor -(sm->data.arr + sm->analyzed);

    sm->uri.buffer = malloc(hostname_size + 1 + port_size + 1 + path_size + 1);

    sm->uri.hostname = sm->uri.buffer;
    memcpy(sm->uri.buffer, hostname, hostname_size);
    sm->uri.buffer[hostname_size] = '\0';

    sm->uri.port = sm->uri.buffer + hostname_size + 1;
    memcpy(sm->uri.buffer + hostname_size + 1, port, port_size);
    sm->uri.buffer[hostname_size + 1 + port_size] = '\0';

    sm->uri.path = sm->uri.buffer + hostname_size + 1 + port_size + 1;
    memcpy(sm->uri.buffer + hostname_size + 1 + port_size + 1, path, path_size);
    sm->uri.buffer[hostname_size + 1 + port_size + 1 + path_size] = '\0';

    sm->state = READ_REQUEST_LINE;
}

void analyze_header(http_state_machine_t *sm) {
}

void http_state_machine_analyze_next_line(http_state_machine_t *sm) {
    switch (sm->state) {
        case READING_REQUEST_LINE:
            analyze_req_line(sm);
            break;
        case READ_REQUEST_LINE:
        case READING_HEADERS:
        case HEADER_AVAILABLE:
            analyze_header(sm);
            break;
        case MALFORMED:
        case COMPLETE:
            break;
    }
}

void http_state_machine_destruct(http_state_machine_t *sm) {
    if (sm->uri.buffer != NULL) {
        free(sm->uri.buffer);
    }
    vector_char_t_destruct(&sm->data);
}
