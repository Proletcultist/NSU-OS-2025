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

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX_HEADERS_SIZE (64 * 1024)    // 64KB

typedef struct request_analysis_task {
    task_t task;
    char client_ip[16];
    size_t bytes_received;
    http_state_machine_t sm;
} request_analysis_task_t;

typedef struct discard_request_task {
    task_t task;
    char client_ip[16];
    size_t bytes_received;
    char buffer[128];
    char *msg;
    size_t msg_size;
} discard_request_task_t;

static bool findDoubleEOL(char *buffer, size_t size) {
    char *p = buffer;
    char *end = buffer + size;

    p = memchr(p, '\n', end - p);
    while (p != NULL) {
        // \n\n found
        if (p > buffer && *(p - 1) == '\n') {
            return true;
        }
        // \r\n\r\n found
        else if (p >= buffer + 3 &&
                 *(p - 1) == '\r' &&
                 *(p - 2) == '\n' &&
                 *(p - 3) == '\r') {
            return true;
        }

        p++;
        p = memchr(p, '\n', end - p);
    }

    return false;
}

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

static void discard_request_callback(ssize_t r, int errno, void *udata) {
    discard_request_task_t *task = udata;

    // Check for errors or connection closed
    if (r < 0) {
        fprintf(stderr, "[Error] Error while trying to read from %s: %s\n", task->client_ip, strerror(errno));

        close(task->task.fd);
        free(task);
        return;
    }
    else if (r == 0) {
        fprintf(stderr, "[Error] Client %s terminated connection\n", task->client_ip);

        close(task->task.fd);
        free(task);
        return;
    }

    task->bytes_received += (size_t) r;

    size_t size = (size_t) r + (size_t) ((char*) task->task.buffer - task->buffer);
    if (task->bytes_received > MAX_HEADERS_SIZE) {
        // TODO: Send error
    }
    else if (findDoubleEOL(task->buffer, size)) {
        schedule_write_response(task->task.fd, task->msg, task->msg_size);
        free(task);
    }
    else {
        // Copy last 3 bytes of not analyzed data in case double EOL starts there
        size_t end_size = size > 3 ? 3 : size;

        task->task = (task_t)
                     {
                         .type = READ_REQUEST,
                         .fd = task->task.fd,
                         .buffer = task->buffer + end_size,
                         .size = sizeof(task->buffer) - end_size,
                         .data = task,
                         .callback = discard_request_callback
                     };
        memmove(task->buffer, task->buffer + size - end_size, end_size);
        aio_scheduler_schedule((task_t*) task);
    }
}

static void schedule_discard_req_and_write_response(request_analysis_task_t *atask, char *msg, size_t msg_size) {
    char *data = atask->sm.data.arr + atask->sm.analyzed;
    size_t data_size = atask->sm.data.size - atask->sm.analyzed;

    if (atask->sm.analyzed > 0 && *(data - 1) == '\n') {
        data--;
        data_size++;
    }
    if (atask->sm.analyzed > 1 && *(data - 1) == '\r') {
        data--;
        data_size++;
    }

    if (findDoubleEOL(data, data_size)) {
        schedule_write_response(atask->task.fd, msg, msg_size);
    }
    else {
        discard_request_task_t *disc_task = malloc(sizeof(discard_request_task_t));
        // Copy last 3 bytes of not analyzed data in case double EOL starts there
        size_t end_size = data_size > 3 ? 3 : data_size;

        *disc_task = (discard_request_task_t)
                     {
                        .task = (task_t)
                                {
                                    .type = READ_REQUEST,
                                    .fd = atask->task.fd,
                                    .buffer = disc_task->buffer + end_size,
                                    .size = sizeof(disc_task->buffer) - end_size,
                                    .data = disc_task,
                                    .callback = discard_request_callback
                                },
                        .bytes_received = atask->bytes_received,
                        .msg = msg,
                        .msg_size = msg_size
                     };
        stpncpy(disc_task->client_ip, atask->client_ip, sizeof(disc_task->client_ip));
        memcpy(disc_task->buffer, data + data_size - end_size, end_size);
        aio_scheduler_schedule((task_t*) disc_task);
    }
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

    task->bytes_received += (size_t) r;
    if (task->bytes_received > MAX_HEADERS_SIZE) {
        // TODO: Send error
    }

    http_state_machine_feed(&task->sm, (size_t) r);
    while (http_state_machine_step(&task->sm)) {
        switch (task->sm.state) {
            case MALFORMED:
                schedule_discard_req_and_write_response(task, bad_request_response, bad_request_response_size);
                http_state_machine_destruct(&task->sm);
                free(task);
                return;
            case READ_REQUEST_LINE:
                fprintf(stderr, "[Info] %s Parsed hostname: \"%s\" port: \"%s\", path: \"%s\"\n", task->client_ip, task->sm.uri.hostname, task->sm.uri.port, task->sm.uri.path);
                switch (task->sm.method) {
                    case POST:
                    case HEAD:
                        schedule_discard_req_and_write_response(task, method_not_allowed, method_not_allowed_size);
                        http_state_machine_destruct(&task->sm);
                        free(task);
                        return;
                    case UNKNOWN_METHOD:
                        schedule_discard_req_and_write_response(task, method_not_implemented_response, method_not_implemented_response_size);
                        http_state_machine_destruct(&task->sm);
                        free(task);
                        return;
                    case GET:
                        break;
                }
                if (task->sm.version != HTTP_1_0) {
                    schedule_discard_req_and_write_response(task, version_not_supported_response, version_not_supported_response_size);
                    http_state_machine_destruct(&task->sm);
                    free(task);
                    return;
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
                    schedule_discard_req_and_write_response(task, bad_request_response, bad_request_response_size);
                    http_state_machine_destruct(&task->sm);
                    free(task);
                    return;
                }
                else if (memcmp(name, "Connection", MIN(name_size, 10)) == 0) {
                    http_state_machine_delete_header(&task->sm, task->sm.last_header);
                }
                break;
            case COMPLETE:
                fprintf(stderr, "[Info] %s COMPLETE\n", task->client_ip);
                // TODO: Open connection with server, schedule writing to it
                return;
            case READING_REQUEST_LINE:
            case READING_HEADER:
                break;
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
