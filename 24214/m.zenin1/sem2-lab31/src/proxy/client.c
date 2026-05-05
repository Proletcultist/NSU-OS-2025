#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include "scheduler/aio_scheduler.h"
#include "http.h"
#include "proxy/util.h"
#include "proxy/client.h"
#include "proxy/server.h"
#include "proxy/responses.h"
#include "cache/cache.h"

void respond_error_callback(ssize_t r, int err, void *udata) {
    task_t *task = udata;

    // Check for errors or connection closed
    if (r < 0) {
        fprintf(stderr, "[Error] Error while trying to write: %s\n", strerror(err));
    }
    else if (r == 0) {
        fprintf(stderr, "[Error] Client terminated connection\n");
    }

    close(task->fd);
    free(task);
}

void analyze_request_callback(ssize_t r, int err, void *udata) {
    request_analysis_task_t *task = udata;

    // Check for errors or connection closed
    if (r < 0) {
        fprintf(stderr, "[Error] Error while trying to read from %s: %s\n", task->client_ip, strerror(err));

        close(task->task.fd);
        http_state_machine_destruct(&task->sm);
        free(task);
        return;
    }
    else if (r == 0) {
        fprintf(stderr, "[Error] Client %s terminated connection\n", task->client_ip);

        close(task->task.fd);
        http_state_machine_destruct(&task->sm);
        free(task);
        return;
    }

    task->bytes_received += (size_t) r;
    if (task->bytes_received > MAX_HEADERS_SIZE) {
        schedule_error_response(task->task.fd, bad_request_response, bad_request_response_size);
        http_state_machine_destruct(&task->sm);
        free(task);
        return;
    }

    http_state_machine_feed(&task->sm, (size_t) r);
    while (http_state_machine_step(&task->sm)) {
        switch (task->sm.state) {
            case MALFORMED:
                schedule_error_response(task->task.fd, bad_request_response, bad_request_response_size);
                http_state_machine_destruct(&task->sm);
                free(task);
                return;
            case READ_REQUEST_LINE:
                fprintf(stderr, "[Info] %s Parsed hostname: \"%s\" port: \"%s\", path: \"%s\"\n", task->client_ip, task->sm.uri.hostname, task->sm.uri.port, task->sm.uri.path);
                switch (task->sm.method) {
                    case POST:
                    case HEAD:
                        task->bad_request = true;
                        task->msg = method_not_allowed;
                        task->msg_size = method_not_allowed_size;
                        break;
                    case UNKNOWN_METHOD:
                        task->bad_request = true;
                        task->msg = method_not_implemented_response;
                        task->msg_size = method_not_implemented_response_size;
                        break;
                    case GET:
                        break;
                }
                if (task->sm.version != HTTP_1_0) {
                    task->bad_request = true;
                    task->msg = version_not_supported_response;
                    task->msg_size = version_not_supported_response_size;
                }
                break;
            case HEADER_AVAILABLE:
                char *name, *value;
                size_t name_size, value_size;
                http_state_machine_get_header_name(&task->sm, task->sm.last_header, &name, &name_size);
                http_state_machine_get_header_value(&task->sm, task->sm.last_header, &value, &value_size);

                fprintf(stderr, "[Info] %s Field-name: \"", task->client_ip);
                for (size_t i = 0; i < name_size; i++) {
                    fprintf(stderr, "%c", name[i]);
                }
                fprintf(stderr, "\" Field-value: \"");
                for (size_t i = 0; i < value_size; i++) {
                    fprintf(stderr, "%c", value[i]);
                }
                fprintf(stderr, "\"\n");

                if (memcmp(name, "Content-Length", MIN(name_size, 14)) == 0 && 
                    !mem_compare_trimed(value, value_size, "0", 1)) {
                    task->bad_request = true;
                    task->msg = bad_request_response;
                    task->msg_size = bad_request_response_size;
                }
                break;
            case COMPLETE:
                fprintf(stderr, "[Info] %s COMPLETE\n", task->client_ip);

                if (task->bad_request) {
                    schedule_error_response(task->task.fd, task->msg, task->msg_size);
                }
                else {
                    cache_entry_t *entry = cache_lookup(task->sm.uri);
                    if (entry == NULL) {
                        fprintf(stderr, "[Info] %s Cache miss for %s\n", task->client_ip, task->sm.uri.hostname);

                        cache_entry_t *new_entry = malloc(sizeof(cache_entry_t));
                        *new_entry = CACHE_ENTRY_INITIALIZER;
                        cache_entry_add_pending(new_entry, task->task.fd);
                        cache_enchache(task->sm.uri, new_entry);

                        // Delete uri from state machine, so it will not deallocate it
                        task->sm.uri.buffer = NULL;

                        char *port = *task->sm.uri.port == '\0' ? "80" : task->sm.uri.port;
                        struct addrinfo *res = resolve_address(task->sm.uri.hostname, port);

                        int server = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
                        err = connect(server, res->ai_addr, res->ai_addrlen);
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
                                                .first = res,
                                                .next_try = res->ai_next
                                            };
                            aio_scheduler_schedule((task_t*) connect_task, true);
                        }
                        else if (err < 0) {
                            // TODO: Handle
                        }

                        // Schedule request writing
                        char *req_buffer;
                        size_t req_size;
                        generate_request(&req_buffer, &req_size, task->sm.uri);
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
                        read_res_task->uri = task->sm.uri;
                        read_res_task->cache_entry = new_entry;
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
                    else {
                        fprintf(stderr, "[Info] %s Cache hit for %s\n", task->client_ip, task->sm.uri.hostname);
                    }
                }

                http_state_machine_destruct(&task->sm);
                free(task);
                return;
            case READ_STATUS_LINE:
            case READING_STATUS_LINE:
                // Unreachable
                break;
            case READING_REQUEST_LINE:
            case READING_HEADER:
                break;
        }
    }

    // Line is too big
    if (task->sm.data.size - task->sm.analyzed > MAX_LINE_SIZE) {
        schedule_error_response(task->task.fd, bad_request_response, bad_request_response_size);
        http_state_machine_destruct(&task->sm);
        free(task);
        return;
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
                     .callback = analyze_request_callback
                 };
    aio_scheduler_schedule((task_t*) task, false);
}

