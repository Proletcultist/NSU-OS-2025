#include <stdio.h>
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
}

static void aio_proceed_tasks(size_t index) {
    struct pollfd *pollfd = &sched.fds.arr[index];
    task_list_t *tasks = &sched.task_lists.arr[index];
    short revents = sched.fds.arr[index].revents; 

    bool written = false;
    bool readen = false;

    task_t *prev = tasks->first;

    for (task_t *cursor = tasks->first->next; cursor != NULL && pollfd->fd != -1;) {
        task_t *next = cursor->next;

        if (revents & POLLIN && !readen) {
            readen = true;
            if (cursor->type == ACCEPT_CONNECTION_REQUESTS) {
                aio_delete_task(pollfd, tasks, prev, cursor);

                cursor->callback(0, 0, cursor->data);
                next = prev->next;
            }
            else if (cursor->type == READ_REQUEST) {
                aio_delete_task(pollfd, tasks, prev, cursor);

                // Read and callback
                ssize_t r = read(cursor->fd, cursor->buffer, cursor->size);
                cursor->callback(r, errno, cursor->data);
                next = prev->next;
            }
        }
        else if (revents & POLLOUT && !written) {
            written = true;
            if (cursor->type == WAIT_FOR_CONNECTION) {
                aio_delete_task(pollfd, tasks, prev, cursor);

                cursor->callback(0, 0, cursor->data);
                next = prev->next;
            }
            else if (cursor->type == WRITE_REQUEST) {
                ssize_t w = write(cursor->fd, cursor->buffer, cursor->size);

                if (w >= 0) {
                    cursor->written += (size_t) w;
                    if (cursor->written == cursor->size) {
                        // Write had completed
                        aio_delete_task(pollfd, tasks, prev, cursor);
                        cursor->callback(cursor->written, errno, cursor->data);
                        next = prev->next;
                    }
                    else {
                        prev = cursor;
                    }
                }
                else {
                    // Write had failed
                    aio_delete_task(pollfd, tasks, prev, cursor);
                    cursor->callback(w, errno, cursor->data);
                    next = prev->next;
                }
            }
        }

        cursor = next;
        pollfd = &sched.fds.arr[index];
        tasks = &sched.task_lists.arr[index];
        revents = sched.fds.arr[index].revents; 
    }

    if (tasks->reads_amount == 0 && tasks->writes_amount == 0) {
        map_int_size_t_remove(&sched.fdToIndex, pollfd->fd);
        pollfd->fd = -1;
    }
}

void aio_scheduler_schedule(task_t *task, bool as_first) {
    task->written = 0;

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

    if (!as_first) {
        task_list_append(tasks, task);
    }
    else {
        task_list_add_first(tasks, task);
    }

    // Add poll event to wait for
    if (tasks->reads_amount != 0) {
        fd->events |= POLLIN;
    }
    if (tasks->writes_amount != 0) {
        fd->events |= POLLOUT;
    }
}

void aio_scheduler_proceed() {
    fprintf(stderr, "Poll %zu\n", sched.fds.size);
    poll(sched.fds.arr, sched.fds.size, -1);

    for (size_t i = 0; i < sched.fds.size; i++) {
        if (sched.fds.arr[i].revents & (POLLIN | POLLOUT)) {
            aio_proceed_tasks(i);
        }
    }

    // Cleanup deleted pollfds
    for (size_t i = 0; i < sched.fds.size;) {
        size_t index = sched.fds.size - i - 1;
        if (sched.fds.arr[index].fd == -1){
            // Just delete from the end
            if (index == sched.fds.size - 1) {
                task_list_destruct(&sched.task_lists.arr[index]);
                sched.fds.size--;
                sched.task_lists.size--;
            }
            // Copy last and then delete from the end
            else {
                task_list_destruct(&sched.task_lists.arr[index]);
                sched.fds.arr[index] = sched.fds.arr[sched.fds.size - 1];
                sched.task_lists.arr[index] = sched.task_lists.arr[sched.task_lists.size - 1];
                map_int_size_t_set(&sched.fdToIndex, sched.fds.arr[index].fd, index);
                sched.fds.size--;
                sched.task_lists.size--;
            }
        }
        else {
            i++;
        }
    }
}

void aio_scheduler_cancel_all(int fd) {
    size_t *index = map_int_size_t_get(&sched.fdToIndex, fd);
    if (index != NULL) {
        sched.fds.arr[*index].fd = -1;
    }
}

void aio_scheduler_destruct() {
}

