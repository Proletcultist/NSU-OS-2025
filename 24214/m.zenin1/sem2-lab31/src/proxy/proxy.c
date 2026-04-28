#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <stddef.h>
#include "scheduler/aio_scheduler.h"
#include "http.h"
#include "proxy/responses.h"

#define MAX_LINE_SIZE (8 * 1024)        // 8KB
#define MAX_HEADERS_SIZE (64 * 1024)    // 64KB

typedef struct request_analysis_task {
    task_t task;
    char client_ip[16];
    bool cacheable;
    size_t content_size;
    http_state_machine_t sm;
} request_analysis_task_t;

static void write_response_callback(ssize_t r, int errno, void *udata) {
    task_t *task = udata;

    // Check for errors or connection closed
    if (r < 0) {
        fprintf(stderr, "[Error] Error while trying to write: %s\n", strerror(errno));
    }
    else if (r == 0) {
        fprintf(stderr, "[Error] Client terminated connection\n");
    }

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
    aio_scheduler_schedule(write_error_task);
}

static void read_req_line_and_headers_callback(ssize_t r, int errno, void *udata) {
    request_analysis_task_t *task = udata;

    // Check for errors or connection closed
    if (r < 0) {
        fprintf(stderr, "[Error] Error while trying to read from %s: %s\n", task->client_ip, strerror(errno));

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

    char *eol = memchr(task->sm.data.arr + task->sm.data.size, '\n', (size_t) r);

    // No EOL found and max line size exceeded
    if (eol == NULL && task->sm.data.size - task->sm.analyzed >= MAX_LINE_SIZE) {
        http_state_machine_destruct(&task->sm);
        schedule_write_response(task->task.fd, too_long_line_response, too_long_line_response_size);
        free(task);
        return;
    }
    while (eol != NULL) {
        http_state_machine_analyze_next_line(&task->sm);

        switch (task->sm.state) {
            case MALFORMED:
                http_state_machine_destruct(&task->sm);
                schedule_write_response(task->task.fd, bad_request_response, bad_request_response_size);
                free(task);
                return;
            case READ_REQUEST_LINE:
                fprintf(stderr, "[Info] %s Parsed hostname: \"%s\" port: \"%s\", path: \"%s\"\n", task->client_ip, task->sm.uri.hostname, task->sm.uri.port, task->sm.uri.path);
                switch (task->sm.method) {
                    case GET:
                        task->cacheable = true;
                        break;
                    case POST:
                    case HEAD:
                        task->cacheable = false;
                        break;
                    case UNKNOWN_METHOD:
                        http_state_machine_destruct(&task->sm);
                        schedule_write_response(task->task.fd, method_not_implemented_response, method_not_implemented_response_size);
                        free(task);
                        return;
                }
                if (task->sm.version != HTTP_1_0) {
                    http_state_machine_destruct(&task->sm);
                    schedule_write_response(task->task.fd, version_not_supported_response, version_not_supported_response_size);
                    free(task);
                    return;
                }
                break;
            case HEADER_AVAILABLE:
                // TODO: Do smth with header
                break;
            case COMPLETE:
                // TODO: Open connection with server, schedule writing to it
                return;
            case READING_REQUEST_LINE:
            case READING_HEADERS:
                break;
        }

        eol = memchr(eol + 1, '\n', (size_t) r - (size_t) (eol - (task->sm.data.arr + task->sm.data.size)) - 1);
    }
    task->sm.data.size += (size_t) r;


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


    if (task->sm.data.size == task->sm.data.cap) {
        vector_char_t_reserve(&task->sm.data, task->sm.data.cap + MAX_LINE_SIZE);
    }
    task->task = (task_t)
                 {
                     .type = READ_REQUEST,
                     .fd = task->task.fd,
                     .buffer = task->sm.data.arr + task->sm.data.size,
                     .size = task->sm.data.cap - task->sm.data.size,
                     .data = task,
                     .callback = read_req_line_and_headers_callback
                 };
    aio_scheduler_schedule((task_t*) task);
}

static void accept_connection(ssize_t r, int errno, void *udata) {
    task_t *task = udata;

    struct sockaddr connected_addr;
    socklen_t connected_len = sizeof(connected_addr);
    int fd = accept(task->fd, &connected_addr, &connected_len);


    request_analysis_task_t *read_req_task = malloc(sizeof(request_analysis_task_t));
    if (connected_addr.sa_family == AF_INET) {
        inet_ntop(AF_INET, &((struct sockaddr_in*) &connected_addr)->sin_addr, read_req_task->client_ip, INET_ADDRSTRLEN);
        fprintf(stderr, "[Info] Connected: %s\n", read_req_task->client_ip);
    }
    else {
        strncpy(read_req_task->client_ip, "Unknown", sizeof(read_req_task->client_ip) - 1);
    }

    read_req_task->sm = HTTP_STATE_MACHINE_INITIALIZER;
    vector_char_t_reserve(&read_req_task->sm.data, MAX_LINE_SIZE);

    read_req_task->task = (task_t)
                          {
                              .type = READ_REQUEST,
                              .fd = fd,
                              .buffer = read_req_task->sm.data.arr,
                              .size = read_req_task->sm.data.cap,
                              .data = read_req_task,
                              .callback = read_req_line_and_headers_callback
                          };

    aio_scheduler_schedule((task_t*) read_req_task);
    aio_scheduler_schedule(task);
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
                     .buffer = NULL,
                     .size = 0,
                     .data = &accept_task,
                     .callback = accept_connection
                  };

    aio_scheduler_schedule(&accept_task);

    while (true) {
        aio_scheduler_proceed();
    }
}
