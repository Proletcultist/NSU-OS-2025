#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <stddef.h>
#include "aio_scheduler.h"

aio_scheduler_t sched;

static void accept_connection(ssize_t r, int errno, void *udata) {
    task_t *task = udata;

    struct sockaddr connected_addr;
    socklen_t connected_len = sizeof(connected_addr);
    int fd = accept(task->fd, &connected_addr, &connected_len);

    if (connected_addr.sa_family == AF_INET) {
        char buff[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &((struct sockaddr_in*) &connected_addr)->sin_addr, buff, INET_ADDRSTRLEN);
        fprintf(stderr, "[Info] Connected: %s\n", buff);
    }
}

int main() {
    sched = aio_scheduler_construct();

    struct in_addr ip;
    in_port_t port = htons(12345);
    inet_pton(AF_INET, "127.0.0.1", &ip);

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

    task_t accept_task;
    accept_task = (task_t) {
                              .type = ACCEPT_CONNECTION_REQUESTS,
                              .fd = listening,
                              .buffer = NULL,
                              .off = 0,
                              .size = 0,
                              .data = &accept_task,
                              .callback = accept_connection
                           };

    aio_scheduler_schedule(&sched, &accept_task);

    while (true) {
        aio_scheduler_proceed(&sched);
    }

    return 0;
}
