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
#include "scheduler/aio_signal.h"
#include "http.h"
#include "proxy/responses.h"
#include "cache/cache.h"
#include "proxy/client.h"
#include "proxy/util.h"

static aio_scheduler_t sched;
static int listening;
static bool alive;

static task_t undelegate_task;
static void stop_signal_callback(int err, void *udata) {
    // Undelegate listening socket
    undelegate_task = (task_t) {
       .type = UNDELEGATE,
       .attrs.ctl = {
           .fd = listening,
           .data = &undelegate_task,
       }
    };
    aio_scheduler_schedule(&sched, &undelegate_task);
}
signal_t stop_sig = {
    .callback = stop_signal_callback,
    .data = NULL
};

static void stop_signal_handler(int sig) {
    if (alive) {
        aio_signal(&sched, &stop_sig);
        alive = false;
    }
}

static void accept_connection(ssize_t r, int err, void *udata) {
    task_t *task = udata;

    // If task was canceled
    if (r < 0) {
        return;
    }

    struct sockaddr connected_addr;
    socklen_t connected_len = sizeof(connected_addr);

    int fd;
    do {
        fd = accept(task->attrs.io.fd, &connected_addr, &connected_len);
    } while (fd < 0 && errno == EINTR);
    // Connection was failed
    if (fd < 0) {
        perror("[Error] Failed acception connection");
        goto accept_connection_defer_0;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    proxy_client_t *client = malloc(sizeof(proxy_client_t));
    if (client == NULL) {
        goto accept_connection_defer_1;
    }

    *client = (proxy_client_t) {
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

    client_task_t *delegate_task = malloc(sizeof(client_task_t));
    if (delegate_task == NULL) {
        goto accept_connection_defer_2;
    }

    *delegate_task = (client_task_t) {
        .task.type = DELEGATE,
        .task.attrs.ctl = {
            .fd = fd,
            .data = delegate_task,
            .callback = client_delegate_callback
        },
        .client = client
    };

    process_request_task_t *req_task = malloc(sizeof(process_request_task_t));
    if (req_task == NULL) {
        goto accept_connection_defer_3;
    }

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
    if (http_state_machine_alloc(&req_task->sm, &req_task->task.attrs.io.buffer, &req_task->task.attrs.io.size)) {
        goto accept_connection_defer_4;
    }

    client_health_check_timer_t *timer_task = malloc(sizeof(client_health_check_timer_t));
    if (timer_task == NULL) {
        goto accept_connection_defer_4;
    }

    *timer_task = (client_health_check_timer_t) {
        .task.type = ADD_TIMER,
        .task.attrs.timer = {
            .time = CLIENT_TIMEOUT,
            .callback = client_health_check_callback,
            .data = timer_task
        },
        .client = client,
        .cleanup_client = true,
        .last_update = sched.loop_time
    };
    client->health_check_timer = timer_task;

    delegate_task->task.next = (task_t*) req_task;
    req_task->task.next = (task_t*) timer_task;
    timer_task->task.next = NULL;
    aio_scheduler_schedule_all(&sched, (task_t*) delegate_task);
    goto accept_connection_defer_0;

accept_connection_defer_4:
    http_state_machine_destruct(&req_task->sm);
    free(req_task);
accept_connection_defer_3:
    free(delegate_task);
accept_connection_defer_2:
    free(client);
accept_connection_defer_1:
    fprintf(stderr, "[Error] Failed to allocate structures for cloent\n");
    close(fd);
accept_connection_defer_0:
    aio_scheduler_schedule(&sched, task);
}


int start_proxy(struct in_addr ip, in_port_t port) {
    int ret = 0;

    cache_init();

    listening = socket(AF_INET, SOCK_STREAM, 0);
    if (listening < 0) {
        perror("[Error] Failed to create listening socket");
        ret = -1;
        goto start_proxy_defer_0;
    }

    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_port = port,
                               .sin_addr = ip};

    if (bind(listening, (struct sockaddr*) &addr, sizeof(addr))) {
        perror("[Error] Failed to bind listening socket");
        ret = -1;
        goto start_proxy_defer_1;
    }
    if (listen(listening, 0)) {
        perror("[Error] Failed to listen on socket");
        ret = -1;
        goto start_proxy_defer_1;
    }

    if (aio_scheduler_construct(&sched)) {
        perror("[Error] Failed to construct scheduler\n");
        ret = -1;
        goto start_proxy_defer_1;
    }

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
    task_t accept_task = {
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

    while (aio_scheduler_proceed(&sched, RUN_NO_TIMER_WAIT)) {}

    aio_scheduler_destruct(&sched);
start_proxy_defer_1:
    close(listening);
start_proxy_defer_0: 
    cache_destruct();
    return ret;
}
