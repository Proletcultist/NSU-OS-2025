#include <math.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include "scheduler/aio_scheduler.h"

aio_scheduler_t aio_scheduler_construct() {
    aio_scheduler_t ret = {
                            .fds = (vector_pollfd_t) VECTOR_INITIALIZER,
                            .task_lists = (vector_task_list_t) VECTOR_INITIALIZER,
                            .timers = (vector_timer_t) VECTOR_INITIALIZER,
                            .fdToIndex = (map_int_size_t) HASHMAP_INITIALIZER
                          };
    ret.pending_tasks = task_list_construct();

    return ret;
}

static void delegate(aio_scheduler_t *sched, task_t *task) {
    vector_pollfd_t_push(&sched->fds, (struct pollfd)
                                      {
                                        .fd = task->attrs.ctl.fd,
                                        .events = 0,
                                        .revents = 0
                                      });
    vector_task_list_t_push(&sched->task_lists, task_list_construct());
    map_int_size_t_set(&sched->fdToIndex, task->attrs.ctl.fd, sched->fds.size - 1);
    
    if (task->attrs.ctl.callback) {
        task->attrs.ctl.callback(0, task->attrs.ctl.data);
    }
}
static void undelegate(aio_scheduler_t *sched, task_t *task) {
    size_t *index = map_int_size_t_get(&sched->fdToIndex, task->attrs.ctl.fd);
    if (index == NULL) {
        if (task->attrs.ctl.callback) {
            task->attrs.ctl.callback(EINVAL, task->attrs.ctl.data);
        }
        return;
    }

    // Remove mapping
    map_int_size_t_remove(&sched->fdToIndex, sched->fds.arr[*index].fd);

    // Cancel all io tasks
    task_list_t *task_list = &sched->task_lists.arr[*index];
    for (task_t *cursor = task_list->first->next; cursor != NULL; cursor = task_list->first->next) {
        task_list_delete(task_list, task_list->first, cursor);

        if (cursor->attrs.io.callback) {
            cursor->attrs.io.callback(-1, ECANCELED, cursor->attrs.io.data);
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

    if (task->attrs.ctl.callback) {
        task->attrs.ctl.callback(0, task->attrs.ctl.data);
    }
}

static void add_timer(aio_scheduler_t *sched, task_t *task) {
    vector_timer_t_push(&sched->timers, task->attrs.timer);
    sched->timers.arr[sched->timers.size - 1].time += sched->loop_time;

    // Sift up
    size_t i = sched->timers.size - 1;
    while (i > 0 && sched->timers.arr[(i - 1) / 2].time > sched->timers.arr[i].time) {
        aio_timer_t tmp = sched->timers.arr[(i - 1) / 2];
        sched->timers.arr[(i - 1) / 2] = sched->timers.arr[i];
        sched->timers.arr[i] = tmp;
        i = (i - 1) / 2;
    }
}

static void schedule_io(aio_scheduler_t *sched, task_t *task) {
    size_t *index = map_int_size_t_get(&sched->fdToIndex, task->attrs.io.fd);
    if (index == NULL){
        if (task->attrs.io.callback) {
            task->attrs.io.callback(-1, EINVAL, task->attrs.io.data);
        }
        return;
    }

    task->attrs.io.written = 0;

    struct pollfd *fd = &sched->fds.arr[*index];
    task_list_t *tasks = &sched->task_lists.arr[*index];

    if (!task->attrs.io.as_first) {
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
    *res = read(task->attrs.io.fd, task->attrs.io.buffer, task->attrs.io.size);
    *err = errno;
    return true;
}
static bool serve_write_task(task_t *task, ssize_t *res, int *err) {
    if (task->attrs.io.size == 0) {
        *res = 0;
        *err = 0;
        return true;
    }

    *res = write(task->attrs.io.fd, task->attrs.io.buffer, task->attrs.io.size);
    *err = errno;

    if (*res >= 0) {
        task->attrs.io.written += (size_t) *res;
        // Write had completed
        if (task->attrs.io.written == task->attrs.io.size) {
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
            case ADD_TIMER:
                add_timer(sched, task);
                break;
            default:
                schedule_io(sched, task);
                break;
        }
    }
}

void aio_scheduler_schedule(aio_scheduler_t *sched, task_t *task) {
    task_list_append(&sched->pending_tasks, task);
}

static inline void aio_delete_io_task(struct pollfd *pollfd, task_list_t *tasks, task_t *prev, task_t *this) {
    task_list_delete(tasks, prev, this);

    if (tasks->reads_amount == 0) {
        pollfd->events &= ~POLLIN;
    }
    if (tasks->writes_amount == 0) {
        pollfd->events &= ~POLLOUT;
    }
}

static void delete_first_timer(aio_scheduler_t *sched) {
    sched->timers.arr[0] = sched->timers.arr[sched->timers.size - 1];
    sched->timers.size--;

    // Sift down
    size_t i = 0;
    while (1) {
        size_t left = 2 * i + 1;
        size_t right = 2 * i + 2;
        size_t smallest = i;

        if (left < sched->timers.size && sched->timers.arr[left].time < sched->timers.arr[smallest].time) {
            smallest = left;
        }
        if (right < sched->timers.size && sched->timers.arr[right].time < sched->timers.arr[smallest].time) {
            smallest = right;
        }

        if (smallest != i) {
            aio_timer_t tmp = sched->timers.arr[smallest];
            sched->timers.arr[smallest] = sched->timers.arr[i];
            sched->timers.arr[i] = tmp;
            i = smallest;
        }
        else {
            break;
        }
    }
}

static void aio_check_timers(aio_scheduler_t *sched) {
    while (sched->timers.size > 0 && sched->loop_time >= sched->timers.arr[0].time) {
        if (sched->timers.arr[0].callback) {
            sched->timers.arr[0].callback(sched->loop_time, sched->timers.arr[0].data);
        }
        delete_first_timer(sched);
    }
}

static void aio_proceed_io_tasks(struct pollfd *pollfd, task_list_t *tasks) {
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
            aio_delete_io_task(pollfd, tasks, prev, cursor);
            if (cursor->attrs.io.callback) {
                cursor->attrs.io.callback(res, err, cursor->attrs.io.data);
            }
            cursor = prev->next;
        }
        else {
            prev = cursor;
            cursor = cursor->next;
        }
    }
}

static int get_timeout(aio_scheduler_t *sched) {
    // No timers - wait for io or signal
    if (sched->timers.size == 0) {
        return -1;
    }
    // First timer already expired - just check all fds for io events and don't wait
    else if (sched->timers.arr[0].time <= sched->loop_time) {
        fprintf(stderr, "Wait for 0\n");
        return 0;
    }
    // Wait for either io, timer of signal
    else {
        fprintf(stderr, "WAIT FOR %d\n", (int) (sched->timers.arr[0].time - sched->loop_time) * 1000);
        return (int) (sched->timers.arr[0].time - sched->loop_time) * 1000;
    }
}

void aio_scheduler_proceed(aio_scheduler_t *sched) {
    sched->loop_time = time(NULL);
    process_pending_tasks(sched);

    fprintf(stderr, "Poll %zu\n", sched->fds.size);
    int poll_res = poll(sched->fds.arr, sched->fds.size, get_timeout(sched));
    sched->loop_time = time(NULL);

    aio_check_timers(sched);

    if (poll_res > 0) {
        fprintf(stderr, "IO\n");
        for (size_t i = 0; i < sched->fds.size; i++) {
            aio_proceed_io_tasks(&sched->fds.arr[i], &sched->task_lists.arr[i]);
        }
    }
}

void aio_scheduler_destruct(aio_scheduler_t *sched) {
}

