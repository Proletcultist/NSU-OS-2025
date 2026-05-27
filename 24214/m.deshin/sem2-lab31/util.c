#include "proxy.h"

void free_parsed_request(ParsedRequest *r) {
    free(r->method);
    free(r->host);
    free(r->path);
    free(r->version);
    free(r->key);
    memset(r, 0, sizeof(*r));
}

char *dup_range(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (!p) {
        perror("malloc");
        exit(1);
    }
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *xstrdup(const char *s) {
    return dup_range(s, strlen(s));
}

void ensure_capacity(char **buf, size_t *cap, size_t need) {
    if (*cap >= need) return;

    size_t new_cap = (*cap == 0) ? 1024 : *cap;
    while (new_cap < need) {
        if (new_cap > ((size_t)-1) / 2) {
            fprintf(stderr, "capacity overflow\n");
            exit(1);
        }
        new_cap *= 2;
    }

    char *p = (char *)realloc(*buf, new_cap);
    if (!p) {
        perror("realloc");
        exit(1);
    }
    *buf = p;
    *cap = new_cap;
}

void append_bytes(char **buf, size_t *len, size_t *cap, const void *src, size_t n) {
    ensure_capacity(buf, cap, *len + n + 1);
    memcpy(*buf + *len, src, n);
    *len += n;
    (*buf)[*len] = '\0';
}

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    return 0;
}

ssize_t find_bytes(const char *buf, size_t len, const char *needle, size_t nlen) {
    size_t i;

    if (nlen == 0 || len < nlen) return -1;
    for (i = 0; i + nlen <= len; ++i) {
        if (memcmp(buf + i, needle, nlen) == 0) return (ssize_t)i;
    }
    return -1;
}

ssize_t find_header_end(const char *buf, size_t len) {
    return find_bytes(buf, len, "\r\n\r\n", 4);
}

char *build_cache_key(const char *host, int port, const char *path) {
    char port_buf[32];
    size_t n;
    char *key;

    snprintf(port_buf, sizeof(port_buf), "%d", port);
    n = strlen(host) + 1 + strlen(port_buf) + strlen(path) + 1;
    key = (char *)malloc(n + 1);
    if (!key) {
        perror("malloc");
        exit(1);
    }
    snprintf(key, n + 1, "%s:%d%s", host, port, path);
    return key;
}
