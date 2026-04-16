#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "connection.h"
#include "proxy.h"
#include "cache.h"

#define RESERVING_SIZE 512

int proxy_construct(proxy_t *proxy, struct in_addr ip, in_port_t port) {
    *proxy = (proxy_t) {.fds = vector_pollfd_t_construct(),
                        .connections = vector_connection_t_construct(),
                        .cache = cache_construct()};

    int listening = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_port = port,
                               .sin_addr = ip};
    if (bind(listening, (struct sockaddr*) &addr, sizeof(addr))) {
        return -1;
    }
    if (listen(listening, 0)) {
        return -1;
    }

    vector_pollfd_t_push(&proxy->fds, (struct pollfd) {.fd = listening,
                                                       .events = POLLIN,
                                                       .revents = 0});
    proxy->connections.size = 1;

    return 0;
}

static void proxy_connect_new_client(proxy_t *proxy) {
    struct sockaddr connected_addr;
    socklen_t connected_len = sizeof(connected_addr);
    int fd = accept(proxy->fds.arr[0].fd, &connected_addr, &connected_len);

    if (connected_addr.sa_family == AF_INET) {
        char buff[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &((struct sockaddr_in*) &connected_addr)->sin_addr, buff, INET_ADDRSTRLEN);
        fprintf(stderr, "[Info] Connected: %s\n", buff);
    }

    vector_pollfd_t_push(&proxy->fds, (struct pollfd) {.fd = fd,
                                                       .events = POLLIN,
                                                       .revents = 0});

    exchange_buffer_t *request_buffer = malloc(sizeof(exchange_buffer_t));
    *request_buffer = exchange_buffer_construct();
    vector_connection_t_push(&proxy->connections, (connection_t) {.type = CLIENT_CONNECTION,
                                                                  .c.client = {.status = READ_REQUEST, 
                                                                               .bytes_sended_back = 0,
                                                                               .request_exchange = request_buffer,
                                                                               .response_exchange = NULL}});
}

static void proxy_handle_reading(connection_t *conn, int fd) {
    if (conn->type == CLIENT_CONNECTION && conn->c.client.status == READ_REQUEST) {
    }
    else if (conn->type == SERVER_CONNECTION && conn->c.server.status == WRITE_RESPONSE) {
    }
    else {
        // TODO: Handle
    }
}

void proxy_proceed(proxy_t *proxy) {
    if (poll(proxy->fds.arr, proxy->fds.size, -1) == -1) {
        perror("Failed to poll");
        return;
    }

    if (proxy->fds.arr[0].revents & POLLIN) {
        proxy_connect_new_client(proxy);
    }
    else if (proxy->fds.arr[0].revents & POLLHUP) {
        // TODO: Handle
    }
    else if (proxy->fds.arr[0].revents & POLLERR) {
        // TODO: Handle
    }

    for (size_t i = 1; i < proxy->fds.size; i++) {
        if (proxy->fds.arr[i].revents & POLLIN) {
            proxy_handle_reading(&proxy->connections.arr[i], proxy->fds.arr[i].fd);
        }
    }
}

void proxy_destruct(proxy_t *proxy) {
    for (size_t i = 0; i < proxy->fds.size; i++) {
        close(proxy->fds.arr[i].fd);
    }
    vector_pollfd_t_destruct(&proxy->fds);
    vector_connection_t_destruct(&proxy->connections);
    cache_destruct(&proxy->cache);
}
