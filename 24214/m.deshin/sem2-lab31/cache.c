#include "proxy.h"

CacheEntry *find_cache_entry(const char *key) {
    CacheEntry *e;

    for (e = g_cache; e; e = e->next) {
        if (e->dead) continue;
        if (strcmp(e->key, key) != 0) continue;
        if (e->complete && !e->cacheable) continue;

        DLOG("[CACHE HIT] existing entry: %s\n", key);

        return e;
    }

    return NULL;
}

void close_upstream(CacheEntry *e) {
    if (e->upstream_fd >= 0) {
        close(e->upstream_fd);
        e->upstream_fd = -1;
    }
}

void mark_entry_dead(CacheEntry *e) {
    if (!e->dead) {
        e->dead = 1;
        close_upstream(e);
    }
}

void maybe_reap_entry(CacheEntry *e) {
    if (e->dead) return;

    if (!e->complete && e->ref_clients == 0) {
        mark_entry_dead(e);
        return;
    }
    if (e->complete && !e->cacheable && e->ref_clients == 0) {
        mark_entry_dead(e);
    }
}

void cleanup_dead_entries(void) {
    CacheEntry **pp = &g_cache;

    while (*pp) {
        CacheEntry *e = *pp;
        if (!e->dead) {
            pp = &e->next;
            continue;
        }
        *pp = e->next;
        free(e->key);
        free(e->host);
        free(e->path);
        free(e->up_req);
        free(e->data);
        free(e);
    }
}

static int start_upstream(CacheEntry *e) {
    char port_str[16];
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *ai;

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    snprintf(port_str, sizeof(port_str), "%d", e->port);
    int rc = getaddrinfo(e->host, port_str, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo(%s): %s\n", e->host, gai_strerror(rc));
        entry_make_error(e, 502, "Bad Gateway", "DNS resolution failed\n");
        return -1;
    }

    e->upstream_fd = -1;
    for (ai = res; ai; ai = ai->ai_next) {
        int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        int rc;

        if (fd < 0) continue;
        if (set_nonblocking(fd) < 0) {
            close(fd);
            continue;
        }
        rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc == 0 || errno == EINPROGRESS) {
            e->upstream_fd = fd;
            e->connect_done = (rc == 0);
            break;
        }
        close(fd);
    }
    freeaddrinfo(res);

    if (e->upstream_fd < 0) {
        entry_make_error(e, 502, "Bad Gateway", "Cannot connect to origin server\n");
        return -1;
    }

    char req[2048];
    int n = snprintf(req, sizeof(req),
                        "GET %s HTTP/1.0\r\n"
                        "Host: %s\r\n"
                        "User-Agent: pseudo-proxy/1.0\r\n"
                        "Connection: close\r\n"
                        "Proxy-Connection: close\r\n"
                        "\r\n",
                        e->path, e->host);
    if (n < 0 || (size_t)n >= sizeof(req)) {
        entry_make_error(e, 500, "Internal Server Error", "Request build failed\n");
        return -1;
    }
    e->up_req = xstrdup(req);
    e->up_req_len = (size_t)n;
    e->up_req_sent = 0;

    return 0;
}

CacheEntry *create_entry(const ParsedRequest *r) {
    CacheEntry *e = (CacheEntry *)calloc(1, sizeof(*e));
    if (!e) {
        perror("calloc");
        exit(1);
    }

    e->key = xstrdup(r->key);
    e->host = xstrdup(r->host);
    e->path = xstrdup(r->path);
    e->port = r->port;
    e->upstream_fd = -1;
    e->next = g_cache;
    g_cache = e;

    DLOG("[CACHE MISS] create entry: %s\n", e->key);

    start_upstream(e);
    return e;
}

void attach_client_to_entry(Client *c, CacheEntry *e) {
    detach_client_from_entry(c);
    c->entry = e;
    c->send_pos = 0;
    e->ref_clients++;

    DLOG("[ATTACH] client fd=%d -> key=%s, ref_clients=%d\n",
       c->fd, e->key, e->ref_clients);
}

