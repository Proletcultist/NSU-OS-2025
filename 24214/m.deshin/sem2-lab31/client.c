#include "proxy.h"

Client *add_client(int fd) {
    Client *c = (Client *)calloc(1, sizeof(*c));
    if (!c) {
        perror("calloc");
        exit(1);
    }
    c->fd = fd;
    c->next = g_clients;
    g_clients = c;
    return c;
}

void detach_client_from_entry(Client *c) {
    CacheEntry *e;

    if (!c->entry) return;
    e = c->entry;
    c->entry = NULL;
    if (e->ref_clients > 0) e->ref_clients--;
    maybe_reap_entry(e);
}

void remove_client(Client *c) {
    DLOG("[CLIENT CLOSE] fd=%d\n", c->fd);

    Client **pp = &g_clients;

    while (*pp && *pp != c) pp = &(*pp)->next;
    if (*pp == c) *pp = c->next;

    detach_client_from_entry(c);
    if (c->fd >= 0) close(c->fd);
    free(c->reqbuf);
    free(c->outbuf);
    free(c);
}

void handle_client_write(Client *c) {
    if (c->outbuf && c->out_sent < c->out_len) {
        ssize_t n = send(c->fd, c->outbuf + c->out_sent, c->out_len - c->out_sent, 0);
        if (n > 0) {
            c->out_sent += (size_t)n;
            if (c->out_sent == c->out_len) {
                remove_client(c);
            }
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            remove_client(c);
        }
        return;
    }

    if (!c->entry) return;
    if (c->entry->dead) {
        remove_client(c);
        return;
    }
    if (c->send_pos >= c->entry->data_len) {
        if (c->entry->complete) remove_client(c);
        return;
    }

    
    ssize_t n = send(c->fd,
                        c->entry->data + c->send_pos,
                        c->entry->data_len - c->send_pos,
                        0);
    if (n > 0) {
        c->send_pos += (size_t)n;

        DLOG("[CLIENT WRITE] fd=%d, key=%s, +%zd bytes, sent=%zu/%zu, complete=%d\n",
            c->fd,
            c->entry ? c->entry->key : "(null)",
            n,
            c->send_pos,
            c->entry ? c->entry->data_len : 0,
            c->entry ? c->entry->complete : 0);

        if (c->entry->complete && c->send_pos == c->entry->data_len) {
            remove_client(c);
        }
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        remove_client(c);
    }
}

void handle_client_request_ready(Client *c) {
    ParsedRequest r;
    CacheEntry *e;
    int http_err = 400;

    if (parse_request(c->reqbuf, c->req_len, &r, &http_err) != 0) {
        if (http_err == 501) {
            queue_simple_response(c, 501, "Not Implemented", "Only GET is supported\n");
        } else if (http_err == 505) {
            queue_simple_response(c, 505, "HTTP Version Not Supported", "Use HTTP/1.0 or HTTP/1.1\n");
        } else {
            queue_simple_response(c, 400, "Bad Request", "Malformed HTTP request\n");
        }
        free_parsed_request(&r);
        return;
    }

    e = find_cache_entry(r.key);
    if (!e) e = create_entry(&r);
    attach_client_to_entry(c, e);

    free(c->reqbuf);
    c->reqbuf = NULL;
    c->req_len = 0;
    c->req_cap = 0;

    free_parsed_request(&r);
}

void handle_client_read(Client *c) {
    char buf[READ_CHUNK];
    ssize_t n = recv(c->fd, buf, sizeof(buf), 0);

    if (n > 0) {
        append_bytes(&c->reqbuf, &c->req_len, &c->req_cap, buf, (size_t)n);
        if (c->req_len > MAX_HEADER_SIZE) {
            queue_simple_response(c, 400, "Bad Request", "Request headers too large\n");
            return;
        }
        if (find_header_end(c->reqbuf, c->req_len) >= 0) {
            handle_client_request_ready(c);
        }
        return;
    }

    if (n == 0) {
        remove_client(c);
        return;
    }

    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        remove_client(c);
    }
}

size_t count_clients(void) {
    size_t n = 0;
    Client *c;

    for (c = g_clients; c; c = c->next) n++;
    return n;
}
