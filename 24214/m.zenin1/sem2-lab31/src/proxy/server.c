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

static void fail_server_connection(proxy_server_t server, char *msg, size_t msg_size) {
    cache_delete(server.uri);
    for (pending_client_t *cursor = server.cache_entry->pending; cursor != NULL; cursor = cursor->next) {
        schedule_error_response(cursor->fd, msg, msg_size);
    }
    free(server.uri.buffer);
    free(server.cache_entry);
}

void establish_connect_with_server(uri_t uri, cache_entry_t *entry) {
    char *port = *uri.port == '\0' ? "80" : uri.port;
    struct addrinfo *res = resolve_address(uri.hostname, port);
    if (res == NULL) {
        fail_server_connection((proxy_server_t) {.uri = uri, .cache_entry = entry}, not_found_response, not_found_response_size);
        return;
    }

    proxy_server_t *server = malloc(sizeof(proxy_server_t));
    *server = (proxy_server_t)
              {
                  .state = SERVER_CONNECTION_IN_PROGRESS,
                  .uri = uri,
                  .cache_entry = entry
              };
    int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

    struct addrinfo *current_try = res;
    int err = -1;
    do {
        err = connect(server_fd, current_try->ai_addr, current_try->ai_addrlen);

        // Connection in progress
        if (err < 0 && errno == EINPROGRESS) {
            try_connect_to_server_task_t *connect_task = malloc(sizeof(try_connect_to_server_task_t));
            *connect_task = (try_connect_to_server_task_t)
                            {
                                .task = (task_t)
                                        {
                                            .type = WAIT_FOR_CONNECTION,
                                            .fd = server_fd,
                                            .data = connect_task,
                                            .timeout = 2.0,
                                            .callback = try_connect_callback
                                        },
                                .server = server,
                                .first = res,
                                .next_try = current_try->ai_next
                            };
            aio_scheduler_schedule((task_t*) connect_task, true);
            break;
        }

        current_try = current_try->ai_next;
    } while (err < 0 && current_try != NULL);
    if (err < 0 && current_try == NULL) {
        fprintf(stderr, "[Error] Failed to connect to %s: %s\n", server->uri.hostname, strerror(errno));
        fail_server_connection(*server, bad_gateway_response, bad_gateway_response_size);
        free(server);
        freeaddrinfo(res);
        close(server_fd);
        return;
    }
    else if (err >= 0) {
        fprintf(stderr, "[Info] Connected to %s successfully\n", server->uri.hostname);
        server->state = SERVER_CONNECTED;
    }

    // Schedule request writing
    char *req_buffer;
    size_t req_size;
    generate_request(&req_buffer, &req_size, uri);
    request_writing_task_t *write_req_task = malloc(sizeof(request_writing_task_t));
    *write_req_task = (request_writing_task_t)
                      {
                          .task = (task_t)
                                  {
                                      .type = WRITE_REQUEST,
                                      .fd = server_fd,
                                      .buffer = req_buffer,
                                      .size = req_size,
                                      .data = write_req_task,
                                      .timeout = 10.0,
                                      .callback = write_request_callback
                                  },
                          .server = server
                      };
    aio_scheduler_schedule((task_t*) write_req_task, false);

    // Schedule response reading
    response_analysis_task_t *read_res_task = malloc(sizeof(response_analysis_task_t));
    read_res_task->server = server;
    read_res_task->sm = HTTP_STATE_MACHINE_RES_INITIALIZER;
    void *resp_buffer;
    size_t resp_size;
    http_state_machine_alloc(&read_res_task->sm, &resp_buffer, &resp_size);
    read_res_task->task = (task_t)
                          {
                              .type = READ_REQUEST,
                              .fd = server_fd,
                              .buffer = resp_buffer,
                              .size = resp_size,
                              .data = read_res_task,
                              .timeout = 10.0,
                              .callback = analyze_response_callback
                          };

    aio_scheduler_schedule((task_t*) read_res_task, false);
}

