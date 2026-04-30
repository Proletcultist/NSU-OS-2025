#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include "http.h"

#define MIN_CHUNK_SIZE 128

#define istspecial(c) ((c) == '(' || (c) == ')' || (c) ==  '<' || (c) == '>' || (c) == '@' || \
                       (c) == ',' || (c) == ';' || (c) == ':' || (c) == '\\' || (c) == '"' || \
                       (c) == '/' || (c) == '[' || (c) == ']' || (c) == '?' || (c) == '=' || \
                       (c) == '{' || (c) ==  '}' || (c) == ' ' || (c) == '\t')

// TODO: May be also handle escape (% HEX HEX)
#define isuchar(c) (isalnum(c) || (c) == '$' || (c) == '-' || (c) == '_' || (c) == '.' || \
                    (c) == '!' || (c) == '*' || (c) == '\'' || (c) == '(' || (c) == ')' || (c) == ',')

#define isreserved(c) ((c) == ';' || (c) == '/' || (c) == '?' || (c) == ':' || (c) == '@' || \
                       (c) == '&' || (c) == '=' || (c) == '+')

#define ispchar(c) ((c) == ':' || (c) == '@' || (c) == '&' || (c) == '=' || (c) == '+' || isuchar(c))

static bool skip_word(char **cursor, http_state_machine_t *sm) {
    if (**cursor == '"') {
        // TODO: Quoted words
    }
    else {
        if (istspecial(**cursor) || iscntrl(**cursor)) {
            sm->state = MALFORMED;
            return false;
        }
        while (!istspecial(**cursor) && !iscntrl(**cursor)) {
            (*cursor)++;
        }
    }

    return true;
}

static bool skip_hostname(char **cursor, http_state_machine_t *sm) {
    while (true) {
        if (!isalnum(**cursor)) {
            sm->state = MALFORMED;
            return false;
        }
        while (isalnum(**cursor) || **cursor == '-') {
            (*cursor)++;
        }

        if (**cursor == '-') {
            sm->state = MALFORMED;
            return false;
        }

        if (**cursor != '.') {
            break;
        }

        (*cursor)++;
    }

    return true;
}

static bool skip_number(char **cursor, http_state_machine_t *sm) {
    if (!isdigit(**cursor)) {
        sm->state = MALFORMED;
        return false;
    }
    while (isdigit(**cursor)) {
        (*cursor)++;
    }

    return true;
}

static bool skip_abs_path(char **cursor, http_state_machine_t *sm) {
    // Slash at the beginning
    if (**cursor != '/') {
        sm->state = MALFORMED;
        return false;
    }
    (*cursor)++;

    // Path
    if (ispchar(**cursor)) {
        while (ispchar(**cursor) || **cursor == '/') {
            (*cursor)++;
        }
    }

    // Params
    if (**cursor == ';') {
        (*cursor)++;
        while (ispchar(**cursor) || **cursor == '/' || **cursor == ';') {
            (*cursor)++;
        }
    }

    // Query
    if (**cursor == '?') {
        (*cursor)++;
        while (isuchar(**cursor) || isreserved(**cursor)) {
            (*cursor)++;
        }
    }

    return true;
}

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

static bool analyze_method(char **cursor, http_state_machine_t *sm) {
    // Checking method
    if (linencmp(*cursor, "GET ", 4)) {
        (*cursor) += 3;
        sm->method = GET;
    }
    else if (linencmp(*cursor, "POST ", 5)) {
        (*cursor) += 4;
        sm->method = POST;
    }
    else if (linencmp(*cursor, "HEAD ", 5)) {
        (*cursor) += 4;
        sm->method = HEAD;
    }
    else {
        if (!skip_word(cursor, sm)) {
            return false;
        }
        sm->method = UNKNOWN_METHOD;
    }

    return true;
}

