#include "proxy.h"

static char *trim_dup(const char *s, size_t n) {
    const char *beg = s;
    const char *end = s + n;

    while (beg < end && isspace((unsigned char)*beg)) beg++;
    while (end > beg && isspace((unsigned char)end[-1])) end--;
    return dup_range(beg, (size_t)(end - beg));
}

static char *find_host_header(const char *req) {
    const char *p = strstr(req, "\r\n");
    if (!p) return NULL;
    p += 2;

    while (*p) {
        const char *e = strstr(p, "\r\n");
        const char *colon;
        size_t name_len;

        if (!e) break;
        if (e == p) break;

        colon = memchr(p, ':', (size_t)(e - p));
        if (colon) {
            name_len = (size_t)(colon - p);
            if (name_len == 4 && strncasecmp(p, "Host", 4) == 0) {
                return trim_dup(colon + 1, (size_t)(e - (colon + 1)));
            }
        }
        p = e + 2;
    }

    return NULL;
}

static int parse_authority(const char *authority, char **host_out, int *port_out) {
    const char *colon;

    *port_out = 80;

    if (authority[0] == '[') {
        const char *rb = strchr(authority, ']');
        if (!rb) return -1;
        *host_out = dup_range(authority + 1, (size_t)(rb - authority - 1));
        if (rb[1] == ':') {
            int p = atoi(rb + 2);
            if (p <= 0 || p > 65535) return -1;
            *port_out = p;
        } else if (rb[1] != '\0') {
            return -1;
        }
        return 0;
    }

    colon = strrchr(authority, ':');
    if (colon && strchr(colon + 1, ':') == NULL && strchr(authority, ':') == colon) {
        int p = atoi(colon + 1);
        if (p <= 0 || p > 65535) return -1;
        *host_out = dup_range(authority, (size_t)(colon - authority));
        *port_out = p;
    } else {
        *host_out = xstrdup(authority);
    }

    if ((*host_out)[0] == '\0') return -1;
    return 0;
}

static int parse_absolute_http_uri(const char *uri, char **host_out, int *port_out, char **path_out) {
    const char *p;
    const char *slash;
    char *authority;

    if (strncasecmp(uri, "http://", 7) != 0) return -1;

    p = uri + 7;
    slash = strchr(p, '/');

    if (slash) {
        authority = dup_range(p, (size_t)(slash - p));
        *path_out = xstrdup(slash);
    } else {
        authority = xstrdup(p);
        *path_out = xstrdup("/");
    }

    if (parse_authority(authority, host_out, port_out) != 0) {
        free(authority);
        free(*path_out);
        *path_out = NULL;
        return -1;
    }

    free(authority);
    return 0;
}

int parse_request(const char *buf, size_t len, ParsedRequest *r, int *http_err) {
    char *req;
    char *line_end;
    char *sp1;
    char *sp2;

    memset(r, 0, sizeof(*r));
    *http_err = 400;

    req = dup_range(buf, len);
    line_end = strstr(req, "\r\n");
    if (!line_end) {
        free(req);
        return -1;
    }
    *line_end = '\0';

    sp1 = strchr(req, ' ');
    if (!sp1) {
        free(req);
        return -1;
    }
    *sp1++ = '\0';
    while (*sp1 == ' ') sp1++;

    sp2 = strchr(sp1, ' ');
    if (!sp2) {
        free(req);
        return -1;
    }
    *sp2++ = '\0';
    while (*sp2 == ' ') sp2++;

    r->method = xstrdup(req);
    r->version = xstrdup(sp2);

    if (strcmp(r->method, "GET") != 0) {
        *http_err = 501;
        free(req);
        return -1;
    }

    if (strcmp(r->version, "HTTP/1.0") != 0 && strcmp(r->version, "HTTP/1.1") != 0) {
        *http_err = 505;
        free(req);
        return -1;
    }

    if (strncasecmp(sp1, "http://", 7) == 0) {
        if (parse_absolute_http_uri(sp1, &r->host, &r->port, &r->path) != 0) {
            free(req);
            return -1;
        }
    } else if (sp1[0] == '/') {
        char *host_header = find_host_header(buf);
        if (!host_header) {
            free(req);
            return -1;
        }
        if (parse_authority(host_header, &r->host, &r->port) != 0) {
            free(host_header);
            free(req);
            return -1;
        }
        free(host_header);
        r->path = xstrdup(sp1);
    } else {
        free(req);
        return -1;
    }

    r->key = build_cache_key(r->host, r->port, r->path);
    free(req);
    return 0;
}

void queue_simple_response(Client *c, int code, const char *reason, const char *body) {
    char header[1024];
    size_t body_len = strlen(body);
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.0 %d %s\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "\r\n"
                     "%s",
                     code, reason, body_len, body);
    if (n < 0) return;
    append_bytes(&c->outbuf, &c->out_len, &c->out_cap, header, (size_t)n);
    c->out_sent = 0;
}
