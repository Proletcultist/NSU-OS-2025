#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdatomic.h>
#include "scheduler/aio_scheduler.h"
#include "cache/cache.h"
#include "proxy/server.h"
#include "proxy/util.h"
#include "proxy/client.h"
#include "proxy/responses.h"

#define DATA_CHUNK_SIZE 1024

static void write_next_cache_block(server_task_t *task);

static void server_cleanup_callback(int err, void *udata) {
    server_task_t *task = udata;

    int close_err;
    do {
        close_err = close(task->server->fd);
    } while (close_err < 0 && errno == EINTR);

    if (task->server->health_check_timer != NULL) {
        task->server->health_check_timer->server = NULL;
    }
    cache_entry_put(task->server->cache_entry);
    free(task->server);
    free(task);
}

static void cache_entry_postprocess(aio_scheduler_t *sched, cache_entry_t *entry) {
    // If entry successfully commited to cache - start expiration timer
    if (commit_entry(entry)) {
        cache_expire_timer_t *timer = malloc(sizeof(cache_expire_timer_t));
        if (timer == NULL) {
            panic("Out of memory");
        }

        *timer = (cache_expire_timer_t) {
            .task.type = ADD_TIMER,
            .task.attrs.timer = {
                .time = CACHE_EXPIRATION_TIME,
                .data = timer,
                .callback = cache_expired_callback
            },
            .entry = entry
        };
        aio_scheduler_schedule(sched, (task_t*) timer);
    }
}

static void server_delegate_callback(int err, void *udata) {
    server_task_t *task = udata;

    if (err != 0) {
        panic("Out of memory");
    }
    else {
        free(task);
    }
}

static bool try_cleanup(proxy_client_t *client) {
    if (client->state != CLIENT_DISCONNECTED) {
        return false;
    }

    client_task_t *task = malloc(sizeof(client_task_t));
    if (task == NULL) {
        panic("Out of memory");
    }

    task->client = client;
    client_silent_disconnect(task);

    return true;
}

static void send_task_to_client(proxy_server_t *server, client_task_t *task) {
    // If client is newbie - change his state and update the timer
    if (task->client->state == CLIENT_WAITS_FOR_DATA) {
        task->client->health_check_timer->last_update = server->sched->loop_time;
        task->client->state = CLIENT_RECEIVING_SERVER_DATA;
    }

    aio_scheduler_schedule(task->client->sched, (task_t*) task);
}

// Go through linked list of clients, cleanup disconnected, send data to connected
// Move *clients to the new start of linked list
// Return last client in list
static proxy_client_t* send_cached_to_all_clients(proxy_server_t *server, proxy_client_t **clients, char *buffer, size_t size, bool last) {
    void (*callback)(ssize_t, int, void*);
    if (last) {
        callback = client_write_cached_last_callback;
    }
    else {
        callback = client_write_cached_callback;
    }

    proxy_client_t sentinel;
    sentinel.next = *clients;

    proxy_client_t *prev = &sentinel;
    proxy_client_t *cursor = sentinel.next;
    while (cursor != NULL) {
        proxy_client_t *next = cursor->next;

        if (try_cleanup(cursor)) {
            prev->next = next;
            cursor = next;
            continue;
        }

        client_task_t *task = malloc(sizeof(client_task_t));
        if (task == NULL) {
            panic("Out of memory");
        }

        *task = (client_task_t) {
            .task.type = WRITE_REQUEST,
            .task.attrs.io = {
                .fd = cursor->fd,
                .as_first = false,
                .buffer = buffer,
                .size = size,
                .data = task,
                .callback = callback
            },
            .client = cursor
        };
        send_task_to_client(server, task);

        if (last) {
            prev->next = next;
            cursor = next;
        }
        else {
            prev = cursor;
            cursor = next;
        }
    }

    *clients = sentinel.next;
    return prev == &sentinel ? NULL : prev;
}

