#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include "http.h"

#define MIN_CHUNK_SIZE 64

#define istspecial(c) ((c) == '(' || (c) == ')' || (c) ==  '<' || (c) == '>' || (c) == '@' || \
                       (c) == ',' || (c) == ';' || (c) == ':' || (c) == '\\' || (c) == '"' || \
                       (c) == '/' || (c) == '[' || (c) == ']' || (c) == '?' || (c) == '=' || \
                       (c) == '{' || (c) ==  '}' || (c) == ' ' || (c) == '\t')

#define isuchar(c) (isalnum(c) || (c) == '$' || (c) == '-' || (c) == '_' || (c) == '.' || \
                    (c) == '!' || (c) == '*' || (c) == '\'' || (c) == '(' || (c) == ')' || (c) == ',')

#define isreserved(c) ((c) == ';' || (c) == '/' || (c) == '?' || (c) == ':' || (c) == '@' || \
                       (c) == '&' || (c) == '=' || (c) == '+')

#define ispchar(c) ((c) == ':' || (c) == '@' || (c) == '&' || (c) == '=' || (c) == '+' || isuchar(c))

static bool skip_token(http_state_machine_t *sm) {
    if (istspecial(sm->data.arr[sm->analyzed]) || iscntrl(sm->data.arr[sm->analyzed])) {
        sm->state = MALFORMED;
        return false;
    }
    while (!istspecial(sm->data.arr[sm->analyzed]) && !iscntrl(sm->data.arr[sm->analyzed])) {
        sm->analyzed++;
    }

    return true;
}

static bool skip_word(http_state_machine_t *sm) {
    if (sm->data.arr[sm->analyzed] == '"') {
        // TODO: Quoted words
    }
    else if (!skip_token(sm)) {
        return false;
    }

    return true;
}

static bool skip_hostname(http_state_machine_t *sm) {
    while (true) {
        if (!isalnum(sm->data.arr[sm->analyzed])) {
            sm->state = MALFORMED;
            return false;
        }
        while (isalnum(sm->data.arr[sm->analyzed]) || sm->data.arr[sm->analyzed] == '-') {
            sm->analyzed++;
        }

        if (sm->data.arr[sm->analyzed] == '-') {
            sm->state = MALFORMED;
            return false;
        }

        if (sm->data.arr[sm->analyzed] != '.') {
            break;
        }

        sm->analyzed++;
    }

    return true;
}

static bool skip_number(http_state_machine_t *sm) {
    if (!isdigit(sm->data.arr[sm->analyzed])) {
        sm->state = MALFORMED;
        return false;
    }
    while (isdigit(sm->data.arr[sm->analyzed])) {
        sm->analyzed++;
    }

    return true;
}

