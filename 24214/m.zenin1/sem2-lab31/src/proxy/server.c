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

void establish_connect_with_server(uri_t uri, cache_entry_t *entry) {
    char *port = *uri.port == '\0' ? "80" : uri.port;
    struct addrinfo *res = resolve_address(uri.hostname, port);
    if (res == NULL) {
        cache_delete(uri);
        for (pending_client_t *cursor = entry->pending; cursor != NULL; cursor = cursor->next) {
            schedule_error_response(cursor->fd, not_found_response, not_found_response_size);
        }
        return;
    }

    int server = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

    struct addrinfo *current_try = res;
    int err = -1;
    do {
        err = connect(server, current_try->ai_addr, current_try->ai_addrlen);

        // Connection in progress
        if (err < 0 && (errno == EINPROGRESS || errno == EAGAIN)) {
            try_connect_to_server_task_t *connect_task = malloc(sizeof(try_connect_to_server_task_t));
            *connect_task = (try_connect_to_server_task_t)
                            {
                                .task = (task_t)
                                        {
                                            .type = WAIT_FOR_CONNECTION,
                                            .fd = server,
                                            .data = connect_task,
                                            .callback = try_connect_callback
                                        },
                                .uri = uri,
                                .first = res,
                                .next_try = current_try->ai_next
                            };
            aio_scheduler_schedule((task_t*) connect_task, true);
            break;
        }

        current_try = current_try->ai_next;
    } while (err < 0 && current_try != NULL);
    if (err < 0 && current_try == NULL) {
        cache_delete(uri);
        for (pending_client_t *cursor = entry->pending; cursor != NULL; cursor = cursor->next) {
            // TODO: Send error
            // schedule_error_response(cursor->fd, not_found_response, not_found_response_size);
        }
        fprintf(stderr, "[Error] Failed to connect to %s: %s\n", uri.hostname, strerror(errno));
        freeaddrinfo(res);
        close(server);
        return;
    }
    else if (err >= 0) {
        fprintf(stderr, "[Info] Connected to %s successfully\n", uri.hostname);
    }

    // Schedule request writing
    char *req_buffer;
    size_t req_size;
    generate_request(&req_buffer, &req_size, uri);
    task_t *write_req_task = malloc(sizeof(task_t));
    *write_req_task = (task_t)
                      {
                          .type = WRITE_REQUEST,
                          .fd = server,
                          .buffer = req_buffer,
                          .size = req_size,
                          .data = write_req_task,
                          .callback = write_request_callback
                      };
    aio_scheduler_schedule(write_req_task, false);

    // Schedule response reading
    response_analysis_task_t *read_res_task = malloc(sizeof(response_analysis_task_t));
    read_res_task->uri = uri;
    read_res_task->cache_entry = entry;
    read_res_task->sm = HTTP_STATE_MACHINE_RES_INITIALIZER;
    // read_res_task->sm.state = 
    void *resp_buffer;
    size_t resp_size;
    http_state_machine_alloc(&read_res_task->sm, &resp_buffer, &resp_size);
    read_res_task->task = (task_t)
                          {
                              .type = READ_REQUEST,
                              .fd = server,
                              .buffer = resp_buffer,
                              .size = resp_size,
                              .data = read_res_task,
                              .callback = analyze_response_callback
                          };

    aio_scheduler_schedule((task_t*) read_res_task, false);
}

void try_connect_callback(ssize_t r, int err, void *udata) {
    try_connect_to_server_task_t *task = udata;

    socklen_t len = sizeof(err);
    getsockopt(task->task.fd, SOL_SOCKET, SO_ERROR, &err, &len);

    if (err == 0) {
        fprintf(stderr, "[Info] Connected to %s successfully\n", task->uri.hostname);
        freeaddrinfo(task->first);
        free(task);
    }
    else {
        struct addrinfo *current_try = task->next_try;
        while (err < 0 && current_try != NULL) {
            err = connect(task->task.fd, current_try->ai_addr, current_try->ai_addrlen);

            // Connection in progress
            if (err < 0 && (errno == EINPROGRESS || errno == EAGAIN)) {
                task->next_try = current_try->ai_next;
                aio_scheduler_schedule((task_t*) task, true);
                break;
            }

            current_try = current_try->ai_next;
        }
        if (err < 0 && current_try == NULL) {
            fprintf(stderr, "[Error] Failed to connect to %s\n", task->uri.hostname);
            freeaddrinfo(task->first);
            aio_scheduler_cancel_all(task->task.fd);
            close(task->task.fd);
            free(task);
            return;
        }
    }
}

void write_request_callback(ssize_t w, int err, void *udata) {
    task_t *task = udata;

    // Check for errors or connection closed
    if (w < 0) {
        fprintf(stderr, "[Error] Error while trying to write: %s\n", strerror(err));
        aio_scheduler_cancel_all(task->fd);
        close(task->fd);
        // TODO: Write error to all clients
    }
    else if (w == 0) {
        fprintf(stderr, "[Error] Server terminated connection\n");
        aio_scheduler_cancel_all(task->fd);
        close(task->fd);
        // TODO: Write error to all clients
    }

    free(task);
}

void analyze_response_callback(ssize_t r, int err, void *udata) {
    response_analysis_task_t *task = udata;

    // Check for errors or connection closed
    if (r < 0) {
        fprintf(stderr, "[Error] Error while trying to read from %s: %s\n", task->uri.hostname, strerror(err));

        aio_scheduler_cancel_all(task->task.fd);
        close(task->task.fd);
        http_state_machine_destruct(&task->sm);
        free(task);
        // TODO: Write error to all clients
        return;
    }
    else if (r == 0) {
        fprintf(stderr, "[Error] Server %s terminated connection\n", task->uri.hostname);

        aio_scheduler_cancel_all(task->task.fd);
        close(task->task.fd);
        http_state_machine_destruct(&task->sm);
        free(task);
        // TODO: Write error to all clients
        return;
    }

    http_state_machine_feed(&task->sm, (size_t) r);
    while (http_state_machine_step(&task->sm)) {
        switch (task->sm.state) {
            case MALFORMED:
                // TODO: Schedule write response to clients
                fprintf(stderr, "[Error] Server resp malformed\n");
                http_state_machine_destruct(&task->sm);
                free(task);
                return;
            case READ_STATUS_LINE:
                // TODO: Do smth with status line
                break;
            case HEADER_AVAILABLE:
                char *name, *value;
                size_t name_size, value_size;
                http_state_machine_get_header_name(&task->sm, task->sm.last_header, &name, &name_size);
                http_state_machine_get_header_value(&task->sm, task->sm.last_header, &value, &value_size);

                fprintf(stderr, "[Info] %s Field-name: \"", task->uri.hostname);
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
                // TODO: Unreachable
                break;
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
