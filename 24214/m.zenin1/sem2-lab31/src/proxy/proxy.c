#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <stddef.h>
#include <netdb.h>
#include "scheduler/aio_scheduler.h"
#include "http.h"
#include "proxy/responses.h"
#include "cache/cache.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX_HEADERS_SIZE (64 * 1024)    // 64KB
#define MAX_LINE_SIZE (8 * 1024)    // 8KB

typedef struct request_analysis_task {
    task_t task;
    char client_ip[16];
    size_t bytes_received;
    http_state_machine_t sm;

    bool bad_request;
    char *msg;
    size_t msg_size;
} request_analysis_task_t;

typedef struct try_connect_to_server_task {
    task_t task;
    struct addrinfo *first;
    struct addrinfo *next_try;
} try_connect_to_server_task_t;

static void try_connect_callback(ssize_t r, int err, void *udata) {
    try_connect_to_server_task_t *task = udata;

    socklen_t len = sizeof(err);
    getsockopt(task->task.fd, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err == 0) {
        fprintf(stderr, "[Info] Connected successfully\n");
        freeaddrinfo(task->first);
        free(task);
    }
    else if (task->next_try != NULL) {
        err = connect(task->task.fd, task->next_try->ai_addr, task->next_try->ai_addrlen);
        if (err < 0 && (errno == EINPROGRESS || errno == EAGAIN)) {
            task->next_try = task->next_try->ai_next;
            aio_scheduler_schedule((task_t*) task, true);
        }
        else if (err < 0) {
            // TODO: Handle
            fprintf(stderr, "[Info] Error while trying to connect\n");
            freeaddrinfo(task->first);
            free(task);
        }
    }
    else {
        fprintf(stderr, "[Error] Failed to connect\n");
        freeaddrinfo(task->first);
        free(task);
    }
}

static void generate_request(char **buffer, size_t *size, uri_t uri) {
    size_t hostname_len = strlen(uri.hostname);
    size_t port_len = strlen(uri.port);
    size_t path_len = strlen(uri.path);
    *size = 71 + hostname_len * 2 + port_len + path_len + 1;
    *buffer = malloc(*size);

    sprintf(*buffer, "GET http://%s:%s%s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\nContent-Lenght: 0\r\n\r\n", uri.hostname, uri.port, uri.path, uri.hostname);
}

static void write_response_callback(ssize_t r, int err, void *udata) {
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

static void write_request_callback(ssize_t w, int err, void *udata) {
    task_t *task = udata;

    // Check for errors or connection closed
    if (w < 0) {
        fprintf(stderr, "[Error] Error while trying to write: %s\n", strerror(err));
    }
    else if (w == 0) {
        fprintf(stderr, "[Error] Server terminated connection\n");
    }

    // TODO: Also stop reading from server somehow
    close(task->fd);
    free(task);
}

static void schedule_write_response(int fd, char *msg, size_t msg_size) {
    task_t *write_error_task = malloc(sizeof(task_t));
    *write_error_task = (task_t)
                        {
                            .type = WRITE_REQUEST,
                            .fd = fd,
                            .buffer = msg,
                            .size = msg_size,
                            .data = write_error_task,
                            .callback = write_response_callback
                        };
    aio_scheduler_schedule(write_error_task, false);
}

static bool mem_compare_trimed(char *a, size_t a_size, char *b, size_t b_size) {
    while (a_size > 0 && (*a == ' ' || *a == '\t')) {
        a++;
        a_size--;
    }
    while (b_size > 0 && (*b == ' ' || *b == '\t')) {
        b++;
        b_size--;
    }

    size_t i = 0;
    while (i < a_size && i < b_size && a[i] == b[i]) {
        i++;
    }

    for (size_t j = i; j < a_size; j++) {
        if (a[j] != ' ' && a[j] != '\t') {
            return false;
        }
    }
    for (size_t j = i; j < b_size; j++) {
        if (b[j] != ' ' && b[j] != '\t') {
            return false;
        }
    }

    return true;
}

static struct addrinfo* resolve_address(char *hostname, char *port) {
    struct addrinfo *res;

    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM
    };
    int err = getaddrinfo(hostname, port, &hints, &res);

    return res;
}

