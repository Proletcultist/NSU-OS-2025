#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include "proxy/server.h"
#include "scheduler/aio_scheduler.h"

void try_connect_callback(ssize_t r, int err, void *udata) {
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

void write_request_callback(ssize_t w, int err, void *udata) {
    task_t *task = udata;

    // Check for errors or connection closed
    if (w < 0) {
        fprintf(stderr, "[Error] Error while trying to write: %s\n", strerror(err));
        close(task->fd);
        // TODO: Also stop reading from server somehow
    }
    else if (w == 0) {
        fprintf(stderr, "[Error] Server terminated connection\n");
        close(task->fd);
        // TODO: Also stop reading from server somehow
    }

    free(task);
}

void analyze_response_callback(ssize_t r, int err, void *udata) {
    response_analysis_task_t *task = udata;

    // Check for errors or connection closed
    if (r < 0) {
        fprintf(stderr, "[Error] Error while trying to read from %s: %s\n", task->uri.hostname, strerror(err));

        // TODO: Also stop writing
        close(task->task.fd);
        http_state_machine_destruct(&task->sm);
        free(task);
        return;
    }
    else if (r == 0) {
        fprintf(stderr, "[Error] Server %s terminated connection\n", task->uri.hostname);

        // TODO: Also stop writing
        close(task->task.fd);
        http_state_machine_destruct(&task->sm);
        free(task);
        return;
    }

    http_state_machine_feed(&task->sm, (size_t) r);
    while (http_state_machine_step(&task->sm)) {
        switch (task->sm.state) {
            case MALFORMED:
                // TODO: Schedule write response to clients
                fprintf(stderr, "[Error] Server resp malformed\n");
                http_state_machine_destruct(&task->sm);
                free(task);
                return;
            case READ_STATUS_LINE:
                // TODO: Do smth with status line
                break;
            case HEADER_AVAILABLE:
                char *name, *value;
                size_t name_size, value_size;
                http_state_machine_get_header_name(&task->sm, task->sm.last_header, &name, &name_size);
                http_state_machine_get_header_value(&task->sm, task->sm.last_header, &value, &value_size);

                fprintf(stderr, "[Info] %s Field-name: \"", task->uri.hostname);
                for (size_t i = 0; i < name_size; i++) {
                    fprintf(stderr, "%c", name[i]);
                }
                fprintf(stderr, "\" Field-value: \"");
                for (size_t i = 0; i < value_size; i++) {
                    fprintf(stderr, "%c", value[i]);
                }
                fprintf(stderr, "\"\n");

                // TODO: Get content length, check Connection
                break;
            case COMPLETE:
                // TODO: Start writing
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
            case READ_REQUEST_LINE:
                // TODO: Unreachable
                break;
            case READING_STATUS_LINE:
            case READING_HEADER:
                break;
        }
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
                     .callback = analyze_response_callback
                 };
    aio_scheduler_schedule((task_t*) task, false);
}
