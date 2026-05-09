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
#include "proxy/util.h"

static aio_scheduler_t sched;

static void accept_connection(ssize_t r, int err, void *udata) {
    task_t *task = udata;

    struct sockaddr connected_addr;
    socklen_t connected_len = sizeof(connected_addr);
    int fd = accept(task->attrs.io.fd, &connected_addr, &connected_len);

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    proxy_client_t *client = malloc(sizeof(proxy_client_t));
    *client = (proxy_client_t)
              {
                  .state = CLIENT_SENDING_REQUEST,
                  .fd = fd,
                  .sched = &sched,
              };
    if (connected_addr.sa_family == AF_INET) {
        inet_ntop(AF_INET, &((struct sockaddr_in*) &connected_addr)->sin_addr, client->client_ip, INET_ADDRSTRLEN);
        fprintf(stderr, "[Info] Connected: %s\n", client->client_ip);
    }
    else {
        strncpy(client->client_ip, "Unknown", sizeof(client->client_ip) - 1);
        client->client_ip[sizeof(client->client_ip - 1)] = '\0';
    }

    task_t *delegate_task = malloc(sizeof(task_t));
    *delegate_task = (task_t)
                     {
                         .type = DELEGATE,
                         .attrs.ctl = {
                             .fd = fd,
                             .callback = free_callback
                         }
                     };

    process_request_task_t *req_task = malloc(sizeof(process_request_task_t));
    *req_task = (process_request_task_t)
                {
                    .task.type = READ_REQUEST,
                    .task.attrs.io = {
                        .as_first = false,
                        .fd = fd,
                        .data = req_task,
                        .callback = process_request_callback
                    },
                    .client = client,
                    .bytes_received = 0,
                    .sm = HTTP_STATE_MACHINE_REQ_INITIALIZER,
                    .bad_request = false
                };
    req_task->sm.discarding = true;
    http_state_machine_alloc(&req_task->sm, &req_task->task.attrs.io.buffer, &req_task->task.attrs.io.size);

    client_health_check_timer_t *timer_task = malloc(sizeof(client_health_check_timer_t));
    *timer_task = (client_health_check_timer_t)
                  {
                      .task.type = ADD_TIMER,
                      .task.attrs.timer = {
                          .time = CLIENT_SEND_REQUEST_TIMEOUT,
                          .callback = client_health_check_callback,
                          .data = timer_task
                      },
                      .client = client,
                      .cleanup_client = true,
                      .last_update = sched.loop_time
                  };
    client->health_check_timer = timer_task;

    aio_scheduler_schedule(&sched, delegate_task);
    aio_scheduler_schedule(&sched, (task_t*) req_task);
    aio_scheduler_schedule(&sched, (task_t*) timer_task);
    aio_scheduler_schedule(&sched, task);
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

    sched = aio_scheduler_construct();

    task_t delegate_task = {
                              .type = DELEGATE,
                              .attrs.ctl = {
                                  .fd = listening,
                                  .callback = NULL
                              }
                           };
    task_t accept_task =   {
                              .type = ACCEPT_CONNECTION_REQUESTS,
                              .attrs.io = {
                                  .as_first = false,
                                  .fd = listening,
                                  .data = &accept_task,
                                  .callback = accept_connection
                              }
                           };

    aio_scheduler_schedule(&sched, &delegate_task);
    aio_scheduler_schedule(&sched, &accept_task);

    while (true) {
        aio_scheduler_proceed(&sched);
    }
}