static void read_req_line_and_headers_callback(ssize_t r, int err, void *udata) {
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
        schedule_write_response(task->task.fd, bad_request_response, bad_request_response_size);
        http_state_machine_destruct(&task->sm);
        free(task);
        return;
    }

    http_state_machine_feed(&task->sm, (size_t) r);
    while (http_state_machine_step(&task->sm)) {
        switch (task->sm.state) {
            case MALFORMED:
                schedule_write_response(task->task.fd, bad_request_response, bad_request_response_size);
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
                    schedule_write_response(task->task.fd, task->msg, task->msg_size);
                }
                else {
                    cache_entry_t *entry = cache_lookup(task->sm.uri);
                    if (entry == NULL) {
                        fprintf(stderr, "No cache for u :(\n");

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

                        char *buffer;
                        size_t size;
                        generate_request(&buffer, &size, task->sm.uri);

                        task_t *write_req_task = malloc(sizeof(task_t));
                        *write_req_task = (task_t)
                                          {
                                              .type = WRITE_REQUEST,
                                              .fd = server,
                                              .buffer = buffer,
                                              .size = size,
                                              .data = write_req_task,
                                              .callback = write_request_callback
                                          };
                        aio_scheduler_schedule(write_req_task, false);
                    }
                    else {
                        fprintf(stderr, "Wtf\n");
                    }
                }

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
            case READING_HEADER:
                break;
        }
    }

    // Line is too big
    if (task->sm.data.size - task->sm.analyzed > MAX_LINE_SIZE) {
        schedule_write_response(task->task.fd, bad_request_response, bad_request_response_size);
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
                     .callback = read_req_line_and_headers_callback
                 };
    aio_scheduler_schedule((task_t*) task, false);
}

static void accept_connection(ssize_t r, int err, void *udata) {
    task_t *task = udata;

    struct sockaddr connected_addr;
    socklen_t connected_len = sizeof(connected_addr);
    int fd = accept(task->fd, &connected_addr, &connected_len);

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    request_analysis_task_t *read_req_task = malloc(sizeof(request_analysis_task_t));
    if (connected_addr.sa_family == AF_INET) {
        inet_ntop(AF_INET, &((struct sockaddr_in*) &connected_addr)->sin_addr, read_req_task->client_ip, INET_ADDRSTRLEN);
        fprintf(stderr, "[Info] Connected: %s\n", read_req_task->client_ip);
    }
    else {
        strncpy(read_req_task->client_ip, "Unknown", sizeof(read_req_task->client_ip) - 1);
    }

    read_req_task->sm = HTTP_STATE_MACHINE_INITIALIZER;
    read_req_task->bad_request = false;
    read_req_task->sm.discarding = true;
    read_req_task->bytes_received = 0;
    void *buffer;
    size_t size;
    http_state_machine_alloc(&read_req_task->sm, &buffer, &size);
    read_req_task->task = (task_t)
                          {
                              .type = READ_REQUEST,
                              .fd = fd,
                              .buffer = buffer,
                              .size = size,
                              .data = read_req_task,
                              .callback = read_req_line_and_headers_callback
                          };

    aio_scheduler_schedule((task_t*) read_req_task, false);
    aio_scheduler_schedule(task, false);
}


void start_proxy(struct in_addr ip, in_port_t port) {
    int listening = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_port = port,
                               .sin_addr = ip};

    // TODO: Propagate error
    if (bind(listening, (struct sockaddr*) &addr, sizeof(addr))) {
        perror("Failed to bind");
        return;
    }
    if (listen(listening, 0)) {
        perror("Failed to listen");
        return;
    }

    task_t accept_task;
    accept_task = (task_t) 
                  {
                     .type = ACCEPT_CONNECTION_REQUESTS,
                     .fd = listening,
                     .data = &accept_task,
                     .callback = accept_connection
                  };

    aio_scheduler_schedule(&accept_task, false);

    while (true) {
        aio_scheduler_proceed();
    }
}
