#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include "proxy/server.h"
#include "scheduler/aio_scheduler.h"
#include "proxy/util.h"
#include "proxy/client.h"
#include "proxy/responses.h"

static void server_cleanup_callback(int err, void *udata) {
    server_task_t *task = udata;

    close(task->server->fd);
    if (task->server->health_check_timer != NULL) {
        task->server->health_check_timer->server = NULL;
    }
    free(task->server->cache_entry);
    free(task->server->uri.buffer);
    free(task->server);
    free(task);
}

static void send_error_to_all_clients(proxy_server_t server, char *msg, size_t msg_size) {
    for (proxy_client_t *cursor = server.cache_entry->pending; cursor != NULL; cursor = cursor->next) {
        client_health_check_timer_t *timer_task = malloc(sizeof(client_health_check_timer_t));
        *timer_task = (client_health_check_timer_t)
                      {
                          .task.type = ADD_TIMER,
                          .task.attrs.timer = {
                              .time = CLIENT_DISCONNECTION_TIMEOUT,
                              .callback = client_health_check_callback,
                              .data = timer_task
                          },
                          .client = cursor,
                          .cleanup_client = true,
                          .last_update = server.sched->loop_time
                      };
        cursor->health_check_timer = timer_task;

        client_task_t *send_error_task = malloc(sizeof(client_task_t));
        send_error_task->client = cursor;
        client_respond_error(send_error_task, msg, msg_size);
        aio_scheduler_schedule(cursor->sched, (task_t*) timer_task);
    }
}

static void early_fail_server_connection(proxy_server_t server, char *msg, size_t msg_size) {
    cache_delete(server.uri);

    send_error_to_all_clients(server, msg, msg_size);

    free(server.uri.buffer);
    free(server.cache_entry);
}

static void fail_server_connection(server_task_t *task, char *msg, size_t msg_size) {
    task->server->state = SERVER_DISCONNECTED;
    cache_delete(task->server->uri);

    send_error_to_all_clients(*task->server, msg, msg_size);

    task->task = (task_t)
                 {
                     .type = UNDELEGATE,
                     .attrs.ctl = {
                         .fd = task->server->fd,
                         .callback = server_cleanup_callback
                     }
                 };
    aio_scheduler_schedule(task->server->sched, (task_t*) task);
}

