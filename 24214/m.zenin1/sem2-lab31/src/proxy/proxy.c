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
#include "proxy/request_analysis.h"
#include "proxy/responses.h"

#define MAX_LINE_SIZE (8 * 1024)        // 8KB
#define MAX_HEADERS_SIZE (64 * 1024)    // 64KB

typedef struct request_analysis_task {
    task_t task;
    char client_ip[16];
    bool cacheable;
    size_t content_size;
    request_analysis_data_t analysis_data;
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
        request_analysis_data_t_destruct(&task->analysis_data);
        free(task);
        return;
    }
    else if (r == 0) {
        fprintf(stderr, "[Error] Client %s terminated connection\n", task->client_ip);

        close(task->task.fd);
        request_analysis_data_t_destruct(&task->analysis_data);
        free(task);
        return;
    }

    char *eol = memchr(task->analysis_data.data.arr + task->analysis_data.data.size, '\n', (size_t) r);

    // No EOL found and max line size exceeded
    if (eol == NULL && task->analysis_data.data.size - task->analysis_data.analyzed >= MAX_LINE_SIZE) {
        request_analysis_data_t_destruct(&task->analysis_data);
        schedule_write_response(task->task.fd, too_long_line_response, too_long_line_response_size);
        free(task);
        return;
    }
    while (eol != NULL) {
        try_analyze_next_line(&task->analysis_data);

        switch (task->analysis_data.state) {
            case MALFORMED:
                request_analysis_data_t_destruct(&task->analysis_data);
                schedule_write_response(task->task.fd, bad_request_response, bad_request_response_size);
                free(task);
                return;
            case READ_REQUEST_LINE:
                fprintf(stderr, "[Info] %s Parsed hostname: \"%s\" port: \"%s\", path: \"%s\"\n", task->client_ip, task->analysis_data.uri.hostname, task->analysis_data.uri.port, task->analysis_data.uri.path);
                switch (task->analysis_data.method) {
                    case GET:
                        task->cacheable = true;
                        break;
                    case POST:
                    case HEAD:
                        task->cacheable = false;
                        break;
                    case UNKNOWN_METHOD:
                        request_analysis_data_t_destruct(&task->analysis_data);
                        schedule_write_response(task->task.fd, method_not_implemented_response, method_not_implemented_response_size);
                        free(task);
                        return;
                }
                if (task->analysis_data.version != HTTP_1_0) {
                    request_analysis_data_t_destruct(&task->analysis_data);
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

        }

        eol = memchr(eol + 1, '\n', (size_t) r - (size_t) (eol - (task->analysis_data.data.arr + task->analysis_data.data.size)) - 1);
    }
    task->analysis_data.data.size += (size_t) r;


    printf("\nBuffer content:\n");
    printf("\033[32m");
    for (size_t i = 0; i < task->analysis_data.analyzed; i++) {
        printf("%c", task->analysis_data.data.arr[i]);
    }
    printf("\033[0m");
    for (size_t i = task->analysis_data.analyzed; i < task->analysis_data.data.size; i++) {
        printf("%c", task->analysis_data.data.arr[i]);
    }
    printf("\n");


    if (task->analysis_data.data.size == task->analysis_data.data.cap) {
        vector_char_t_reserve(&task->analysis_data.data, task->analysis_data.data.cap + MAX_LINE_SIZE);
    }
    task->task = (task_t)
                 {
                     .type = READ_REQUEST,
                     .fd = task->task.fd,
                     .buffer = task->analysis_data.data.arr + task->analysis_data.data.size,
                     .size = task->analysis_data.data.cap - task->analysis_data.data.size,
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

    read_req_task->analysis_data = REQUEST_ANALYSIS_DATA_INITIALIZER;
    vector_char_t_reserve(&read_req_task->analysis_data.data, MAX_LINE_SIZE);

    read_req_task->task = (task_t)
                          {
                              .type = READ_REQUEST,
                              .fd = fd,
                              .buffer = read_req_task->analysis_data.data.arr,
                              .size = read_req_task->analysis_data.data.cap,
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
