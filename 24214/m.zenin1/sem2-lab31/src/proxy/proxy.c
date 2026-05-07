#include <math.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stddef.h>
#include "scheduler/aio_scheduler.h"
#include "http.h"
#include "proxy/responses.h"
#include "cache/cache.h"
#include "proxy/client.h"

static void accept_connection(ssize_t r, int err, void *udata) {
    task_t *task = udata;

    struct sockaddr connected_addr;
    socklen_t connected_len = sizeof(connected_addr);
    int fd = accept(task->fd, &connected_addr, &connected_len);

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    proxy_client_t *client = malloc(sizeof(proxy_client_t));
    client->state = CLIENT_CONNECTED;
    if (connected_addr.sa_family == AF_INET) {
        inet_ntop(AF_INET, &((struct sockaddr_in*) &connected_addr)->sin_addr, client->client_ip, INET_ADDRSTRLEN);
        fprintf(stderr, "[Info] Connected: %s\n", client->client_ip);
    }
    else {
        strncpy(client->client_ip, "Unknown", sizeof(client->client_ip) - 1);
        client->client_ip[sizeof(client->client_ip - 1)] = '\0';
    }

    request_analysis_task_t *read_req_task = malloc(sizeof(request_analysis_task_t));
    read_req_task->client = client;
    read_req_task->bytes_received = 0;
    read_req_task->sm = HTTP_STATE_MACHINE_REQ_INITIALIZER;
    read_req_task->sm.discarding = true;
    read_req_task->bad_request = false;
    void *buffer;
    size_t size;
    http_state_machine_alloc(&read_req_task->sm, &buffer, &size);
    read_req_task->task = (task_t)
                          {
                              .type = READ_REQUEST,
                              .fd = fd,
                              .buffer = buffer,
                              .size = size,
                              .data = read_req_task,
                              .timeout = 10.0,
                              .callback = analyze_request_callback
                          };

    aio_scheduler_schedule((task_t*) read_req_task, false);
    aio_scheduler_schedule(task, false);
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
    accept_task = (task_t) 
                  {
                     .type = ACCEPT_CONNECTION_REQUESTS,
                     .fd = listening,
                     .data = &accept_task,
                     .timeout = NAN,
                     .callback = accept_connection
                  };

    aio_scheduler_schedule(&accept_task, false);

    while (true) {
        aio_scheduler_proceed();
    }
}
