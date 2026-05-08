#include <math.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include "scheduler/aio_scheduler.h"

#define CHECKUP_TIMEOUT 2 * 1000 // ms

aio_scheduler_t aio_scheduler_construct() {
    aio_scheduler_t ret = {
                            .fds = (vector_pollfd_t) VECTOR_INITIALIZER,
                            .task_lists = (vector_task_list_t) VECTOR_INITIALIZER,
                            .fdToIndex = (map_int_size_t) HASHMAP_INITIALIZER
                          };
    ret.pending_tasks = task_list_construct();

    return ret;
}

static void delegate(aio_scheduler_t *sched, task_t *task) {
    vector_pollfd_t_push(&sched->fds, (struct pollfd)
                                      {
                                        .fd = task->fd,
                                        .events = 0,
                                        .revents = 0
                                      });
    vector_task_list_t_push(&sched->task_lists, task_list_construct());
    map_int_size_t_set(&sched->fdToIndex, task->fd, sched->fds.size - 1);
    
    if (task->callback) {
        task->callback(0, 0, task->data);
    }
}
static void undelegate(aio_scheduler_t *sched, task_t *task) {
    size_t *index = map_int_size_t_get(&sched->fdToIndex, task->fd);
    if (index == NULL) {
        if (task->callback) {
            task->callback(-1, EINVAL, task->data);
        }
        return;
    }

    // Cancel all tasks
    task_list_t *task_list = &sched->task_lists.arr[*index];
    for (task_t *cursor = task_list->first->next; cursor != NULL; cursor = task_list->first->next) {
        task_list_delete(task_list, task_list->first, cursor);

        if (cursor->callback) {
            cursor->callback(-1, ECANCELED, cursor->data);
        }
    }
    task_list_destruct(task_list);

    // Just delete from the end
    if (*index == sched->fds.size - 1) {
        sched->fds.size--;
        sched->task_lists.size--;
    }
    // Copy last and then delete from the end
    else {
        sched->fds.arr[*index] = sched->fds.arr[sched->fds.size - 1];
        sched->task_lists.arr[*index] = sched->task_lists.arr[sched->task_lists.size - 1];
        map_int_size_t_set(&sched->fdToIndex, sched->fds.arr[*index].fd, *index);
        sched->fds.size--;
        sched->task_lists.size--;
    }

    // Remove mapping
    map_int_size_t_remove(&sched->fdToIndex, sched->fds.arr[*index].fd);

    if (task->callback) {
        task->callback(0, 0, task->data);
    }
}
static void schedule_pending(aio_scheduler_t *sched, task_t *task) {
    size_t *index = map_int_size_t_get(&sched->fdToIndex, task->fd);
    if (index == NULL){
        if (task->callback) {
            task->callback(-1, EINVAL, task->data);
        }
        return;
    }

    task->written = 0;

    struct pollfd *fd = &sched->fds.arr[*index];
    task_list_t *tasks = &sched->task_lists.arr[*index];

    if (!task->as_first) {
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

static bool serve_read_task(task_t *task, ssize_t *res, int *err) {
    *res = read(task->fd, task->buffer, task->size);
    *err = errno;
    return true;
}
static bool serve_write_task(task_t *task, ssize_t *res, int *err) {
    *res = write(task->fd, task->buffer, task->size);
    *err = errno;

    if (*res >= 0) {
        task->written += (size_t) *res;
        // Write had completed
        if (task->written == task->size) {
            return true;
        }
        // Write hadn't completed
        else {
            return false;
        }
    }
    else {
        // Write had failed
        return true;
    }
}
static bool serve_accept_connection_task(task_t *task, ssize_t *res, int *err) {
    *res = 0;
    *err = 0;
    return true;
}
static bool serve_wait_connection_task(task_t *task, ssize_t *res, int *err) {
    *res = 0;
    *err = 0;
    return true;
}


static void process_pending_tasks(aio_scheduler_t *sched) {
    for (task_t *task = sched->pending_tasks.first->next; task != NULL; task = sched->pending_tasks.first->next) {
        task_list_delete(&sched->pending_tasks, sched->pending_tasks.first, task);
        switch (task->type) {
            case DELEGATE:
                delegate(sched, task);
                break;
            case UNDELEGATE:
                undelegate(sched, task);
                break;
            default:
                schedule_pending(sched, task);
                break;
        }
    }
}

void aio_scheduler_schedule(aio_scheduler_t *sched, task_t *task) {
    task_list_append(&sched->pending_tasks, task);
}

static inline void aio_delete_task(struct pollfd *pollfd, task_list_t *tasks, task_t *prev, task_t *this) {
    task_list_delete(tasks, prev, this);

    if (tasks->reads_amount == 0) {
        pollfd->events &= ~POLLIN;
    }
    if (tasks->writes_amount == 0) {
        pollfd->events &= ~POLLOUT;
    }
}

static void aio_proceed_tasks(struct pollfd *pollfd, task_list_t *tasks) {
    bool written = false;
    bool readen = false;

    task_t *prev = tasks->first;

    for (task_t *cursor = prev->next; cursor != NULL;) {
        ssize_t res = 0;
        int err = 0;
        bool complete = false;
        switch (cursor->type) {
            case ACCEPT_CONNECTION_REQUESTS:
                if (pollfd->revents & POLLIN && !readen) {
                    readen = true;
                    complete = serve_accept_connection_task(cursor, &res, &err);
                }
                break;
            case READ_REQUEST:
                if (pollfd->revents & POLLIN && !readen) {
                    readen = true;
                    complete = serve_read_task(cursor, &res, &err);
                }
                break;
            case WRITE_REQUEST:
                if (pollfd->revents & POLLOUT && !written) {
                    written = true;
                    complete = serve_write_task(cursor, &res, &err);
                }
                break;
            case WAIT_FOR_CONNECTION:
                if (pollfd->revents & POLLOUT && !written) {
                    written = true;
                    complete = serve_wait_connection_task(cursor, &res, &err);
                }
                break;
            default:
                // Unreachable
                break;
        }
        if (complete) {
            aio_delete_task(pollfd, tasks, prev, cursor);
            cursor->callback(res, err, cursor->data);
            cursor = prev->next;
        }
        else {
            prev = cursor;
            cursor = cursor->next;
        }
    }
}

void aio_scheduler_proceed(aio_scheduler_t *sched) {
    process_pending_tasks(sched);

    fprintf(stderr, "Poll %zu\n", sched->fds.size);
    poll(sched->fds.arr, sched->fds.size, -1);

    for (size_t i = 0; i < sched->fds.size; i++) {
        aio_proceed_tasks(&sched->fds.arr[i], &sched->task_lists.arr[i]);
    }
}

void aio_scheduler_destruct(aio_scheduler_t *sched) {
}

