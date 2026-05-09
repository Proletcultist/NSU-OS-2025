#include <stdio.h>
#include <time.h>
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

static void client_cleanup_callback(int err, void *udata) {
    client_task_t *task = udata;

    close(task->client->fd);
    if (task->client->health_check_timer != NULL) {
        task->client->health_check_timer->client = NULL;
    }
    free(task->client);
    free(task);
}

void client_respond_error_callback(ssize_t r, int err, void *udata) {
    client_task_t *task = udata;
    if (task->client->state == CLIENT_DISCONNECTED) {
        free(task);
        return;
    }

    // Check for errors or connection closed
    if (r < 0) {
        fprintf(stderr, "[Error] Error while trying to write: %s\n", strerror(err));
    }
    else if (r == 0) {
        fprintf(stderr, "[Error] Client terminated connection\n");
    }

    task->task = (task_t)
                 {
                     .type = UNDELEGATE,
                     .attrs.ctl = {
                         .fd = task->client->fd,
                         .data = task,
                         .callback = client_cleanup_callback
                     }
                 };
    aio_scheduler_schedule(task->client->sched, (task_t*) task);

    task->client->state = CLIENT_DISCONNECTED;
}

void client_silent_disconnect(client_task_t *task) {
    task->task = (task_t)
                 {
                     .type = UNDELEGATE,
                     .attrs.ctl = {
                         .fd = task->client->fd,
                         .data = task,
                         .callback = client_cleanup_callback
                     }
                 };
    aio_scheduler_schedule(task->client->sched, (task_t*) task);

    task->client->state = CLIENT_DISCONNECTED;
}

void client_respond_error(client_task_t *task, char *msg, size_t msg_size) {
    task->task = (task_t)
                 {
                     .type = WRITE_REQUEST,
                     .attrs.io = {
                         .as_first = false,
                         .fd = task->client->fd,
                         .buffer = msg,
                         .size = msg_size,
                         .data = task,
                         .callback = client_respond_error_callback
                     }
                 };

    aio_scheduler_schedule(task->client->sched, (task_t*) task);

    task->client->state = CLIENT_DISCONNECTING;
}

void client_health_check_callback(time_t time, void *udata) {
    client_health_check_timer_t *timer = udata;

    // If client is already freed, started waiting for data from server or
    // disconnected - turn of teh timer
    if (timer->client == NULL || timer->client->state == CLIENT_DISCONNECTED) {
        free(timer);
        return;
    }

    // If time ellapsed - disconnect client
    if (time - timer->last_update >= timer->task.attrs.timer.time) {
        if (timer->cleanup_client) {
            client_silent_disconnect((client_task_t*) timer);
        }
        else {
            timer->client->state = CLIENT_DISCONNECTED;
            free(timer);
        }
        return;
    }
    // Schedule next timer
    else {
        time_t next_check_time = 0;
        switch (timer->client->state) {
            case CLIENT_SENDING_REQUEST:
                next_check_time = timer->last_update + CLIENT_SEND_REQUEST_TIMEOUT;
                break;
            case CLIENT_READING_CACHED:
                next_check_time = timer->last_update + CLIENT_READ_CACHED_TIMEOUT;
                break;
            case CLIENT_WAITS_FOR_DATA:
                next_check_time = timer->last_update + CLIENT_WAIT_FOR_DATA_TIMEOUT;
                break;
            case CLIENT_DISCONNECTING:
                next_check_time = timer->last_update + CLIENT_DISCONNECTION_TIMEOUT;
                break;
            case CLIENT_DISCONNECTED:
                // Unreachable
                return;
        }

        if (next_check_time < time) {
            if (timer->cleanup_client) {
                client_silent_disconnect((client_task_t*) timer);
            }
            else {
                timer->client->state = CLIENT_DISCONNECTED;
                free(timer);
            }
        }
        else {
            timer->task.attrs.timer.time = next_check_time - time;
            aio_scheduler_schedule(timer->client->sched, (task_t*) timer);
        }
    }
}

void process_request_callback(ssize_t r, int err, void *udata) {
    process_request_task_t *task = udata;

    if (task->client->state != CLIENT_SENDING_REQUEST) {
        http_state_machine_destruct(&task->sm);
        free(task);
        return;
    }

    task->client->health_check_timer->last_update = task->client->sched->loop_time;

    // Check for errors or connection closed
    if (r < 0) {
        fprintf(stderr, "[Error] Error while trying to read from %s: %s\n", task->client->client_ip, strerror(err));
    }
    else if (r == 0) {
        fprintf(stderr, "[Error] Client %s terminated connection\n", task->client->client_ip);
    }
    if (r <= 0) {
        http_state_machine_destruct(&task->sm);
        client_silent_disconnect((client_task_t*) task);
        return;
    }

    // Request is too big
    task->bytes_received += (size_t) r;
    if (task->bytes_received > MAX_HEADERS_SIZE) {
        http_state_machine_destruct(&task->sm);
        client_respond_error((client_task_t*) task, bad_request_response, bad_request_response_size);
        return;
    }

    http_state_machine_feed(&task->sm, (size_t) r);
    while (http_state_machine_step(&task->sm)) {
        switch (task->sm.state) {
            case MALFORMED:
                http_state_machine_destruct(&task->sm);
                client_respond_error((client_task_t*) task, bad_request_response, bad_request_response_size);
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
                    http_state_machine_destruct(&task->sm);
                    client_respond_error((client_task_t*) task, task->msg, task->msg_size);
                }
                else {
                    cache_entry_t *entry = cache_lookup(task->sm.uri);
                    if (entry == NULL) {
                        fprintf(stderr, "[Info] %s Cache miss for %s\n", task->client->client_ip, task->sm.uri.hostname);

                        // Start waiting for data from server and forget about timer - it will be cleaned up
                        task->client->state = CLIENT_WAITS_FOR_DATA;
                        task->client->health_check_timer->client = NULL;
                        task->client->health_check_timer = NULL;

                        cache_entry_t *new_entry = malloc(sizeof(cache_entry_t));
                        *new_entry = CACHE_ENTRY_INITIALIZER;
                        cache_entry_add_pending(new_entry, task->client);
                        cache_enchache(task->sm.uri, new_entry);

                        establish_connect_with_server(task->client->sched, task->sm.uri, new_entry);

                        // Delete uri from state machine, so it will not deallocate it
                        task->sm.uri.buffer = NULL;
                    }
                    else {
                        fprintf(stderr, "[Info] %s Cache hit for %s\n", task->client->client_ip, task->sm.uri.hostname);
                        // TODO: Suck all the cache, also, set state
                    }
                    http_state_machine_destruct(&task->sm);
                    free(task);
                }
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
        http_state_machine_destruct(&task->sm);
        client_respond_error((client_task_t*) task, bad_request_response, bad_request_response_size);
        return;
    }

    http_state_machine_alloc(&task->sm, &task->task.attrs.io.buffer, &task->task.attrs.io.size);
    aio_scheduler_schedule(task->client->sched, (task_t*) task);
}

