#pragma once

#include <poll.h>
#include <netinet/in.h>
#include "connection.h"
#include "cache.h"

#define NAME vector_pollfd_t 
#define TYPE struct pollfd
#define VECTOR_DECL
#include "template/vector.h"
#undef VECTOR_DECL
#undef NAME
#undef TYPE

#define NAME vector_connection_t
#define TYPE connection_t
#define VECTOR_DECL
#include "template/vector.h"
#undef VECTOR_DECL
#undef NAME
#undef TYPE

typedef struct proxy {
    // 0th fd is always a proxy listening fd
    vector_pollfd_t fds;
    vector_connection_t connections;
    cache_t cache;
} proxy_t;

int proxy_construct(proxy_t *proxy, struct in_addr ip, in_port_t port);
void proxy_proceed(proxy_t *proxy);
void proxy_destruct(proxy_t *proxy);