void try_connect_callback(ssize_t r, int err, void *udata) {
    try_connect_to_server_task_t *task = udata;

    // If no error happened yet (timeout or cancellation)
    // Check if connection was successful or not
    if (r >= 0) {
        socklen_t len = sizeof(err);
        getsockopt(task->task.fd, SOL_SOCKET, SO_ERROR, &err, &len);
    }
    else if (err == ECANCELED) {
        freeaddrinfo(task->first);
        free(task);
        return;
    }

    if (err == 0) {
        fprintf(stderr, "[Info] Connected to %s successfully\n", task->server->uri.hostname);
        task->server->state = SERVER_CONNECTED;
        freeaddrinfo(task->first);
        free(task);
    }
    else {
        int connect_res = -1;
        struct addrinfo *current_try = task->next_try;
        while (connect_res < 0 && current_try != NULL) {
            connect_res = connect(task->task.fd, current_try->ai_addr, current_try->ai_addrlen);

            // Connection in progress
            if (connect_res < 0 && errno == EINPROGRESS) {
                task->next_try = current_try->ai_next;
                aio_scheduler_schedule((task_t*) task, true);
                break;
            }

            current_try = current_try->ai_next;
        }
        if (connect_res < 0 && current_try == NULL) {
            fprintf(stderr, "[Error] Failed to connect to %s\n", task->server->uri.hostname);
            if (err == ETIMEDOUT) {
                fail_server_connection(*task->server, gateway_timeout_response, gateway_timeout_response_size);
            }
            else {
                fail_server_connection(*task->server, bad_gateway_response, bad_gateway_response_size);
            }
            aio_scheduler_cancel_all(task->task.fd);
            close(task->task.fd);
            freeaddrinfo(task->first);
            free(task->server);
            free(task);
            return;
        }
    }
}

void write_request_callback(ssize_t w, int err, void *udata) {
    request_writing_task_t *task = udata;

    // Check for errors or connection closed
    if (w < 0 && err != ECANCELED) {
        fprintf(stderr, "[Error] Error while trying to write: %s\n", strerror(err));
        fail_server_connection(*task->server, bad_gateway_response, bad_gateway_response_size);
        aio_scheduler_cancel_all(task->task.fd);
        free(task->server);
        close(task->task.fd);
    }
    else if (w == 0) {
        fprintf(stderr, "[Error] Server terminated connection\n");
        fail_server_connection(*task->server, bad_gateway_response, bad_gateway_response_size);
        aio_scheduler_cancel_all(task->task.fd);
        free(task->server);
        close(task->task.fd);
    }

    free(task);
}

void analyze_response_callback(ssize_t r, int err, void *udata) {
    response_analysis_task_t *task = udata;

    // Check for errors or connection closed
    if (r < 0 && err == ECANCELED) {
        http_state_machine_destruct(&task->sm);
        free(task);
        return;
    }
    else if (r < 0) {
        fprintf(stderr, "[Error] Error while trying to read from %s: %s\n", task->server->uri.hostname, strerror(err));

        fail_server_connection(*task->server, bad_gateway_response, bad_gateway_response_size);
        aio_scheduler_cancel_all(task->task.fd);
        close(task->task.fd);
        http_state_machine_destruct(&task->sm);
        free(task->server);
        free(task);
        return;
    }
    else if (r == 0) {
        fprintf(stderr, "[Error] Server %s terminated connection\n", task->server->uri.hostname);

        fail_server_connection(*task->server, bad_gateway_response, bad_gateway_response_size);
        aio_scheduler_cancel_all(task->task.fd);
        close(task->task.fd);
        http_state_machine_destruct(&task->sm);
        free(task->server);
        free(task);
        return;
    }

    http_state_machine_feed(&task->sm, (size_t) r);
    while (http_state_machine_step(&task->sm)) {
        switch (task->sm.state) {
            case MALFORMED:
                fprintf(stderr, "[Error] Server resp malformed\n");
                fail_server_connection(*task->server, bad_gateway_response, bad_gateway_response_size);
                http_state_machine_destruct(&task->sm);
                aio_scheduler_cancel_all(task->task.fd);
                close(task->task.fd);
                free(task->server);
                free(task);
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

    void *buffer;
    size_t size;
    http_state_machine_alloc(&task->sm, &buffer, &size);
    task->task = (task_t)
                 {
                     .type = READ_REQUEST,
                     .fd = task->task.fd,
                     .buffer = buffer,
                     .size = size,
                     .data = task,
                     .callback = analyze_response_callback
                 };
    aio_scheduler_schedule((task_t*) task, false);
}
