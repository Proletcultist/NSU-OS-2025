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

    int close_err;
    do {
        close_err = close(task->client->fd);
    } while (close_err < 0 && errno == EINTR);

    if (task->client->health_check_timer != NULL) {
        task->client->health_check_timer->client = NULL;
    }
    if (task->client->entry != NULL) {
        cache_entry_put(task->client->entry);
    }
    free(task->client);
    free(task);
}

void client_respond_error_callback(ssize_t r, int err, void *udata) {
    client_task_t *task = udata;
    if ((r < 0 && (err == ECANCELED || err == EINVAL)) || task->client->state == CLIENT_DISCONNECTED) {
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

    task->client->health_check_timer->last_update = task->client->sched->loop_time;

    task->task = (task_t) {
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
    task->task = (task_t) {
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

void client_delegate_callback(int err, void *udata) {
    client_task_t *task = udata;

    // If delegate fails - cleanup
    if (err != 0) {
        client_cleanup_callback(0, task);
    }
    else {
        free(task);
    }
}

void client_respond_error(client_task_t *task, char *msg, size_t msg_size) {
    task->task = (task_t) {
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

void client_write_cached_last_callback(ssize_t r, int err, void *udata) {
    client_task_t *task = udata;

    if ((r < 0 && (err == ECANCELED || err == EINVAL)) || task->client->state == CLIENT_DISCONNECTED) {
        free(task);
        return;
    }

    task->client->health_check_timer->last_update = task->client->sched->loop_time;

    client_silent_disconnect(task);
}

void client_write_cached_callback(ssize_t r, int err, void *udata) {
    client_task_t *task = udata;

    if (r <= 0) {
        task->client->state = CLIENT_DISCONNECTED;
    }

    task->client->health_check_timer->last_update = task->client->sched->loop_time;

    free(task);
}

static void read_cache_callback(ssize_t r, int err, void *udata) {
    client_read_cache_task_t *task = udata;

    if ((r < 0 && (err == ECANCELED || err == EINVAL)) || task->client->state != CLIENT_READING_CACHED) {
        free(task);
        return;
    }

    if (r < 0) {
        fprintf(stderr, "[Error] Error while trying to write to %s: %s\n", task->client->client_ip, strerror(err));
    }
    else if (r == 0) {
        fprintf(stderr, "[Error] Client %s terminated connection\n", task->client->client_ip);
    }
    if (r <= 0) {
        client_silent_disconnect((client_task_t*) task);
        return;
    }

    task->client->health_check_timer->last_update = task->client->sched->loop_time;

    char *buffer = get_cache_block_buffer(task->current_block);
    size_t readen = (size_t) r + (size_t) ((char*) task->task.attrs.io.buffer - buffer);
    if (readen < task->current_block->size) {
        // There is more data in the same block
        task->task.attrs.io.buffer = buffer + readen;
        task->task.attrs.io.size = task->current_block->size - readen;
        aio_scheduler_schedule(task->client->sched, (task_t*) task);
    }
    else if (task->current_block->finished && task->current_block->next != NULL && task->current_block->next->size > 0) {
        // Block is finished, but there is next with data
        task->current_block = task->current_block->next;
        task->task.attrs.io.buffer = get_cache_block_buffer(task->current_block);
        task->task.attrs.io.size = task->current_block->size;
        aio_scheduler_schedule(task->client->sched, (task_t*) task);
    }
    else if (task->current_block->finished && task->current_block->next == NULL) {
        // Block is finished and there is no next block - close connection
        client_silent_disconnect((client_task_t*) task);   
    }
    else {
        // No data available - add to pending
        task->client->state = CLIENT_WAITS_FOR_DATA;
        // Disable client cleanuping - only server connect can add tasks to client from now on
        task->client->health_check_timer->cleanup_client = false;
        cache_entry_add_pending(task->client->entry, task->client);
        free(task);
    }
}

void client_health_check_callback(int err, time_t time, void *udata) {
    client_health_check_timer_t *timer = udata;

    // If there is no client linked with this timer or it is disconnected or timer canceled
    // destroy the timer
    if (timer->client == NULL) {
        free(timer);
        return;
    }
    else if (err == ENOMEM) {
        timer->client->health_check_timer = NULL;
        if (timer->client->state != CLIENT_DISCONNECTED) {
            client_silent_disconnect((client_task_t*) timer);
        }
        else {
            free(timer);
        }
        return;
    }
    else if (err == ECANCELED || err == EINVAL || timer->client->state == CLIENT_DISCONNECTED) {
        timer->client->health_check_timer = NULL;
        free(timer);
        return;
    }
    // If client is waiting for data from server - just restart teh timer
    else if (timer->client->state == CLIENT_WAITS_FOR_DATA) {
        timer->task.attrs.timer.time = CLIENT_TIMEOUT;
        aio_scheduler_schedule(timer->client->sched, (task_t*) timer);
        return;
    }


    time_t next_check_time = timer->last_update + CLIENT_TIMEOUT;
    // If time ellapsed - disconnect client
    if (time - timer->last_update >= timer->task.attrs.timer.time || next_check_time < time) {
        timer->client->health_check_timer = NULL;
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
        timer->task.attrs.timer.time = next_check_time - time;
        aio_scheduler_schedule(timer->client->sched, (task_t*) timer);
    }
}

void process_request_callback(ssize_t r, int err, void *udata) {
    process_request_task_t *task = udata;

    if ((r < 0 && (err == ECANCELED || err == EINVAL)) || task->client->state != CLIENT_SENDING_REQUEST) {
        http_state_machine_destruct(&task->sm);
        free(task);
        return;
    }

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

    task->client->health_check_timer->last_update = task->client->sched->loop_time;

    // Request is too big
    task->bytes_received += (size_t) r;
    if (task->bytes_received > MAX_HEADERS_SIZE) {
        http_state_machine_destruct(&task->sm);
        client_respond_error((client_task_t*) task, bad_request_response, bad_request_response_size);
        return;
    }

    http_state_machine_feed(&task->sm, (size_t) r);
    while (http_state_machine_step(&task->sm)) {
        char *name, *value;
        size_t name_size, value_size;
        switch (task->sm.state) {
            case MALFORMED:
                http_state_machine_destruct(&task->sm);
                client_respond_error((client_task_t*) task, bad_request_response, bad_request_response_size);
                return;
            case READ_REQUEST_LINE:
                if (task->sm.uri.buffer == NULL) {
                    http_state_machine_destruct(&task->sm);
                    client_respond_error((client_task_t*) task, internal_server_error_response, internal_server_error_response_size);
                    return;
                }

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

                if (ci_memcmp(name, "Content-Length", MIN(name_size, 14)) && 
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
                    cache_entry_t *new_entry = malloc(sizeof(cache_entry_t));
                    cache_block_external_t *head_block = malloc(sizeof(cache_block_external_t));
                    if (head_block == NULL || new_entry == NULL) {
                        free(head_block);
                        free(new_entry);
                        http_state_machine_destruct(&task->sm);
                        client_respond_error((client_task_t*) task, internal_server_error_response, internal_server_error_response_size);
                        return;
                    }

                    *head_block = (cache_block_external_t) {
                        .type = EXTERNAL_CACHE_BLOCK,
                        .size = 0,
                        .cap = 0,
                        .finished = false,
                        .data = NULL
                    };

                    *new_entry = CACHE_ENTRY_INITIALIZER;
                    new_entry->references = 1;
                    new_entry->uri = task->sm.uri;
                    // Delete uri from state machine, so it will not deallocate it
                    task->sm.uri.buffer = NULL;
                    cache_entry_add_block(new_entry, (cache_block_t*) head_block);
                    cache_entry_add_pending(new_entry, task->client);

                    http_state_machine_destruct(&task->sm);

                    task->client->entry = cache_encache_or_get_ref(task->sm.uri, new_entry);

                    if (task->client->entry == NULL) {
                        // There is no such cache entry, and adding one had failed
                        cache_entry_put(new_entry);

                        client_respond_error((client_task_t*) task, internal_server_error_response, internal_server_error_response_size);
                    }
                    else if (task->client->entry == new_entry) {
                        fprintf(stderr, "[Info] %s Cache miss for %s\n", task->client->client_ip, task->sm.uri.hostname);

                        task->client->state = CLIENT_WAITS_FOR_DATA;
                        // Disable client cleanuping - only server connect can add tasks to client from now on
                        task->client->health_check_timer->cleanup_client = false;

                        establish_connect_with_server(task->client->sched, task->client->entry);

                        free(task);
                    }
                    else {
                        cache_entry_put(new_entry);
                        
                        fprintf(stderr, "[Info] %s Cache hit for %s\n", task->client->client_ip, task->client->entry->uri.hostname);

                        if (task->client->entry->first_block != NULL && task->client->entry->first_block->size > 0) {
                            // If there is data in first block - read it
                            task->client->state = CLIENT_READING_CACHED;

                            void *buffer = get_cache_block_buffer(task->client->entry->first_block);

                            client_read_cache_task_t *cache_task = malloc(sizeof(client_read_cache_task_t));
                            if (cache_task == NULL) {
                                client_respond_error((client_task_t*) task, internal_server_error_response, internal_server_error_response_size);
                                return;
                            }

                            *cache_task = (client_read_cache_task_t) {
                                .task.type = WRITE_REQUEST,
                                .task.attrs.io = {
                                    .fd = task->client->fd,
                                    .as_first = false,
                                    .buffer = buffer,
                                    .size = task->client->entry->first_block->size,
                                    .data = cache_task,
                                    .callback = read_cache_callback
                                },
                                .client = task->client,
                                .current_block = task->client->entry->first_block
                            };
                            aio_scheduler_schedule(task->client->sched, (task_t*) cache_task);
                            free(task);
                        }
                        else if (task->client->entry->first_block != NULL &&
                                 task->client->entry->first_block->finished) {
                            // Block is finished and empty - close connection (adnormal situation)
                            client_silent_disconnect((client_task_t*) task);   
                        }
                        else {
                            // No data available - add to pending
                            task->client->state = CLIENT_WAITS_FOR_DATA;
                            // Disable client cleanuping - only server connect can add tasks to client from now on
                            task->client->health_check_timer->cleanup_client = false;
                            cache_entry_add_pending(task->client->entry, task->client);
                            free(task);
                        }
                    }
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

    if (http_state_machine_alloc(&task->sm, &task->task.attrs.io.buffer, &task->task.attrs.io.size)) {
        http_state_machine_destruct(&task->sm);
        client_respond_error((client_task_t*) task, internal_server_error_response, internal_server_error_response_size);
        return;
    }
    aio_scheduler_schedule(task->client->sched, (task_t*) task);
}

