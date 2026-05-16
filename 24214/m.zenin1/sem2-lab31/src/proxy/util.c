#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <sys/types.h>
#include "scheduler/task.h"
#include "scheduler/aio_scheduler.h"
#include "proxy/util.h"
#include "proxy/client.h"
#include "http.h"

void panic(char *msg) {
    fprintf(stderr, "[Panic!!!1] %s\n", msg);
    exit(-1);
}

void free_callback(int err, void *udata) {
    free(udata);
}

size_t parse_size_t_trimmed(char *str, size_t size, bool *succ) {
    while (size != 0 && (*str == ' ' || *str == '\t')) {
        str++;
        size--;
    }
    if (size == 0 || !isdigit(*str)){
        *succ = false;
        return 0;
    }

    size_t ret = 0;
    while (size != 0 && isdigit(*str)) {
        size_t prev = ret;
        ret *= 10;
        ret += (*str) - '0';
        if (prev > ret) {
            *succ = false;
            return 0;
        }

        str++;
        size--;
    }

    while (size != 0 && (*str == ' ' || *str == '\t')) {
        str++;
        size--;
    }

    if (size != 0) {
        *succ = false;
        return 0;
    }

    *succ = true;
    return ret;
}

void generate_request(char **buffer, size_t *size, uri_t uri) {
    size_t hostname_len = strlen(uri.hostname);
    size_t port_len = strlen(uri.port);
    size_t path_len = strlen(uri.path);
    *size = 71 + hostname_len * 2 + port_len + path_len + 1;
    *buffer = malloc(*size);

    if (*buffer != NULL) {
        sprintf(*buffer, "GET http://%s:%s%s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\nContent-Lenght: 0\r\n\r\n", uri.hostname, uri.port, uri.path, uri.hostname);
    }
}

bool ci_memcmp(char *s1, char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (tolower(s1[i]) != tolower(s2[i])) {
            return false;
        }
    }
    return true;
}

bool mem_compare_trimed(char *a, size_t a_size, char *b, size_t b_size) {
    while (a_size > 0 && (*a == ' ' || *a == '\t')) {
        a++;
        a_size--;
    }
    while (b_size > 0 && (*b == ' ' || *b == '\t')) {
        b++;
        b_size--;
    }

    size_t i = 0;
    while (i < a_size && i < b_size && tolower(a[i]) == tolower(b[i])) {
        i++;
    }

    for (size_t j = i; j < a_size; j++) {
        if (a[j] != ' ' && a[j] != '\t') {
            return false;
        }
    }
    for (size_t j = i; j < b_size; j++) {
        if (b[j] != ' ' && b[j] != '\t') {
            return false;
        }
    }

    return true;
}

struct addrinfo* resolve_address(char *hostname, char *port) {
    struct addrinfo *res;

    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM
    };
    int err = getaddrinfo(hostname, port, &hints, &res);
    if (err) {
        fprintf(stderr, "[Error] Failed to resolve %s: %s\n", hostname, gai_strerror(err));
        return NULL;
    }

    return res;
}
