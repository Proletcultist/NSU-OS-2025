#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include "scheduler/aio_scheduler.h"

aio_scheduler_t sched = {
                            .fds = (vector_pollfd_t) VECTOR_INITIALIZER,
                            .task_lists = (vector_task_list_t) VECTOR_INITIALIZER,
                            .fdToIndex = (map_int_size_t) HASHMAP_INITIALIZER
                        };

static inline void aio_delete_task(struct pollfd *pollfd, task_list_t *tasks, task_t *prev, task_t *this) {
    task_list_delete(tasks, prev, this);

    if (tasks->reads_amount == 0) {
        pollfd->events &= ~POLLIN;
    }
    if (tasks->writes_amount == 0) {
        pollfd->events &= ~POLLOUT;
    }
    if (tasks->reads_amount == 0 && tasks->writes_amount == 0) {
        // TODO: Delete fd and descriptor
        map_int_size_t_remove(&sched.fdToIndex, pollfd->fd);
        pollfd->fd = -1;
    }
}

static void aio_proceed_tasks(struct pollfd *pollfd, short revents, task_list_t *tasks) {
    bool written = false;
    task_t *prev = tasks->first;

    for (task_t *cursor = tasks->first->next; cursor != NULL;) {
        task_t *tmp = cursor->next;

        if (revents & POLLIN) {
            if (cursor->type == ACCEPT_CONNECTION_REQUESTS) {
                aio_delete_task(pollfd, tasks, prev, cursor);

                cursor->callback(0, 0, cursor);
            }
            else if (cursor->type == READ_REQUEST) {
                aio_delete_task(pollfd, tasks, prev, cursor);

                // Read and callback
                ssize_t r = read(cursor->fd, cursor->buffer, cursor->size);
                cursor->callback(r, errno, cursor->data);
            }
        }
        else if (revents & POLLOUT) {
            if (cursor->type == WRITE_REQUEST && !written) {
                written = true;
                ssize_t w = write(cursor->fd, cursor->buffer, cursor->size);
                int tmp = errno;

                if (w >= 0) {
                    cursor->written += (size_t) w;
                    if (cursor->written == cursor->size) {
                        // Write had completed
                        aio_delete_task(pollfd, tasks, prev, cursor);
                        cursor->callback(cursor->written, tmp, cursor->data);
                    }
                    else {
                        prev = cursor;
                    }
                }
                else {
                    // Write had failed
                    aio_delete_task(pollfd, tasks, prev, cursor);
                    cursor->callback(w, tmp, cursor->data);
                }
            }
        }

        cursor = tmp;
    }
}

void aio_scheduler_schedule(task_t *task) {
    task->written = 0;
    int flags = fcntl(task->fd, F_GETFL, 0);
    fcntl(task->fd, F_SETFL, flags | O_NONBLOCK);

    struct pollfd *fd = NULL;
    task_list_t *tasks = NULL;

    size_t *index = map_int_size_t_get(&sched.fdToIndex, task->fd);

    if (index == NULL) {
        vector_pollfd_t_push(&sched.fds, (struct pollfd)
                                          {
                                            .fd = task->fd,
                                            .events = 0,
                                            .revents = 0
                                          });

        vector_task_list_t_push(&sched.task_lists, task_list_construct());

        fd = &sched.fds.arr[sched.fds.size - 1];
        tasks = &sched.task_lists.arr[sched.task_lists.size - 1];

        map_int_size_t_set(&sched.fdToIndex, task->fd, sched.fds.size - 1);
    }
    else {
        fd = &sched.fds.arr[*index];
        tasks = &sched.task_lists.arr[*index];
    }

    task_list_append(tasks, task);

    // Add poll event to wait for
    if (tasks->reads_amount != 0) {
        fd->events |= POLLIN;
    }
    if (tasks->writes_amount != 0) {
        fd->events |= POLLOUT;
    }
}

void aio_scheduler_proceed() {
    fprintf(stderr, "Poll\n");
    poll(sched.fds.arr, sched.fds.size, -1);

    for (size_t i = 0; i < sched.fds.size; i++) {
        if (sched.fds.arr[i].revents & (POLLIN | POLLOUT)) {
            aio_proceed_tasks(&sched.fds.arr[i], sched.fds.arr[i].revents, &sched.task_lists.arr[i]);
        }
        else if (sched.fds.arr[i].revents & (POLLHUP | POLLERR)) {
            // TODO: Delete socket
        }
    }
}

void aio_scheduler_destruct() {
}

