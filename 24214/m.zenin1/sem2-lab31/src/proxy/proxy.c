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
#include <signal.h>
#include "scheduler/aio_scheduler.h"
#include "http.h"
#include "proxy/responses.h"
#include "cache/cache.h"
#include "proxy/client.h"
#include "proxy/util.h"

static aio_scheduler_t sched;
static int skip_poll_pipe[2];
bool alive;

static void close_listening_callback(int err, void *udata) {
    task_t *task = udata;
    int close_err;
    do {
        close_err = close(task->attrs.ctl.fd);
    } while (close_err < 0 && errno == EINTR);
}

static void stop_signal_handler(int sig) {
    if (alive) {
        // Write to skip poll and leave event loop
        write(skip_poll_pipe[1], "1", 1);
        alive = false;
    }
}

static void accept_connection(ssize_t r, int err, void *udata) {
    task_t *task = udata;

    // If task was canceled
    if (r < 0 && err == ECANCELED) {
        return;
    }

    struct sockaddr connected_addr;
    socklen_t connected_len = sizeof(connected_addr);

    int fd;
    do {
        fd = accept(task->attrs.io.fd, &connected_addr, &connected_len);
    } while (fd < 0 && errno == EINTR);

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    proxy_client_t *client = malloc(sizeof(proxy_client_t));
    *client = (proxy_client_t)
              {
                  .state = CLIENT_SENDING_REQUEST,
                  .fd = fd,
                  .sched = &sched,
                  .entry = NULL
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
    *delegate_task = (task_t) {
        .type = DELEGATE,
        .attrs.ctl = {
            .fd = fd,
            .data = delegate_task,
            .callback = free_callback
        }
    };

    process_request_task_t *req_task = malloc(sizeof(process_request_task_t));
    *req_task = (process_request_task_t) {
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
    *timer_task = (client_health_check_timer_t) {
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

    if (bind(listening, (struct sockaddr*) &addr, sizeof(addr))) {
        perror("Failed to bind");
        return;
    }
    if (listen(listening, 0)) {
        perror("Failed to listen");
        return;
    }

    sched = aio_scheduler_construct();
    pipe(skip_poll_pipe);
    alive = true;

    struct sigaction act;
    act.sa_handler = stop_signal_handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaddset(&act.sa_mask, SIGINT);
    sigaddset(&act.sa_mask, SIGTERM);
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);

    task_t delegate_task = {
       .type = DELEGATE,
       .attrs.ctl = {
           .fd = listening,
           .callback = NULL
       }
    };
    task_t delegate_skip_pipe_task = {
        .type = DELEGATE,
        .attrs.ctl = {
            .fd = skip_poll_pipe[0],
            .callback = NULL
        }
    };
    task_t accept_task = {
       .type = ACCEPT_CONNECTION_REQUESTS,
       .attrs.io = {
           .as_first = false,
           .fd = listening,
           .data = &accept_task,
           .callback = accept_connection
       }
    };
    task_t skip_task = {
        .type = READ_REQUEST,
        .attrs.io = {
            .as_first = false,
            .fd = skip_poll_pipe[0],
            .buffer = NULL,
            .size = 0,
            .callback = NULL
        }
    };

    aio_scheduler_schedule(&sched, &delegate_task);
    aio_scheduler_schedule(&sched, &delegate_skip_pipe_task);
    aio_scheduler_schedule(&sched, &accept_task);
    aio_scheduler_schedule(&sched, &skip_task);

    while (alive) {
        aio_scheduler_proceed(&sched, RUN_DEFAULT);
    }

    // Undelegate listening socket
    task_t undelegate_task = {
       .type = UNDELEGATE,
       .attrs.ctl = {
           .fd = listening,
           .data = &undelegate_task,
           .callback = close_listening_callback
       }
    };
    // Undelegate skip poll pipe
    task_t undelegate_task2 = {
       .type = UNDELEGATE,
       .attrs.ctl = {
           .fd = skip_poll_pipe[0],
           .callback = NULL
       }
    };
    aio_scheduler_schedule(&sched, &undelegate_task);
    aio_scheduler_schedule(&sched, &undelegate_task2);

    while (aio_scheduler_proceed(&sched, RUN_NO_TIMER_WAIT)) {}

    aio_scheduler_destruct(&sched);
    cache_destruct();
    close(skip_poll_pipe[0]);
    close(skip_poll_pipe[1]);
}
