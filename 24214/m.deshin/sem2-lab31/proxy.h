#ifndef PROXY_H
#define PROXY_H

#define _POSIX_C_SOURCE 200112L

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define LISTEN_BACKLOG 128
#define READ_CHUNK 8192
#define MAX_HEADER_SIZE 65536

#ifdef DEBUG
#define DLOG(...)                           \
    do {                                    \
        fprintf(stderr, __VA_ARGS__);       \
        fflush(stderr);                     \
    } while (0)
#else
#define DLOG(...) do { } while (0)
#endif

typedef struct CacheEntry CacheEntry;
typedef struct Client Client;

struct Client {
    int fd;
    char *reqbuf;
    size_t req_len;
    size_t req_cap;

    char *outbuf;
    size_t out_len;
    size_t out_cap;
    size_t out_sent;

    CacheEntry *entry;
    size_t send_pos;

    Client *next;
};

struct CacheEntry {
    char *key;
    char *host;
    char *path;
    int port;

    int upstream_fd;
    int connect_done;
    char *up_req;
    size_t up_req_len;
    size_t up_req_sent;

    char *data;
    size_t data_len;
    size_t data_cap;

    int header_parsed;
    size_t header_len;
    int status_code;
    int cacheable;
    int complete;
    int ref_clients;
    int dead;

    CacheEntry *next;
};

typedef struct {
    char *method;
    char *host;
    char *path;
    char *version;
    char *key;
    int port;
} ParsedRequest;

extern Client *g_clients;
extern CacheEntry *g_cache;

void free_parsed_request(ParsedRequest *r);
char *dup_range(const char *s, size_t n);
char *xstrdup(const char *s);
void ensure_capacity(char **buf, size_t *cap, size_t need);
void append_bytes(char **buf, size_t *len, size_t *cap, const void *src, size_t n);
int set_nonblocking(int fd);
ssize_t find_bytes(const char *buf, size_t len, const char *needle, size_t nlen);
ssize_t find_header_end(const char *buf, size_t len);
char *build_cache_key(const char *host, int port, const char *path);

int parse_request(const char *buf, size_t len, ParsedRequest *r, int *http_err);
void queue_simple_response(Client *c, int code, const char *reason, const char *body);

CacheEntry *find_cache_entry(const char *key);
void close_upstream(CacheEntry *e);
void mark_entry_dead(CacheEntry *e);
void maybe_reap_entry(CacheEntry *e);
void cleanup_dead_entries(void);
CacheEntry *create_entry(const ParsedRequest *r);
void attach_client_to_entry(Client *c, CacheEntry *e);
void detach_client_from_entry(Client *c);
void entry_make_error(CacheEntry *e, int code, const char *reason, const char *body);
void parse_response_header_if_ready(CacheEntry *e);
void handle_upstream_write(CacheEntry *e);
void handle_upstream_read(CacheEntry *e);
size_t count_upstreams(void);

Client *add_client(int fd);
void remove_client(Client *c);
void handle_client_write(Client *c);
void handle_client_request_ready(Client *c);
void handle_client_read(Client *c);
size_t count_clients(void);

int make_listen_socket(const char *port);
void accept_new_clients(int listen_fd);

#endif
