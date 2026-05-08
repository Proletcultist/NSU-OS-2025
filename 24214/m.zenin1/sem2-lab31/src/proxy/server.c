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

static void server_cleanup_callback(ssize_t r, int err, void *udata) {
    server_connection_task_t *task = udata;
    close(task->server->fd);
    free(task->server->cache_entry);
    free(task->server->uri.buffer);
    free(task->server);
    free(task);
}

static void early_fail_server_connection(proxy_server_t server, char *msg, size_t msg_size) {
    cache_delete(server.uri);
    for (proxy_client_t *cursor = server.cache_entry->pending; cursor != NULL; cursor = cursor->next) {
        client_respond_error(cursor, msg, msg_size);
    }
    free(server.uri.buffer);
    free(server.cache_entry);
}
static void fail_server_connection(server_connection_task_t *task, char *msg, size_t msg_size) {
    task->server->state = SERVER_DISCONNECTED;
    cache_delete(task->server->uri);
    for (proxy_client_t *cursor = task->server->cache_entry->pending; cursor != NULL; cursor = cursor->next) {
        client_respond_error(cursor, msg, msg_size);
    }

    task->task = (task_t)
                 {
                     .type = UNDELEGATE,
                     .fd = task->task.fd,
                     .callback = server_cleanup_callback
                 };
    aio_scheduler_schedule(task->server->sched, (task_t*) task);
}