static bool skip_abs_path(http_state_machine_t *sm) {
    // Slash at the beginning
    if (sm->data.arr[sm->analyzed] != '/') {
        sm->state = MALFORMED;
        return false;
    }
    sm->analyzed++;

    // Path
    if (ispchar(sm->data.arr[sm->analyzed])) {
        while (ispchar(sm->data.arr[sm->analyzed]) || sm->data.arr[sm->analyzed] == '/') {
            sm->analyzed++;
        }
    }

    // Params
    if (sm->data.arr[sm->analyzed] == ';') {
        sm->analyzed++;
        while (ispchar(sm->data.arr[sm->analyzed]) || sm->data.arr[sm->analyzed] == '/' || sm->data.arr[sm->analyzed] == ';') {
            sm->analyzed++;
        }
    }

    // Query
    if (sm->data.arr[sm->analyzed] == '?') {
        sm->analyzed++;
        while (isuchar(sm->data.arr[sm->analyzed]) || isreserved(sm->data.arr[sm->analyzed])) {
            sm->analyzed++;
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

static bool analyze_method(http_state_machine_t *sm) {
    // Checking method
    if (linencmp(sm->data.arr + sm->analyzed, "GET ", 4)) {
        sm->analyzed += 3;
        sm->method = GET;
    }
    else if (linencmp(sm->data.arr + sm->analyzed, "POST ", 5)) {
        sm->analyzed += 4;
        sm->method = POST;
    }
    else if (linencmp(sm->data.arr + sm->analyzed, "HEAD ", 5)) {
        sm->analyzed += 4;
        sm->method = HEAD;
    }
    else {
        if (!skip_word(sm)) {
            return false;
        }
        sm->method = UNKNOWN_METHOD;
    }

    return true;
}

static bool analyze_uri(http_state_machine_t *sm,
                        size_t *hostname_off, size_t *hostname_size,
                        size_t *port_off, size_t *port_size,
                        size_t *path_off, size_t *path_size) {
    *port_size = 0;
    *path_size = 0;

    // Parsing URI into hostname and port
    if (!linencmp(sm->data.arr + sm->analyzed, "http://", 7)) {
        sm->state = MALFORMED;
        return false;
    }
    sm->analyzed += 7;

    *hostname_off = sm->analyzed;
    if (!skip_hostname(sm)) {
        return false;
    }
    *hostname_size = sm->analyzed - *hostname_off;

    if (sm->data.arr[sm->analyzed] == ':') {
        sm->analyzed++;
        *port_off = sm->analyzed;
        if (!skip_number(sm)) {
            return false;
        }
        *port_size = sm->analyzed - *port_off;
    }

    if (sm->data.arr[sm->analyzed] == '/') {
        *path_off = sm->analyzed;
        if (!skip_abs_path(sm)) {
                return false;
        }
        *path_size = sm->analyzed - *path_off;
    }

    return true;
}

static bool analyze_version(http_state_machine_t *sm) {
    // Checking HTTP version
    if (linencmp(sm->data.arr + sm->analyzed, "HTTP/1.0", 8)) {
        sm->analyzed += 8;
        sm->version = HTTP_1_0;
    }
    else if (linencmp(sm->data.arr + sm->analyzed, "HTTP/1.1", 8)) {
        sm->analyzed += 8;
        sm->version = HTTP_1_1;
    }
    else {
        sm->state = MALFORMED;
        return false;
    }

    return true;
}

static bool skip_spaces(http_state_machine_t *sm) {
    if (sm->data.arr[sm->analyzed] != ' ' && sm->data.arr[sm->analyzed] != '\t') {
        sm->state = MALFORMED;
        return false;
    }
    while (sm->data.arr[sm->analyzed] == ' ' || sm->data.arr[sm->analyzed] == '\t') {
        sm->analyzed++;
    }

    return true;
}

static bool skip_eol(http_state_machine_t *sm) {
    if (sm->data.arr[sm->analyzed] == '\n') {
        sm->analyzed++;
        return true;
    }

    if (sm->data.arr[sm->analyzed] != '\r') {
        sm->state = MALFORMED;
        return false;
    }
    sm->analyzed++;

    if (sm->data.arr[sm->analyzed] != '\n') {
        sm->state = MALFORMED;
        return false;
    }
    sm->analyzed++;

    return true;
}

static bool skip_text(http_state_machine_t *sm) {
    while (!iscntrl(sm->data.arr[sm->analyzed])) {
        sm->analyzed++;
    }
    return true;
}

static void analyze_status_line(http_state_machine_t *sm) {
    if (!analyze_version(sm)) {
        return;
    }

    if (!skip_spaces(sm)) {
        return;
    }

    char *end;
    sm->status = (unsigned int) strtoul(sm->data.arr + sm->analyzed, &end, 10);
    if (end == (sm->data.arr + sm->analyzed)) {
        sm->state = MALFORMED;
        return;
    }

    sm->analyzed += (size_t) (end - (sm->data.arr + sm->analyzed));

    if (!skip_spaces(sm)) {
        return;
    }

    if (!skip_text(sm)) {
        return;
    }

    if (sm->data.arr[sm->analyzed] == ' ' || sm->data.arr[sm->analyzed] == '\t') {
        skip_spaces(sm);
    }

    if (!skip_eol(sm)) {
        return;
    }

    sm->state = READ_STATUS_LINE;
    sm->available_lines--;
}

static void analyze_req_line(http_state_machine_t *sm) {
    if (!analyze_method(sm)) {
        return;
    }

    if (!skip_spaces(sm)) {
        return;
    }

    size_t hostname_off, hostname_size;
    size_t port_off, port_size;
    size_t path_off, path_size;
    if (!analyze_uri(sm,
                     &hostname_off, &hostname_size,
                     &port_off, &port_size,
                     &path_off, &path_size)) {
        return;
    }

    if (!skip_spaces(sm)) {
        return;
    }

    if (!analyze_version(sm)) {
        return;
    }

    if (sm->data.arr[sm->analyzed] == ' ' || sm->data.arr[sm->analyzed] == '\t') {
        skip_spaces(sm);
    }

    if (!skip_eol(sm)) {
        return;
    }

    sm->uri.buffer = malloc(hostname_size + 1 + port_size + 1 + path_size + 1);

    sm->uri.hostname = sm->uri.buffer;
    memcpy(sm->uri.buffer, sm->data.arr + hostname_off, hostname_size);
    sm->uri.buffer[hostname_size] = '\0';

    sm->uri.port = sm->uri.buffer + hostname_size + 1;
    memcpy(sm->uri.buffer + hostname_size + 1, sm->data.arr + port_off, port_size);
    sm->uri.buffer[hostname_size + 1 + port_size] = '\0';

    sm->uri.path = sm->uri.buffer + hostname_size + 1 + port_size + 1;
    memcpy(sm->uri.buffer + hostname_size + 1 + port_size + 1, sm->data.arr + path_off, path_size);
    sm->uri.buffer[hostname_size + 1 + port_size + 1 + path_size] = '\0';

    sm->state = READ_REQUEST_LINE;
    sm->available_lines--;
}

static void analyze_header(http_state_machine_t *sm) {
    if (iscntrl(sm->data.arr[sm->analyzed])) {
        // If there is incomplete header - it's now complete, and can be accessed
        if (sm->state == READING_HEADER) {
            sm->state = HEADER_AVAILABLE;
            return;
        }

        if (!skip_eol(sm)) {
            return;
        }
        sm->state = COMPLETE;
    }
    else if (sm->data.arr[sm->analyzed] == ' ' || sm->data.arr[sm->analyzed] == '\t') {
        // If there is no incomplete header
        if (sm->state == READ_REQUEST_LINE || sm->state == HEADER_AVAILABLE) {
            sm->state = MALFORMED;
            return;
        }
        
        if (!skip_text(sm)) {
            return;
        }

        sm->last_header.value_size = sm->analyzed - sm->last_header.value_off;

        if (!skip_eol(sm)) {
            return;
        }
    }
    else {
        // If there is incomplete header - it's now complete, and can be accessed
        if (sm->state == READING_HEADER) {
            sm->state = HEADER_AVAILABLE;
            return;
        }

        sm->last_header.name_off = sm->analyzed;

        if (!skip_token(sm)) {
            return;
        }

        if (sm->data.arr[sm->analyzed] != ':') {
            sm->state = MALFORMED;
            return;
        }

        sm->last_header.name_size = sm->analyzed - sm->last_header.name_off;
        sm->analyzed++;

        sm->last_header.value_off = sm->analyzed;

        if (!skip_text(sm)) {
            return;
        }

        sm->last_header.value_size = sm->analyzed - sm->last_header.value_off;

        if (!skip_eol(sm)) {
            return;
        }
        
        sm->state = READING_HEADER;
    }

    sm->available_lines--;
}

void http_state_machine_alloc(http_state_machine_t *sm, void **buffer, size_t *size) {
    if (sm->discarding) {
        size_t discard_size = sm->analyzed;
        if (sm->state == READING_HEADER) {
            discard_size = sm->last_header.name_off;
        }
        // Discard unneeded data
        memmove(sm->data.arr, sm->data.arr + discard_size, sm->data.size - discard_size);
        sm->data.size -= discard_size;
        sm->analyzed -= discard_size;
        sm->last_header.name_off -= discard_size;
        sm->last_header.value_off -= discard_size;
    }
    vector_char_t_reserve(&sm->data, sm->data.size + MIN_CHUNK_SIZE);
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
        case READING_STATUS_LINE:
            analyze_status_line(sm);
            break;
        case READ_STATUS_LINE:
        case READ_REQUEST_LINE:
        case READING_HEADER:
        case HEADER_AVAILABLE:
            analyze_header(sm);
            break;
        case MALFORMED:
        case COMPLETE:
            break;
    }
}

bool http_state_machine_step(http_state_machine_t *sm) {
    if (sm->available_lines == 0 || sm->state == MALFORMED || sm->state == COMPLETE) {
        return false;
    }
    
    http_state_machine_analyze_next_line(sm);

    return true;
}

void http_state_machine_get_header_name(http_state_machine_t *sm, header_t header, char **buff, size_t *size) {
    *buff = sm->data.arr + header.name_off;
    *size = header.name_size;
}

void http_state_machine_get_header_value(http_state_machine_t *sm, header_t header, char **buff, size_t *size) {
    *buff = sm->data.arr + header.value_off;
    *size = header.value_size;
}

void http_state_machine_destruct(http_state_machine_t *sm) {
    if (sm->uri.buffer != NULL) {
        free(sm->uri.buffer);
    }
    vector_char_t_destruct(&sm->data);
}
