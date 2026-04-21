#include <stdio.h>
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

#define CHUNK_SIZE 512

typedef struct request_analysis_task {
    task_t task;
    char client_ip[16];
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

static void read_req_line_callback(ssize_t r, int errno, void *udata) {
    request_analysis_task_t *task = udata;

    // Check for errors or connection closed
    if (r < 0) {
        fprintf(stderr, "[Error] Error while trying to read from %s: %s\n", task->client_ip, strerror(errno));

        request_analysis_data_t_destruct(&task->analysis_data);
        free(task);
        return;
    }
    else if (r == 0) {
        fprintf(stderr, "[Error] Client %s terminated connection\n", task->client_ip);

        request_analysis_data_t_destruct(&task->analysis_data);
        free(task);
        return;
    }

    // Adjust size and enlarge buffer if needed
    task->analysis_data.data.size += (size_t) r;

    printf("\nBuffer content:\n");
    for (size_t i = 0; i < task->analysis_data.data.size; i++) {
        printf("%c", task->analysis_data.data.arr[i]);
    }
    printf("\n");

    request_analyzis_result_t res = try_analyze(&task->analysis_data);
    switch (res) {
        case INCOMPLETE:
            if (task->analysis_data.data.size == task->analysis_data.data.cap) {
                vector_char_t_reserve(&task->analysis_data.data, task->analysis_data.data.cap + CHUNK_SIZE);
            }

            task->task = (task_t)
                         {
                             .type = READ_REQUEST,
                             .fd = task->task.fd,
                             .buffer = task->analysis_data.data.arr + task->analysis_data.data.size,
                             .size = task->analysis_data.data.cap - task->analysis_data.data.size,
                             .data = task,
                             .callback = read_req_line_callback
                         };

            aio_scheduler_schedule((task_t*) task);
            break;
        case MALFORMED:
            request_analysis_data_t_destruct(&task->analysis_data);

            task_t *write_error_task = malloc(sizeof(task_t));
            *write_error_task = (task_t)
                                {
                                    .type = WRITE_REQUEST,
                                    .fd = task->task.fd,
                                    .buffer = bad_request_response,
                                    .size = bad_request_response_size,
                                    .data = write_error_task,
                                    .callback = write_response_callback
                                };
            aio_scheduler_schedule(write_error_task);

            free(task);
            break;
        case COMPLETE:
            break;
    }

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
    vector_char_t_reserve(&read_req_task->analysis_data.data, CHUNK_SIZE);

    read_req_task->task = (task_t)
                          {
                              .type = READ_REQUEST,
                              .fd = fd,
                              .buffer = read_req_task->analysis_data.data.arr,
                              .size = read_req_task->analysis_data.data.cap,
                              .data = read_req_task,
                              .callback = read_req_line_callback
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