static bool analyze_uri(char **cursor, http_state_machine_t *sm,
                        char **hostname, size_t *hostname_size,
                        char **port, size_t *port_size,
                        char **path, size_t *path_size) {
    *port_size = 0;
    *path_size = 0;

    // Parsing URI into hostname and port
    if (!linencmp(*cursor, "http://", 7)) {
        sm->state = MALFORMED;
        return false;
    }
    (*cursor) += 7;

    *hostname = *cursor;
    if (!skip_hostname(cursor, sm)) {
        return false;
    }
    *hostname_size = (size_t) (*cursor - *hostname);

    if (**cursor == ':') {
        (*cursor)++;
        *port = *cursor;
        if (!skip_number(cursor, sm)) {
            return false;
        }
        *port_size = (size_t) (*cursor - *port);
    }

    if (**cursor == '/') {
        *path = *cursor;
        if (!skip_abs_path(cursor, sm)) {
                return false;
        }
        *path_size = (size_t) (*cursor - *path);
    }

    return true;
}

static bool analyze_version(char **cursor, http_state_machine_t *sm) {
    // Checking HTTP version
    if (linencmp(*cursor, "HTTP/1.0", 8)) {
        (*cursor) += 8;
        sm->version = HTTP_1_0;
    }
    else if (linencmp(*cursor, "HTTP/1.1", 8)) {
        (*cursor) += 8;
        sm->version = HTTP_1_1;
    }
    else {
        sm->state = MALFORMED;
        return false;
    }

    return true;
}

static bool skip_spaces(char **cursor, http_state_machine_t *sm) {
    if (**cursor != ' ') {
        sm->state = MALFORMED;
        return false;
    }
    while (**cursor == ' ') {
        (*cursor)++;
    }

    return true;
}

static bool skip_eol(char **cursor, http_state_machine_t *sm) {
    if (**cursor == '\n') {
        (*cursor)++;
        return true;
    }

    if (**cursor != '\r') {
        sm->state = MALFORMED;
        return false;
    }
    (*cursor)++;

    if (**cursor != '\n') {
        sm->state = MALFORMED;
        return false;
    }
    (*cursor)++;

    return true;
}

void analyze_req_line(http_state_machine_t *sm) {
    char *cursor = sm->data.arr + sm->analyzed;

    if (!analyze_method(&cursor, sm)) {
        return;
    }

    if (!skip_spaces(&cursor, sm)) {
        return;
    }

    char *hostname, *port, *path;
    size_t hostname_size, port_size, path_size;
    if (!analyze_uri(&cursor, sm,
                     &hostname, &hostname_size,
                     &port, &port_size,
                     &path, &path_size)) {
        return;
    }

    if (!skip_spaces(&cursor, sm)) {
        return;
    }

    if (!analyze_version(&cursor, sm)) {
        return;
    }

    if (!skip_spaces(&cursor, sm) && !skip_eol(&cursor, sm)) {
        return;
    }

    sm->analyzed = cursor - sm->data.arr;

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

void http_state_machine_alloc(http_state_machine_t *sm, void **buffer, size_t *size) {
    vector_char_t_reserve(&sm->data, sm->data.cap + MIN_CHUNK_SIZE);
    *buffer = sm->data.arr + sm->data.size;
    *size = sm->data.cap - sm->data.size;
}

void http_state_machine_feed(http_state_machine_t *sm, size_t size) {
    char *eol = memchr(sm->data.arr + sm->data.size, '\n', size);

    while (eol != NULL) {
        sm->available_lines++;
        eol = memchr(eol + 1, '\n', size - (size_t) (eol - (sm->data.arr + sm->data.size)) - 1);
    }
    sm->data.size += size;
}

static void http_state_machine_analyze_next_line(http_state_machine_t *sm) {
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

bool http_state_machine_step(http_state_machine_t *sm) {
    if (sm->available_lines == 0) {
        return false;
    }
    
    http_state_machine_analyze_next_line(sm);

    sm->available_lines--;
}

void http_state_machine_destruct(http_state_machine_t *sm) {
    if (sm->uri.buffer != NULL) {
        free(sm->uri.buffer);
    }
    vector_char_t_destruct(&sm->data);
}