void establish_connect_with_server(aio_scheduler_t *sched, uri_t uri, cache_entry_t *entry) {
    proxy_server_t server_val = {
        .state = SERVER_CONNECTION_IN_PROGRESS,
        .sched = sched,
        .uri = uri,
        .health_check_timer = NULL,
        .cache_entry = entry
    };

    char *port = *uri.port == '\0' ? "80" : uri.port;
    struct addrinfo *res = resolve_address(uri.hostname, port);
    if (res == NULL) {
        early_fail_server_connection(server_val, not_found_response, not_found_response_size);
        return;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    server_val.fd = server_fd;
    proxy_server_t *server = malloc(sizeof(proxy_server_t));
    *server = server_val;

    struct addrinfo *current_try = res;
    int err = -1;
    do {
        err = connect(server_fd, current_try->ai_addr, current_try->ai_addrlen);

        // Connection in progress
        if (err < 0 && errno == EINPROGRESS) {
            try_connect_to_server_task_t *connect_task = malloc(sizeof(try_connect_to_server_task_t));
            *connect_task = (try_connect_to_server_task_t)
                            {
                                .task.type = WAIT_FOR_CONNECTION,
                                .task.attrs.io = {
                                    .fd = server_fd,
                                    .as_first = true,
                                    .data = connect_task,
                                    .callback = try_connect_callback
                                },
                                .server = server,
                                .first = res,
                                .next_try = current_try->ai_next
                            };
            aio_scheduler_schedule(sched, (task_t*) connect_task);
            break;
        }

        current_try = current_try->ai_next;
    } while (err < 0 && current_try != NULL);
    if (err < 0 && current_try == NULL) {
        fprintf(stderr, "[Error] Failed to connect to %s: %s\n", server->uri.hostname, strerror(errno));
        early_fail_server_connection(*server, bad_gateway_response, bad_gateway_response_size);
        free(server);
        freeaddrinfo(res);
        close(server_fd);
        return;
    }
    else if (err >= 0) {
        fprintf(stderr, "[Info] Connected to %s successfully\n", server->uri.hostname);
        server->state = SERVER_RECEIVING_HEADERS;
    }

    // TODO: Create timer for server

    // Schedule delegate server fd
    task_t *delegate_task = malloc(sizeof(task_t));
    *delegate_task = (task_t)
                     {
                         .type = DELEGATE,
                         .attrs.ctl = {
                             .fd = server_fd,
                             .callback = free_callback
                         }
                     };

    // Schedule request writing
    request_writing_task_t *write_req_task = malloc(sizeof(request_writing_task_t));
    *write_req_task = (request_writing_task_t)
                      {
                          .task.type = WRITE_REQUEST,
                          .task.attrs.io = {
                              .as_first = false,
                              .fd = server_fd,
                              .data = write_req_task,
                              .callback = write_request_callback
                          },
                          .server = server
                      };
    generate_request((char**) &write_req_task->task.attrs.io.buffer,
                     &write_req_task->task.attrs.io.size,
                     uri);

    // Schedule response reading
    response_analysis_task_t *read_res_task = malloc(sizeof(response_analysis_task_t));
    *read_res_task = (response_analysis_task_t)
                     {
                         .task.type = READ_REQUEST,
                         .task .attrs.io = {
                            .as_first = false,
                            .fd = server_fd,
                            .data = read_res_task,
                            .callback = analyze_response_callback
                         },
                         .server = server,
                         .content_length = 0,
                         .sm = HTTP_STATE_MACHINE_RES_INITIALIZER
                     };
    http_state_machine_alloc(&read_res_task->sm,
                             &read_res_task->task.attrs.io.buffer,
                             &read_res_task->task.attrs.io.size);

    aio_scheduler_schedule(sched, delegate_task);
    aio_scheduler_schedule(sched, (task_t*) write_req_task);
    aio_scheduler_schedule(sched, (task_t*) read_res_task);
}

void try_connect_callback(ssize_t r, int err, void *udata) {
    try_connect_to_server_task_t *task = udata;

    socklen_t len = sizeof(err);
    getsockopt(task->task.attrs.io.fd, SOL_SOCKET, SO_ERROR, &err, &len);

    if (err == 0) {
        fprintf(stderr, "[Info] Connected to %s successfully\n", task->server->uri.hostname);
        task->server->state = SERVER_RECEIVING_HEADERS;
        freeaddrinfo(task->first);
        free(task);
    }
    else {
        int connect_res = -1;
        struct addrinfo *current_try = task->next_try;
        while (connect_res < 0 && current_try != NULL) {
            connect_res = connect(task->task.attrs.io.fd, current_try->ai_addr, current_try->ai_addrlen);

            // Connection in progress
            if (connect_res < 0 && errno == EINPROGRESS) {
                task->next_try = current_try->ai_next;
                aio_scheduler_schedule(task->server->sched, (task_t*) task);
                break;
            }

            current_try = current_try->ai_next;
        }
        if (connect_res < 0 && current_try == NULL) {
            fprintf(stderr, "[Error] Failed to connect to %s\n", task->server->uri.hostname);
            fail_server_connection((server_task_t*) task, bad_gateway_response, bad_gateway_response_size);
            freeaddrinfo(task->first);
            return;
        }
    }
}

void write_request_callback(ssize_t w, int err, void *udata) {
    request_writing_task_t *task = udata;

    // If server isn't connected, free the task
    if (task->server->state != SERVER_RECEIVING_HEADERS &&
        task->server->state != SERVER_RECEIVING_BODY) {
        free(task);
        return;
    }

    // Check for errors or connection closed
    if (w < 0) {
        fprintf(stderr, "[Error] Error while trying to write: %s\n", strerror(err));
    }
    else if (w == 0) {
        fprintf(stderr, "[Error] Server terminated connection\n");
    }

    // If we didn't start sending body to clients, send them error
    if (w <= 0 && task->server->state == SERVER_RECEIVING_HEADERS) {
        fail_server_connection((server_task_t*) task, bad_gateway_response, bad_gateway_response_size);
        return;
    }

    free(task);
}

void analyze_response_callback(ssize_t r, int err, void *udata) {
    response_analysis_task_t *task = udata;

    if (task->server->state != SERVER_RECEIVING_HEADERS) {
        free(task);
        return;
    }

    // Check for errors or connection closed
    if (r < 0) {
        fprintf(stderr, "[Error] Error while trying to read from %s: %s\n", task->server->uri.hostname, strerror(err));
    }
    else if (r == 0) {
        fprintf(stderr, "[Error] Server %s terminated connection\n", task->server->uri.hostname);
    }
    if (r <= 0) {
        http_state_machine_destruct(&task->sm);
        fail_server_connection((server_task_t*) task, bad_gateway_response, bad_gateway_response_size);
        return;
    }

    http_state_machine_feed(&task->sm, (size_t) r);
    while (http_state_machine_step(&task->sm)) {
        switch (task->sm.state) {
            case MALFORMED:
                fprintf(stderr, "[Error] Server resp malformed\n");
                http_state_machine_destruct(&task->sm);
                fail_server_connection((server_task_t*) task, bad_gateway_response, bad_gateway_response_size);
                return;
            case HEADER_AVAILABLE:
                char *name, *value;
                size_t name_size, value_size;
                http_state_machine_get_header_name(&task->sm, task->sm.last_header, &name, &name_size);
                http_state_machine_get_header_value(&task->sm, task->sm.last_header, &value, &value_size);

                fprintf(stderr, "[Info] %s Field-name: \"", task->server->uri.hostname);
                for (size_t i = 0; i < name_size; i++) {
                    fprintf(stderr, "%c", name[i]);
                }
                fprintf(stderr, "\" Field-value: \"");
                for (size_t i = 0; i < value_size; i++) {
                    fprintf(stderr, "%c", value[i]);
                }
                fprintf(stderr, "\"\n");

                // TODO: Get content length, check Connection
                break;
            case COMPLETE:
                // TODO: Start writing
                printf("\nBuffer content:\n");
                printf("\033[32m");
                for (size_t i = 0; i < task->sm.analyzed; i++) {
                    printf("%c", task->sm.data.arr[i]);
                }
                printf("\033[0m");
                for (size_t i = task->sm.analyzed; i < task->sm.data.size; i++) {
                    printf("%c", task->sm.data.arr[i]);
                }
                printf("\n");
                http_state_machine_destruct(&task->sm);
                free(task);
                return;
            case READING_REQUEST_LINE:
            case READ_REQUEST_LINE:
                // Unreachable
                break;
            case READ_STATUS_LINE:
            case READING_STATUS_LINE:
            case READING_HEADER:
                break;
        }
    }

    http_state_machine_alloc(&task->sm, &task->task.attrs.io.buffer, &task->task.attrs.io.size);
    aio_scheduler_schedule(task->server->sched, (task_t*) task);
}