static void close_server_connection(server_task_t *task) {
    task->server->state = SERVER_DISCONNECTED;
    task->task = (task_t) {
        .type = UNDELEGATE,
        .attrs.ctl = {
            .fd = task->server->fd,
            .data = task,
            .callback = server_cleanup_callback
        }
    };
    aio_scheduler_schedule(task->server->sched, (task_t*) task);
}

// Msg must be not null if cache entry is empty of data
static void early_fail_server_connection(proxy_server_t server, char *msg, size_t msg_size) {
    cache_delete(server.cache_entry->uri);

    pthread_mutex_lock(&server.cache_entry->mtx);
    if (msg != NULL) {
        *((cache_block_external_t*) server.cache_entry->first_block) = (cache_block_external_t) {
            .type = STATIC_EXTERNAL_CACHE_BLOCK,
            .next = NULL,
            .size = msg_size,
            .cap = msg_size,
            .finished = true,
            .data = msg
        };
    }
    server.cache_entry->last_block->finished = true;
    pthread_mutex_unlock(&server.cache_entry->mtx);

    send_cached_to_all_clients(&server, &server.cache_entry->pending, msg, msg_size, true);

    cache_entry_put(server.cache_entry);
}

// Msg must be not null if cache entry is empty of data
static void fail_server_connection(server_task_t *task, char *msg, size_t msg_size) {
    cache_delete(task->server->cache_entry->uri);

    pthread_mutex_lock(&task->server->cache_entry->mtx);
    if (msg != NULL) {
        *((cache_block_external_t*) task->server->cache_entry->first_block) = (cache_block_external_t) {
            .type = STATIC_EXTERNAL_CACHE_BLOCK,
            .next = NULL,
            .size = msg_size,
            .cap = msg_size,
            .finished = true,
            .data = msg
        };
    }
    task->server->cache_entry->last_block->finished = true;
    pthread_mutex_unlock(&task->server->cache_entry->mtx);

    send_cached_to_all_clients(task->server, &task->server->cache_entry->pending, msg, msg_size, true);

    close_server_connection(task);
}

static void server_health_check_callback(int err, time_t time, void *udata) {
    server_health_check_timer_t *timer = udata;

    if (timer->server == NULL) {
        free(timer);
        return;
    }
    else if (err == ENOMEM) {
        panic("Out of memory");
    }
    else if (err == ECANCELED || timer->server->state == SERVER_DISCONNECTED) {
        timer->server->health_check_timer = NULL;
        free(timer);
        return;
    }

    time_t next_check_time = timer->last_update + SERVER_TIMEOUT;
    // If time ellapsed - disconnect
    if (time - timer->last_update >= timer->task.attrs.timer.time || next_check_time < time) {
        timer->server->health_check_timer = NULL;
        // If we didn't start sending body to clients, send them error
        if (timer->server->state == SERVER_CONNECTION_IN_PROGRESS ||
            timer->server->state == SERVER_RECEIVING_HEADERS) {
            fail_server_connection((server_task_t*) timer, gateway_timeout_response, gateway_timeout_response_size);
        }
        // Interrupt response sending
        else if (timer->server->state == SERVER_RECEIVING_BODY) {
            fail_server_connection((server_task_t*) timer, NULL, 0);
        }
        else {
            // Unreachable
            timer->server->health_check_timer = NULL;
            free(timer);
        }
    }
    else {
        timer->task.attrs.timer.time = next_check_time - time;
        aio_scheduler_schedule(timer->server->sched, (task_t*) timer);
    }
}

