#include "proxy.h"

Client *g_clients = NULL;
CacheEntry *g_cache = NULL;

typedef enum {
    SLOT_LISTEN,
    SLOT_CLIENT,
    SLOT_UPSTREAM
} SlotKind;

typedef struct {
    SlotKind kind;
    void *ptr;
} SlotMap;

int main(int argc, char **argv) {
    int listen_fd;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <listen-port>\n", argv[0]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    listen_fd = make_listen_socket(argv[1]);
    if (listen_fd < 0) {
        perror("listen socket");
        return 1;
    }

    printf("proxy listening on port %s\n", argv[1]);

    for (;;) {
        size_t nclients = count_clients();
        size_t nup = count_upstreams();
        size_t nfds = 1 + nclients + nup;
        struct pollfd *pfds;
        SlotMap *map;
        size_t i;
        Client *c;
        CacheEntry *e;
        int rc;

        pfds = (struct pollfd *)calloc(nfds, sizeof(*pfds));
        map = (SlotMap *)calloc(nfds, sizeof(*map));
        if (!pfds || !map) {
            perror("calloc");
            free(pfds);
            free(map);
            break;
        }

        i = 0;
        pfds[i].fd = listen_fd;
        pfds[i].events = POLLIN;
        map[i].kind = SLOT_LISTEN;
        map[i].ptr = NULL;
        i++;

        for (c = g_clients; c; c = c->next) {
            short ev = POLLERR | POLLHUP;
            if (!c->entry && !(c->outbuf && c->out_sent < c->out_len)) {
                ev |= POLLIN;
            }
            if ((c->outbuf && c->out_sent < c->out_len) ||
                (c->entry && !c->entry->dead && 
                    (c->send_pos < c->entry->data_len || c->entry->complete))) {
                ev |= POLLOUT;
            }
            pfds[i].fd = c->fd;
            pfds[i].events = ev;
            map[i].kind = SLOT_CLIENT;
            map[i].ptr = c;
            i++;
        }

        for (e = g_cache; e; e = e->next) {
            short ev;
            if (e->dead || e->upstream_fd < 0) continue;
            ev = POLLERR | POLLHUP;
            if (e->connect_done) ev |= POLLIN;
            if (!e->connect_done || e->up_req_sent < e->up_req_len) ev |= POLLOUT;
            pfds[i].fd = e->upstream_fd;
            pfds[i].events = ev;
            map[i].kind = SLOT_UPSTREAM;
            map[i].ptr = e;
            i++;
        }

        rc = poll(pfds, nfds, -1);
        if (rc < 0) {
            if (errno == EINTR) {
                free(pfds);
                free(map);
                cleanup_dead_entries();
                continue;
            }
            perror("poll");
            free(pfds);
            free(map);
            break;
        }

        for (i = 0; i < nfds; ++i) {
            if (pfds[i].revents == 0) continue;

            if (map[i].kind == SLOT_LISTEN) {
                if (pfds[i].revents & POLLIN) accept_new_clients(listen_fd);
                continue;
            }

            if (map[i].kind == SLOT_CLIENT) {
                Client *cl = (Client *)map[i].ptr;
                if (!cl) continue;
                if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    remove_client(cl);
                    continue;
                }
                if (pfds[i].revents & POLLIN) {
                    handle_client_read(cl);
                    continue;
                }
                if (pfds[i].revents & POLLOUT) {
                    handle_client_write(cl);
                }
                continue;
            }

            if (map[i].kind == SLOT_UPSTREAM) {
                CacheEntry *ce = (CacheEntry *)map[i].ptr;
                if (!ce || ce->dead) continue;
                if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    if (ce->data_len == 0) {
                        entry_make_error(ce, 502, "Bad Gateway", "Upstream connection failed\n");
                    } else {
                        ce->complete = 1;
                        close_upstream(ce);
                    }
                    maybe_reap_entry(ce);
                    continue;
                }
                if (pfds[i].revents & POLLOUT) {
                    handle_upstream_write(ce);
                }
                if (pfds[i].revents & POLLIN) {
                    handle_upstream_read(ce);
                }
            }
        }

        free(pfds);
        free(map);
        cleanup_dead_entries();
    }

    close(listen_fd);
    return 0;
}
