#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include "aio_scheduler.h"

aio_scheduler_t sched = {
                            .fds = (vector_pollfd_t) VECTOR_INITIALIZER,
                            .tasks = (vector_task_descriptor_t) VECTOR_INITIALIZER
                        };

void aio_scheduler_schedule(task_t *task) {
    struct pollfd *fd = NULL;
    tasks_descriptor_t *desc = NULL;

    task->next = NULL;
    
    for (size_t i = 0; i < sched.fds.size; i++) {
        if (sched.fds.arr[i].fd == task->fd) {
            fd = &sched.fds.arr[i];
            desc = &sched.tasks.arr[i];
            break;
        }
    }

    // Add fd and tasks_descriptor
    if (desc == NULL) {
        vector_pollfd_t_push(&sched.fds, (struct pollfd)
                                          {
                                            .fd = task->fd,
                                            .events = 0,
                                            .revents = 0
                                          });

        task_t *sentinel = malloc(sizeof(task_t));
        sentinel->next = NULL;

        vector_task_descriptor_t_push(&sched.tasks, (tasks_descriptor_t) 
                                                     {.first = sentinel,
                                                      .last = sentinel,
                                                      .reads_amount = 0,
                                                      .writes_amount = 0});


        fd = &sched.fds.arr[sched.fds.size - 1];
        desc = &sched.tasks.arr[sched.tasks.size - 1];
    }

    // Add task at the end of tasks list
    desc->last->next = task;
    desc->last = task;

    // Add poll event to wait for
    switch (task->type) {
        case ACCEPT_CONNECTION_REQUESTS:
        case READ_REQUEST:
            desc->reads_amount++;
            fd->events |= POLLIN;
            break;
        case WRITE_REQUEST:
            desc->writes_amount++;
            fd->events |= POLLOUT;
            break;
    }
}

static inline void aio_delete_task(struct pollfd *pollfd, tasks_descriptor_t *tasks, task_t *prev, task_t *this) {
    // Delete task from linked list
    prev->next = this->next;
    
    // If it was last, prev become last
    if (tasks->last == this) {
        tasks->last = prev;
    }

    switch (this->type) {
        case ACCEPT_CONNECTION_REQUESTS:
        case READ_REQUEST:
            tasks->reads_amount--;
            break;
        case WRITE_REQUEST:
            tasks->writes_amount--;
            break;
    }

    if (tasks->reads_amount == 0) {
        pollfd->events &= ~POLLIN;
    }
    if (tasks->writes_amount == 0) {
        pollfd->events &= ~POLLOUT;
    }
    if (tasks->reads_amount == 0 && tasks->writes_amount == 0) {
        // TODO: Delete fd and descriptor
    }
}

static void aio_proceed_tasks(struct pollfd *pollfd, short revents, tasks_descriptor_t *tasks) {
    bool written = false;
    task_t *prev = tasks->first;

    for (task_t *cursor = tasks->first->next; cursor != NULL; cursor = cursor->next) {
        if (revents & POLLIN) {
            if (cursor->type == ACCEPT_CONNECTION_REQUESTS) {
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
            if (cursor->type == WRITE_REQUEST) {
                // TODO: Do NONBLOCK write, proceed with task, set written
            }
        }

        prev = cursor;
    }
}

void aio_scheduler_proceed() {
    fprintf(stderr, "Poll\n");
    poll(sched.fds.arr, sched.fds.size, -1);

    for (size_t i = 0; i < sched.fds.size; i++) {
        if (sched.fds.arr[i].revents & (POLLIN | POLLOUT)) {
            aio_proceed_tasks(&sched.fds.arr[i], sched.fds.arr[i].revents, &sched.tasks.arr[i]);
        }
        else if (sched.fds.arr[i].revents & (POLLHUP | POLLERR)) {
            // TODO: Delete socket
        }
    }
}

void aio_scheduler_destruct() {
}