void establish_connect_with_server(aio_scheduler_t *sched, cache_entry_t *entry) {
    atomic_fetch_add_explicit(&entry->references, 1, memory_order_relaxed);
    proxy_server_t server_val = {
        .state = SERVER_CONNECTION_IN_PROGRESS,
        .sched = sched,
        .has_content_length = false,
        .content_length = 0,
        .health_check_timer = NULL,
        .cache_entry = entry
    };

    char *port = *entry->uri.port == '\0' ? "80" : entry->uri.port;
    struct addrinfo *res = resolve_address(entry->uri.hostname, port);
    if (res == NULL) {
        early_fail_server_connection(server_val, not_found_response, not_found_response_size);
        return;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server_fd < 0) {
        freeaddrinfo(res);
        early_fail_server_connection(server_val, internal_server_error_response, internal_server_error_response_size);
        return;
    }

    server_val.fd = server_fd;
    proxy_server_t *server = malloc(sizeof(proxy_server_t));
    if (server == NULL) {
        panic("Out of memory");
    }

    *server = server_val;

    struct addrinfo *current_try = res;
    int err = -1;
    try_connect_to_server_task_t *connect_task = NULL;
    do {
        err = connect(server_fd, current_try->ai_addr, current_try->ai_addrlen);

        // Connection in progress
        if (err < 0 && (errno == EINPROGRESS || errno == EAGAIN)) {
            connect_task = malloc(sizeof(try_connect_to_server_task_t));
            if (connect_task == NULL) {
                panic("Out of memory");
            }

            *connect_task = (try_connect_to_server_task_t) {
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
            break;
        }

        current_try = current_try->ai_next;
    } while (err < 0 && current_try != NULL);
    if (err < 0 && current_try == NULL) {
        fprintf(stderr, "[Error] Failed to connect to %s: %s\n", server->cache_entry->uri.hostname, strerror(errno));
        early_fail_server_connection(*server, bad_gateway_response, bad_gateway_response_size);
        free(server);
        freeaddrinfo(res);

        int close_err;
        do {
            close_err = close(server_fd);
        } while (close_err < 0 && errno == EINTR);

        return;
    }
    else if (err >= 0) {
        fprintf(stderr, "[Info] Connected to %s successfully\n", server->cache_entry->uri.hostname);
        server->state = SERVER_RECEIVING_HEADERS;
        freeaddrinfo(res);
    }

    server_task_t *delegate_task = malloc(sizeof(server_task_t));
    if (delegate_task == NULL) {
        panic("Out of memory");
    }

    *delegate_task = (server_task_t) {
        .task.type = DELEGATE,
        .task.attrs.ctl = {
            .fd = server_fd,
            .data = delegate_task,
            .callback = server_delegate_callback
        },
        .server = server
    };

    server_health_check_timer_t *timer_task = malloc(sizeof(server_health_check_timer_t));
    if (timer_task == NULL) {
        panic("Out of memory");
    }

    *timer_task = (server_health_check_timer_t) {
        .task.type = ADD_TIMER,
        .task.attrs.timer = {
            .time = SERVER_TIMEOUT,
            .callback = server_health_check_callback,
            .data = timer_task
        },
        .server = server,
        .last_update = sched->loop_time
    };
    server->health_check_timer = timer_task;

    // Schedule request writing
    request_writing_task_t *write_req_task = malloc(sizeof(request_writing_task_t));
    if (write_req_task == NULL) {
        panic("Out of memory");
    }
    
    *write_req_task = (request_writing_task_t) {
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
                     server->cache_entry->uri);
    if (write_req_task->task.attrs.io.buffer == NULL) {
        panic("Out of memory");
    }

    // Schedule response reading
    response_analysis_task_t *read_res_task = malloc(sizeof(response_analysis_task_t));
    if (read_res_task == NULL) {
        panic("Out of memory");
    }

    *read_res_task = (response_analysis_task_t) {
        .task.type = READ_REQUEST,
        .task .attrs.io = {
           .as_first = false,
           .fd = server_fd,
           .data = read_res_task,
           .callback = analyze_response_callback
        },
        .server = server,
        .sm = HTTP_STATE_MACHINE_RES_INITIALIZER
    };
    if (http_state_machine_alloc(&read_res_task->sm,
                             &read_res_task->task.attrs.io.buffer,
                             &read_res_task->task.attrs.io.size)) {
        panic("Out of memory");
    }

    task_t *new_tasks = (task_t*) delegate_task;
    task_t *new_tasks_cursor = new_tasks;

    if (connect_task != NULL) {
        new_tasks_cursor->next = (task_t*) connect_task;
        new_tasks_cursor = (task_t*) connect_task;
    }

    new_tasks_cursor->next = (task_t*) write_req_task;
    new_tasks_cursor = (task_t*) write_req_task;

    new_tasks_cursor->next = (task_t*) read_res_task;
    new_tasks_cursor = (task_t*) read_res_task;

    new_tasks_cursor->next = (task_t*) timer_task;
    new_tasks_cursor = (task_t*) timer_task;

    new_tasks_cursor->next = NULL;

    aio_scheduler_schedule_all(sched, new_tasks);
}

void try_connect_callback(ssize_t r, int err, void *udata) {
    try_connect_to_server_task_t *task = udata;
    if ((r < 0 && (err == ECANCELED || err == EINVAL)) || task->server->state != SERVER_CONNECTION_IN_PROGRESS) {
        freeaddrinfo(task->first);
        free(task);
        return;
    }

    task->server->health_check_timer->last_update = task->server->sched->loop_time;

    socklen_t len = sizeof(err);
    getsockopt(task->task.attrs.io.fd, SOL_SOCKET, SO_ERROR, &err, &len);

    if (err == 0) {
        fprintf(stderr, "[Info] Connected to %s successfully\n", task->server->cache_entry->uri.hostname);
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
            if (connect_res < 0 && (errno == EINPROGRESS || errno == EAGAIN)) {
                task->next_try = current_try->ai_next;
                aio_scheduler_schedule(task->server->sched, (task_t*) task);
                break;
            }

            current_try = current_try->ai_next;
        }
        if (connect_res < 0 && current_try == NULL) {
            fprintf(stderr, "[Error] Failed to connect to %s\n", task->server->cache_entry->uri.hostname);
            fail_server_connection((server_task_t*) task, bad_gateway_response, bad_gateway_response_size);
            freeaddrinfo(task->first);
            return;
        }
        else if (connect_res >= 0) {
            fprintf(stderr, "[Info] Connected to %s successfully\n", task->server->cache_entry->uri.hostname);
            task->server->state = SERVER_RECEIVING_HEADERS;
            freeaddrinfo(task->first);
        }
    }
}

