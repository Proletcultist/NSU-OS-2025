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
        fprintf(stderr, "[Error] Error while trying to read from %s: %s\n", task->client->client_ip, strerror(err));

        close(task->task.fd);
        http_state_machine_destruct(&task->sm);
        free(task->client);
        free(task);
        return;
    }
    else if (r == 0) {
        fprintf(stderr, "[Error] Client %s terminated connection\n", task->client->client_ip);

        close(task->task.fd);
        http_state_machine_destruct(&task->sm);
        free(task->client);
        free(task);
        return;
    }

    task->bytes_received += (size_t) r;
    if (task->bytes_received > MAX_HEADERS_SIZE) {
        schedule_error_response(task->task.fd, bad_request_response, bad_request_response_size);
        http_state_machine_destruct(&task->sm);
        free(task->client);
        free(task);
        return;
    }

    http_state_machine_feed(&task->sm, (size_t) r);
    while (http_state_machine_step(&task->sm)) {
        switch (task->sm.state) {
            case MALFORMED:
                schedule_error_response(task->task.fd, bad_request_response, bad_request_response_size);
                http_state_machine_destruct(&task->sm);
                free(task->client);
                free(task);
                return;
            case READ_REQUEST_LINE:
                fprintf(stderr, "[Info] %s Parsed hostname: \"%s\" port: \"%s\", path: \"%s\"\n", task->client->client_ip, task->sm.uri.hostname, task->sm.uri.port, task->sm.uri.path);
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

                fprintf(stderr, "[Info] %s Field-name: \"", task->client->client_ip);
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
                if (task->bad_request) {
                    schedule_error_response(task->task.fd, task->msg, task->msg_size);
                }
                else {
                    cache_entry_t *entry = cache_lookup(task->sm.uri);
                    if (entry == NULL) {
                        fprintf(stderr, "[Info] %s Cache miss for %s\n", task->client->client_ip, task->sm.uri.hostname);

                        cache_entry_t *new_entry = malloc(sizeof(cache_entry_t));
                        *new_entry = CACHE_ENTRY_INITIALIZER;
                        cache_entry_add_pending(new_entry, task->task.fd);
                        cache_enchache(task->sm.uri, new_entry);

                        establish_connect_with_server(task->sm.uri, new_entry);

                        // Delete uri from state machine, so it will not deallocate it
                        task->sm.uri.buffer = NULL;
                    }
                    else {
                        fprintf(stderr, "[Info] %s Cache hit for %s\n", task->client->client_ip, task->sm.uri.hostname);
                    }
                }

                http_state_machine_destruct(&task->sm);
                free(task->client);
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
        free(task->client);
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

