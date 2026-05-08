#include <stdio.h>
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

void free_callback(int err, void *udata) {
    free(udata);
}

void generate_request(char **buffer, size_t *size, uri_t uri) {
    size_t hostname_len = strlen(uri.hostname);
    size_t port_len = strlen(uri.port);
    size_t path_len = strlen(uri.path);
    *size = 71 + hostname_len * 2 + port_len + path_len + 1;
    *buffer = malloc(*size);

    sprintf(*buffer, "GET http://%s:%s%s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\nContent-Lenght: 0\r\n\r\n", uri.hostname, uri.port, uri.path, uri.hostname);
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
    while (i < a_size && i < b_size && a[i] == b[i]) {
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