void establish_connect_with_server(aio_scheduler_t *sched, uri_t uri, cache_entry_t *entry) {
    char *port = *uri.port == '\0' ? "80" : uri.port;
    struct addrinfo *res = resolve_address(uri.hostname, port);
    if (res == NULL) {
        early_fail_server_connection((proxy_server_t) {.uri = uri, .cache_entry = entry}, not_found_response, not_found_response_size);
        return;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    proxy_server_t *server = malloc(sizeof(proxy_server_t));
    *server = (proxy_server_t)
              {
                  .state = SERVER_CONNECTION_IN_PROGRESS,
                  .fd = server_fd,
                  .sched = sched,
                  .uri = uri,
                  .cache_entry = entry
              };

    struct addrinfo *current_try = res;
    int err = -1;
    do {
        err = connect(server_fd, current_try->ai_addr, current_try->ai_addrlen);

        // Connection in progress
        if (err < 0 && errno == EINPROGRESS) {
            try_connect_to_server_task_t *connect_task = malloc(sizeof(try_connect_to_server_task_t));
            *connect_task = (try_connect_to_server_task_t)
                            {
                                .server_task = (server_connection_task_t)
                                               {
                                                .task = (task_t)
                                                        {
                                                            .type = WAIT_FOR_CONNECTION,
                                                            .fd = server_fd,
                                                            .as_first = true,
                                                            .data = connect_task,
                                                            .callback = try_connect_callback
                                                        },
                                                .server = server
                                               },
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
        server->state = SERVER_CONNECTED;
    }

    // Schedule delegate server fd
    task_t *delegate_task = malloc(sizeof(task_t));
    *delegate_task = (task_t)
                     {
                         .type = DELEGATE,
                         .fd = server_fd,
                         .callback = free_callback
                     };
    aio_scheduler_schedule(sched, delegate_task);

    // Schedule request writing
    request_writing_task_t *write_req_task = malloc(sizeof(request_writing_task_t));
    *write_req_task = (request_writing_task_t)
                      {
                          .server_task = (server_connection_task_t)
                                         {
                                          .task = (task_t)
                                                  {
                                                      .type = WRITE_REQUEST,
                                                      .as_first = false,
                                                      .fd = server_fd,
                                                      .data = write_req_task,
                                                      .callback = write_request_callback
                                                  },
                                          .server = server
                                         }
                      };
    generate_request((char**) &write_req_task->server_task.task.buffer,
                     &write_req_task->server_task.task.size,
                     uri);
    aio_scheduler_schedule(sched, (task_t*) write_req_task);

    // Schedule response reading
    response_analysis_task_t *read_res_task = malloc(sizeof(response_analysis_task_t));
    *read_res_task = (response_analysis_task_t)
                     {
                         .server_task = (server_connection_task_t)
                                        {
                                            .task = (task_t)
                                                    {
                                                        .type = READ_REQUEST,
                                                        .as_first = false,
                                                        .fd = server_fd,
                                                        .data = read_res_task,
                                                        .callback = analyze_response_callback
                                                    },
                                            .server = server
                                        },
                         .content_length = 0,
                         .sm = HTTP_STATE_MACHINE_RES_INITIALIZER
                     };
    http_state_machine_alloc(&read_res_task->sm,
                             &read_res_task->server_task.task.buffer,
                             &read_res_task->server_task.task.size);
    aio_scheduler_schedule(sched, (task_t*) read_res_task);
}

void try_connect_callback(ssize_t r, int err, void *udata) {
    try_connect_to_server_task_t *task = udata;

    socklen_t len = sizeof(err);
    getsockopt(task->server_task.task.fd, SOL_SOCKET, SO_ERROR, &err, &len);

    if (err == 0) {
        fprintf(stderr, "[Info] Connected to %s successfully\n", task->server_task.server->uri.hostname);
        task->server_task.server->state = SERVER_CONNECTED;
        freeaddrinfo(task->first);
        free(task);
    }
    else {
        int connect_res = -1;
        struct addrinfo *current_try = task->next_try;
        while (connect_res < 0 && current_try != NULL) {
            connect_res = connect(task->server_task.task.fd, current_try->ai_addr, current_try->ai_addrlen);

            // Connection in progress
            if (connect_res < 0 && errno == EINPROGRESS) {
                task->next_try = current_try->ai_next;
                aio_scheduler_schedule(task->server_task.server->sched, (task_t*) task);
                break;
            }

            current_try = current_try->ai_next;
        }
        if (connect_res < 0 && current_try == NULL) {
            fprintf(stderr, "[Error] Failed to connect to %s\n", task->server_task.server->uri.hostname);
            fail_server_connection((server_connection_task_t*) task, bad_gateway_response, bad_gateway_response_size);
            freeaddrinfo(task->first);
            return;
        }
    }
}

void write_request_callback(ssize_t w, int err, void *udata) {
    request_writing_task_t *task = udata;

    // If server is in process of disconnecting, don't do anything
    if (task->server_task.server->state == SERVER_DISCONNECTED) {
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
    if (w <= 0) {
        fail_server_connection((server_connection_task_t*) task, bad_gateway_response, bad_gateway_response_size);
    }
    free(task);
}

void analyze_response_callback(ssize_t r, int err, void *udata) {
    response_analysis_task_t *task = udata;

    // If server is in process of disconnecting, don't do anything
    if (task->server_task.server->state == SERVER_DISCONNECTED) {
        free(task);
        return;
    }

    // Check for errors or connection closed
    if (r < 0) {
        fprintf(stderr, "[Error] Error while trying to read from %s: %s\n", task->server_task.server->uri.hostname, strerror(err));
    }
    else if (r == 0) {
        fprintf(stderr, "[Error] Server %s terminated connection\n", task->server_task.server->uri.hostname);
    }
    if (r <= 0) {
        fail_server_connection((server_connection_task_t*) task, bad_gateway_response, bad_gateway_response_size);
        http_state_machine_destruct(&task->sm);
        return;
    }

    http_state_machine_feed(&task->sm, (size_t) r);
    while (http_state_machine_step(&task->sm)) {
        switch (task->sm.state) {
            case MALFORMED:
                fprintf(stderr, "[Error] Server resp malformed\n");
                fail_server_connection((server_connection_task_t*) task, bad_gateway_response, bad_gateway_response_size);
                http_state_machine_destruct(&task->sm);
                return;
            case HEADER_AVAILABLE:
                char *name, *value;
                size_t name_size, value_size;
                http_state_machine_get_header_name(&task->sm, task->sm.last_header, &name, &name_size);
                http_state_machine_get_header_value(&task->sm, task->sm.last_header, &value, &value_size);

                fprintf(stderr, "[Info] %s Field-name: \"", task->server_task.server->uri.hostname);
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

    http_state_machine_alloc(&task->sm, &task->server_task.task.buffer, &task->server_task.task.size);
    aio_scheduler_schedule(task->server_task.server->sched, (task_t*) task);
}
