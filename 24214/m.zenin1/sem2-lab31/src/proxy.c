#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <stddef.h>
#include "aio_scheduler.h"

#define CHUNK_SIZE 512

typedef struct vector_buffered_task {
    task_t task;

    void *data;
    size_t size, cap;
} vector_buffered_task_t;

static void read_req_line_callback(ssize_t r, int errno, void *udata) {
    if (r < 0) {
        // TODO: Handle errors
    }
    else if (r == 0) {
        // TODO: Handle EOF
    }

    vector_buffered_task_t *task = udata;

    task->size += (size_t) r;

    printf("\nBuffer content (size: %zu):\n", task->size);
    for (size_t i = 0; i < task->size; i++) {
        printf("%c", ((char*) task->data)[i]);
    }
    printf("\n");

    // TODO: Realloc if buffer exceeded
    *task = (vector_buffered_task_t)
            {
                 .task = {
                     .type = READ_REQUEST,
                     .fd = task->task.fd,
                     .buffer = (char*) task->data + task->size,
                     .size = task->cap - task->size,
                     .data = task,
                     .callback = read_req_line_callback
                 },
                 .data = task->data,
                 .size = task->size,
                 .cap = task->cap
            };
    aio_scheduler_schedule((task_t*) task);
}

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

    vector_buffered_task_t *read_req_task = malloc(sizeof(vector_buffered_task_t));
    void *buffer = malloc(CHUNK_SIZE); 
    *read_req_task = (vector_buffered_task_t) 
                     {
                         .task = {
                             .type = READ_REQUEST,
                             .fd = fd,
                             .buffer = buffer,
                             .size = CHUNK_SIZE,
                             .data = read_req_task,
                             .callback = read_req_line_callback
                         },
                         .data = buffer,
                         .size = 0,
                         .cap = CHUNK_SIZE
                     };

    aio_scheduler_schedule((task_t*) read_req_task);
}


void start_proxy(struct in_addr ip, in_port_t port) {
    int listening = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_port = port,
                               .sin_addr = ip};

    // TODO: Propagate error
    if (bind(listening, (struct sockaddr*) &addr, sizeof(addr))) {
        perror("Failed to bind");
        return;
    }
    if (listen(listening, 0)) {
        perror("Failed to listen");
        return;
    }

    task_t accept_task;
    accept_task = (task_t) {
                              .type = ACCEPT_CONNECTION_REQUESTS,
                              .fd = listening,
                              .buffer = NULL,
                              .size = 0,
                              .data = &accept_task,
                              .callback = accept_connection
                           };

    aio_scheduler_schedule(&accept_task);

    while (true) {
        aio_scheduler_proceed();
    }
}
