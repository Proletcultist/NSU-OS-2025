#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "task.h"
#include "proxy.h"
#include "cache.h"

int proxy_construct(proxy_t *proxy, struct in_addr ip, in_port_t port) {
    *proxy = (proxy_t) {.fds = vector_pollfd_t_construct(),
                        .tasks = vector_task_t_ptr_construct(),
                        .cache = cache_construct()};

    int listening = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = port, .sin_addr = ip};
    if (bind(listening, (struct sockaddr*) &addr, sizeof(addr))) {
        return -1;
    }
    if (listen(listening, 0)) {
        return -1;
    }

    vector_pollfd_t_push(&proxy->fds, (struct pollfd) {.fd = listening, .events = POLLIN, .revents = 0});
    proxy->tasks.size = 1;

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

    vector_pollfd_t_push(&proxy->fds, (struct pollfd) {.fd = fd, .events = POLLIN, .revents = 0});

    read_request_task_t *task = malloc(sizeof(read_request_task_t));
    *task = (read_request_task_t) {.super = {.type = READ_REQUEST, .next = NULL}, .buffer = NULL};
    vector_task_t_ptr_push(&proxy->tasks, (task_t*) task);
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
            
        }
    }
}

void proxy_destruct(proxy_t *proxy) {
    for (size_t i = 0; i < proxy->fds.size; i++) {
        close(proxy->fds.arr[i].fd);
    }
    vector_pollfd_t_destruct(&proxy->fds);
    vector_task_t_ptr_destruct(&proxy->tasks);
    cache_destruct(&proxy->cache);
}