void entry_make_error(CacheEntry *e, int code, const char *reason, const char *body) {
    char resp[1024];
    int n = snprintf(resp, sizeof(resp),
                     "HTTP/1.0 %d %s\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "\r\n"
                     "%s",
                     code, reason, strlen(body), body);
    if (n < 0) return;

    e->data_len = 0;
    append_bytes(&e->data, &e->data_len, &e->data_cap, resp, (size_t)n);
    e->header_parsed = 1;
    e->status_code = code;
    e->cacheable = 0;
    e->complete = 1;
    close_upstream(e);
}

void parse_response_header_if_ready(CacheEntry *e) {
    ssize_t hdr;
    char *line_end;
    char *status;
    char *sp;

    if (e->header_parsed) return;

    hdr = find_header_end(e->data, e->data_len);
    if (hdr < 0) return;

    e->header_parsed = 1;
    e->header_len = (size_t)hdr + 4;

    line_end = strstr(e->data, "\r\n");
    if (!line_end) {
        e->status_code = 502;
        e->cacheable = 0;
        return;
    }

    status = dup_range(e->data, (size_t)(line_end - e->data));
    if (strncmp(status, "HTTP/", 5) != 0) {
        e->status_code = 502;
        e->cacheable = 0;
        free(status);
        return;
    }

    sp = strchr(status, ' ');
    if (!sp) {
        e->status_code = 502;
        e->cacheable = 0;
        free(status);
        return;
    }

    e->status_code = atoi(sp + 1);
    e->cacheable = (e->status_code == 200);
    free(status);
}

void handle_upstream_write(CacheEntry *e) {
    if (e->dead || e->upstream_fd < 0) return;

    if (!e->connect_done) {
        int err = 0;
        socklen_t len = sizeof(err);
        if (getsockopt(e->upstream_fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
            if (e->data_len == 0) {
                entry_make_error(e, 502, "Bad Gateway", "Connect failed\n");
            } else {
                e->complete = 1;
                close_upstream(e);
            }
            maybe_reap_entry(e);
            return;
        }
        e->connect_done = 1;
    }

    if (e->up_req_sent < e->up_req_len) {
        ssize_t n = send(e->upstream_fd,
                         e->up_req + e->up_req_sent,
                         e->up_req_len - e->up_req_sent,
                         0);
        if (n > 0) {
            e->up_req_sent += (size_t)n;
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            if (e->data_len == 0) {
                entry_make_error(e, 502, "Bad Gateway", "Cannot send request to origin\n");
            } else {
                e->complete = 1;
                close_upstream(e);
            }
            maybe_reap_entry(e);
        }
    }
}

void handle_upstream_read(CacheEntry *e) {
    char buf[READ_CHUNK];
    ssize_t n;

    if (e->dead || e->upstream_fd < 0 || !e->connect_done) return;

    n = recv(e->upstream_fd, buf, sizeof(buf), 0);
    if (n > 0) {
        append_bytes(&e->data, &e->data_len, &e->data_cap, buf, (size_t)n);

        DLOG("[UPSTREAM READ] key=%s, +%zd bytes, total=%zu\n", e->key, n, e->data_len);

        parse_response_header_if_ready(e);
        return;
    }

    if (n == 0) {
        if (e->data_len == 0) {
            entry_make_error(e, 502, "Bad Gateway", "Origin closed without response\n");
        } else {
            parse_response_header_if_ready(e);
            e->complete = 1;
            close_upstream(e);
        }
        maybe_reap_entry(e);
        return;
    }

    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        if (e->data_len == 0) {
            entry_make_error(e, 502, "Bad Gateway", "Read from origin failed\n");
        } else {
            e->complete = 1;
            close_upstream(e);
        }
        maybe_reap_entry(e);
    }
}

size_t count_upstreams(void) {
    size_t n = 0;
    CacheEntry *e;

    for (e = g_cache; e; e = e->next) {
        if (!e->dead && e->upstream_fd >= 0) n++;
    }
    return n;
}