void write_request_callback(ssize_t w, int err, void *udata) {
    request_writing_task_t *task = udata;
    free(task->task.attrs.io.buffer);
    
    if ((w < 0 && (err == ECANCELED || EINVAL)) || 
        (task->server->state != SERVER_RECEIVING_HEADERS &&
        task->server->state != SERVER_RECEIVING_BODY)) {
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
        if (task->server->state == SERVER_RECEIVING_HEADERS) {
            fail_server_connection((server_task_t*) task, bad_gateway_response, bad_gateway_response_size);
        }
        else {
            fail_server_connection((server_task_t*) task, NULL, 0);
        }
    }
    else {
        task->server->health_check_timer->last_update = task->server->sched->loop_time;
        free(task);
    }
}

static void fill_up_cache_callback(ssize_t w, int err, void *udata) {
    server_task_t *task = udata;

    if ((w < 0 && (err == ECANCELED || err == EINVAL)) || task->server->state != SERVER_RECEIVING_BODY) {
        free(task);
        return;
    }

    if (w < 0) {
        fprintf(stderr, "[Error] Error while trying to read from %s: %s\n", task->server->cache_entry->uri.hostname, strerror(err));
        fail_server_connection((server_task_t*) task, NULL, 0);
        return;
    }
    else if (w == 0) {
        if (task->server->has_content_length) {
            fprintf(stderr, "[Error] Server %s terminated connection\n", task->server->cache_entry->uri.hostname);
            fail_server_connection((server_task_t*) task, NULL, 0);
        }
        else {
            fprintf(stderr, "[Info] Server %s terminated connection\n", task->server->cache_entry->uri.hostname);

            pthread_mutex_lock(&task->server->cache_entry->mtx);
            task->server->cache_entry->last_block->finished = true;
            pthread_mutex_unlock(&task->server->cache_entry->mtx);

            send_cached_to_all_clients(task->server, &task->server->cache_entry->pending, NULL, 0, true);

            cache_entry_postprocess(task->server->sched, task->server->cache_entry);
            close_server_connection(task);
        }
        return;
    }

    task->server->health_check_timer->last_update = task->server->sched->loop_time;

    task->server->content_length -= (size_t) w;

    bool last = (task->server->has_content_length && task->server->content_length == 0);
    bool finished = (last || task->server->cache_entry->last_block->size + (size_t) w == task->server->cache_entry->last_block->cap);

    cache_block_in_place_t *following_block = NULL;
    if (!last && finished) {
        size_t new_cap = task->server->has_content_length ? task->server->content_length : DATA_CHUNK_SIZE;
        following_block = malloc(sizeof(cache_block_in_place_t) + new_cap);
        if (following_block == NULL) {
            panic("Out of memory");
        }

        *following_block = (cache_block_in_place_t) {
            .type = IN_PLACE_CACHE_BLOCK,
            .size = 0,
            .cap = new_cap,
            .finished = false
        };
    }

    pthread_mutex_lock(&task->server->cache_entry->mtx);
    task->server->cache_entry->last_block->finished = finished;
    cache_entry_occupy_last_block(task->server->cache_entry, (size_t) w);
    if (following_block != NULL) {
        cache_entry_add_block(task->server->cache_entry, (cache_block_t*) following_block);
    }
    proxy_client_t *processing_start = task->server->cache_entry->pending;
    task->server->cache_entry->pending = NULL;
    pthread_mutex_unlock(&task->server->cache_entry->mtx);

    proxy_client_t *processing_end = send_cached_to_all_clients(task->server, &processing_start, task->task.attrs.io.buffer, (size_t) w, last);

    // If there are any clients left after processing, return them to list
    if (processing_start != NULL) {
        pthread_mutex_lock(&task->server->cache_entry->mtx);
        processing_end->next = task->server->cache_entry->pending;
        task->server->cache_entry->pending = processing_start;
        pthread_mutex_unlock(&task->server->cache_entry->mtx);
    }

    if (!last) {
        write_next_cache_block(task);
    }
    else {
        cache_entry_postprocess(task->server->sched, task->server->cache_entry);
        close_server_connection(task);
    }
}

static void write_next_cache_block(server_task_t *task) {
    void *buffer = get_cache_block_buffer(task->server->cache_entry->last_block);
    size_t size = task->server->cache_entry->last_block->cap - task->server->cache_entry->last_block->size;
    size = task->server->has_content_length ? MIN(size, task->server->content_length) : size;

    task->task = (task_t) {
        .type = READ_REQUEST,
        .attrs.io = {
            .fd = task->server->fd,
            .as_first = false,
            .buffer = buffer + task->server->cache_entry->last_block->size,
            .size = size,
            .data = task,
            .callback = fill_up_cache_callback
        }
    };
    aio_scheduler_schedule(task->server->sched, (task_t*) task);
}

void analyze_response_callback(ssize_t r, int err, void *udata) {
    response_analysis_task_t *task = udata;

    if ((r < 0 && (err == ECANCELED || err == EINVAL)) || task->server->state != SERVER_RECEIVING_HEADERS) {
        http_state_machine_destruct(&task->sm);
        free(task);
        return;
    }

    // Check for errors or connection closed
    if (r < 0) {
        fprintf(stderr, "[Error] Error while trying to read from %s: %s\n", task->server->cache_entry->uri.hostname, strerror(err));
    }
    else if (r == 0) {
        fprintf(stderr, "[Error] Server %s terminated connection\n", task->server->cache_entry->uri.hostname);
    }
    if (r <= 0) {
        http_state_machine_destruct(&task->sm);
        fail_server_connection((server_task_t*) task, bad_gateway_response, bad_gateway_response_size);
        return;
    }

    task->server->health_check_timer->last_update = task->server->sched->loop_time;

    http_state_machine_feed(&task->sm, (size_t) r);
    while (http_state_machine_step(&task->sm)) {
        char *name, *value;
        size_t name_size, value_size;
        switch (task->sm.state) {
            case MALFORMED:
                fprintf(stderr, "[Error] Server resp malformed\n");
                http_state_machine_destruct(&task->sm);
                fail_server_connection((server_task_t*) task, bad_gateway_response, bad_gateway_response_size);
                return;
            case HEADER_AVAILABLE:
                http_state_machine_get_header_name(&task->sm, task->sm.last_header, &name, &name_size);
                http_state_machine_get_header_value(&task->sm, task->sm.last_header, &value, &value_size);

                fprintf(stderr, "[Info] %s Field-name: \"%.*s\" Field-value: \"%.*s\"\n", task->server->cache_entry->uri.hostname, (int) name_size, name, (int) value_size, value);

                if (ci_memcmp(name, "Content-Length", MIN(name_size, 14))) {
                    bool succ;
                    task->server->content_length = parse_size_t_trimmed(value, value_size, &succ);
                    if (!succ) {
                        http_state_machine_destruct(&task->sm);
                        fail_server_connection((server_task_t*) task, bad_gateway_response, bad_gateway_response_size);
                        return;
                    }
                    task->server->has_content_length = true;
                }
                else if (ci_memcmp(name, "Connection", MIN(name_size, 10)) &&
                         !mem_compare_trimed(value, value_size, "close", 5)) {
                    http_state_machine_destruct(&task->sm);
                    fail_server_connection((server_task_t*) task, bad_gateway_response, bad_gateway_response_size);
                    return;
                }
                break;
            case COMPLETE:
                task->server->state = SERVER_RECEIVING_BODY;

                char *buffer = task->sm.data.arr;
                size_t size = task->sm.data.size;
                size_t cap = task->sm.data.cap;
                task->sm.data.arr = NULL;
                task->server->content_length -= size - task->sm.analyzed;

                http_state_machine_destruct(&task->sm);

                bool last = (task->server->has_content_length && task->server->content_length == 0);
                bool finished = (last || size == cap);

                if (task->sm.status != 200) {
                    cache_delete(task->server->cache_entry->uri);
                }

                cache_block_in_place_t *following_block = NULL;
                if (!last && finished) {
                    size_t new_cap = task->server->has_content_length ? task->server->content_length : DATA_CHUNK_SIZE;
                    following_block = malloc(sizeof(cache_block_in_place_t) + new_cap);
                    if (following_block == NULL) {
                        panic("Out of memory");
                    }

                    *following_block = (cache_block_in_place_t) {
                        .type = IN_PLACE_CACHE_BLOCK,
                        .size = 0,
                        .cap = new_cap,
                        .finished = false
                    };
                }

                pthread_mutex_lock(&task->server->cache_entry->mtx);
                *((cache_block_external_t*) task->server->cache_entry->first_block) = (cache_block_external_t) {
                    .type = EXTERNAL_CACHE_BLOCK,
                    .size = 0,
                    .cap = cap,
                    .finished = finished,
                    .data = buffer
                };
                cache_entry_occupy_last_block(task->server->cache_entry, size);
                if (following_block != NULL) {
                    cache_entry_add_block(task->server->cache_entry, (cache_block_t*) following_block);
                }
                proxy_client_t *processing_start = task->server->cache_entry->pending;
                task->server->cache_entry->pending = NULL;
                pthread_mutex_unlock(&task->server->cache_entry->mtx);

                proxy_client_t *processing_end = send_cached_to_all_clients(task->server, &processing_start, buffer, size, last);

                // If there are any clients left after processing, return them to list
                if (processing_start != NULL) {
                    pthread_mutex_lock(&task->server->cache_entry->mtx);
                    processing_end->next = task->server->cache_entry->pending;
                    task->server->cache_entry->pending = processing_start;
                    pthread_mutex_unlock(&task->server->cache_entry->mtx);
                }

                if (!last) {
                    write_next_cache_block((server_task_t*) task);
                }
                else {
                    cache_entry_postprocess(task->server->sched, task->server->cache_entry);
                    close_server_connection((server_task_t*) task);
                }
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

    if (http_state_machine_alloc(&task->sm, &task->task.attrs.io.buffer, &task->task.attrs.io.size)) {
        panic("Out of memory");
    }

    aio_scheduler_schedule(task->server->sched, (task_t*) task);
}
