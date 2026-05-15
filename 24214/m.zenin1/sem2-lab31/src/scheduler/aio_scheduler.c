#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include "scheduler/aio_scheduler.h"

static char void_buf[32];

int aio_scheduler_construct(aio_scheduler_t *sched) {
    *sched = (aio_scheduler_t) {
      .fdToIndex = (map_int_size_t) HASHMAP_INITIALIZER,
      .fds = (vector_pollfd_t) VECTOR_INITIALIZER,
      .task_lists = (vector_task_list_t) VECTOR_INITIALIZER,
      .timers = (vector_timer_t) VECTOR_INITIALIZER,
      .pending_signals = 0,
      .signal_handlers = {NULL, NULL}, 
      .pending_tasks = {NULL, NULL}
    };

    int pipes[2];

    if (pipe(pipes)) {
        goto aio_scheduler_construct_defer_0;
    }
    int flags = fcntl(pipes[0], F_GETFL, 0);
    fcntl(pipes[0], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(pipes[1], F_GETFL, 0);
    fcntl(pipes[1], F_SETFL, flags | O_NONBLOCK);

    sched->signals_pipe = pipes[1];

    struct pollfd sig_pollfd = {
        .fd = pipes[0],
        .events = POLLIN,
        .revents = 0
    };

    if (vector_pollfd_t_push(&sched->fds, sig_pollfd) == -1) {
        goto aio_scheduler_construct_defer_1;
    }
    if (vector_task_list_t_reserve(&sched->task_lists, 1)) {
        goto aio_scheduler_construct_defer_2;
    }
    sched->task_lists.size = 1;
 
    return 0;

aio_scheduler_construct_defer_2:
    vector_pollfd_t_destruct(&sched->fds);
aio_scheduler_construct_defer_1:
    close(pipes[0]);
    close(pipes[1]);
aio_scheduler_construct_defer_0:
    return -1;
}

static inline void aio_delete_io_task(aio_scheduler_t *sched, struct pollfd *pollfd, task_list_t *tasks, task_t *prev, task_t *this) {
    task_list_delete(tasks, prev, this);

    if (tasks->reads_amount == 0) {
        pollfd->events &= ~POLLIN;
    }
    if (tasks->writes_amount == 0) {
        pollfd->events &= ~POLLOUT;
    }
}

static void delegate(aio_scheduler_t *sched, task_t *task) {
    ssize_t res = vector_pollfd_t_push(&sched->fds, (struct pollfd) {
      .fd = task->attrs.ctl.fd,
      .events = 0,
      .revents = 0
    });

    if (res == -1) {
        if (task->attrs.ctl.callback) {
            task->attrs.ctl.callback(ENOMEM, task->attrs.ctl.data);
        }
        return;
    }

    task_list_t new_tl;
    if (task_list_construct(&new_tl)) {
        vector_pollfd_t_pop(&sched->fds);
        if (task->attrs.ctl.callback) {
            task->attrs.ctl.callback(ENOMEM, task->attrs.ctl.data);
        }
        return;
    }

    res = vector_task_list_t_push(&sched->task_lists, new_tl);
    if (res == -1) {
        vector_pollfd_t_pop(&sched->fds);
        task_list_destruct(&new_tl);
        if (task->attrs.ctl.callback) {
            task->attrs.ctl.callback(ENOMEM, task->attrs.ctl.data);
        }
        return;
    }

    if (map_int_size_t_set(&sched->fdToIndex, task->attrs.ctl.fd, sched->fds.size - 1)) {
        vector_task_list_t_pop(&sched->task_lists);
        vector_pollfd_t_pop(&sched->fds);
        task_list_destruct(&new_tl);
        if (task->attrs.ctl.callback) {
            task->attrs.ctl.callback(ENOMEM, task->attrs.ctl.data);
        }
        return;
    }
    
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
    struct pollfd *pollfd = &sched->fds.arr[*index];
    for (task_t *cursor = task_list->first->next; cursor != NULL; cursor = task_list->first->next) {
        aio_delete_io_task(sched, pollfd, task_list, task_list->first, cursor);

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
    if (vector_timer_t_push(&sched->timers, task->attrs.timer) == -1) {
        if (task->attrs.timer.callback) {
            task->attrs.timer.callback(ENOMEM, 0, task->attrs.timer.data);
            return;
        }
    }
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
    do {
        *res = read(task->attrs.io.fd, task->attrs.io.buffer, task->attrs.io.size);
    } while (*res < 0 && errno == EINTR);

    *err = errno;
    return true;
}
static bool serve_write_task(task_t *task, ssize_t *res, int *err) {
    if (task->attrs.io.size == 0) {
        *res = 0;
        *err = 0;
        return true;
    }

    do {
        *res = write(task->attrs.io.fd, task->attrs.io.buffer, task->attrs.io.size);
    } while (*res < 0 && errno == EINTR);

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
    task_t *task = sched->pending_tasks[0];
    sched->pending_tasks[0] = NULL;
    sched->pending_tasks[1] = NULL;

    while (task != NULL) {
        task_t *next = task->next;

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
            case ACCEPT_CONNECTION_REQUESTS:
            case WAIT_FOR_CONNECTION:
            case READ_REQUEST:
            case WRITE_REQUEST:
                schedule_io(sched, task);
                break;
        }

        task = next;
    }
}

void aio_scheduler_schedule_all(aio_scheduler_t *sched, task_t *task) {

    if (sched->pending_tasks[0] == NULL) {
        sched->pending_tasks[0] = task;
    }
    else {
        sched->pending_tasks[1]->next = task;
    }

    while (task->next != NULL) {
        task = task->next;
    }

    sched->pending_tasks[1] = task;

    int write_err;
    do {
        write_err = write(sched->signals_pipe, "1", 1);
    } while (write_err < 0 && errno == EINTR);
}

void aio_scheduler_schedule(aio_scheduler_t *sched, task_t *task) {
    task->next = NULL;

    aio_scheduler_schedule_all(sched, task);
}

void aio_add_signal_handler(aio_scheduler_t *sched, signal_handler_t *handler) {
    handler->next = NULL;

    if (sched->signal_handlers[0] == NULL) {
        sched->signal_handlers[0] = handler;
    }
    else {
        sched->signal_handlers[1]->next = handler;
    }

    sched->signal_handlers[1] = handler;
}

static void check_signals(aio_scheduler_t *sched) {
    signal_handler_t *handler = sched->signal_handlers[0];
    sched->pending_tasks[0] = NULL;
    sched->pending_tasks[1] = NULL;

    while (handler != NULL) {
        signal_handler_t *next = handler->next;

        if (((1u << handler->signum) & sched->pending_signals) && handler->callback) {
            handler->callback(0, handler->data);
        }
        else {
            aio_add_signal_handler(sched, handler);           
        }

        handler = next;
    }

    sched->pending_signals = 0;
}

static void clear_signals_pipe(aio_scheduler_t *sched) {
    int read_err;
    do {
        read_err = read(sched->fds.arr[0].fd, void_buf, sizeof(void_buf));
    } while (read_err > 0 || (read_err < 0 && errno == EINTR));
}

int aio_signal(aio_scheduler_t *sched, uint8_t signum) {
    if (signum >= sizeof(sched->pending_signals)) {
        return -1;
    }

    sched->pending_signals |= (1u << signum);

    int write_err;
    do {
        write_err = write(sched->signals_pipe, "1", 1);
    } while (write_err < 0 && errno == EINTR);

    return 0;
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

static void check_timers(aio_scheduler_t *sched) {
    while (sched->timers.size > 0 && sched->loop_time >= sched->timers.arr[0].time) {
        if (sched->timers.arr[0].callback) {
            sched->timers.arr[0].callback(0, sched->loop_time, sched->timers.arr[0].data);
        }
        delete_first_timer(sched);
    }
}

static void aio_proceed_io_tasks(aio_scheduler_t *sched, struct pollfd *pollfd, task_list_t *tasks) {
    bool written = false;
    bool readen = false;

    task_t *prev = tasks->first;

    for (task_t *cursor = prev->next; cursor != NULL;) {
        if (pollfd->revents & POLLERR) {
            aio_delete_io_task(sched, pollfd, tasks, prev, cursor);
            if (cursor->attrs.io.callback) {
                cursor->attrs.io.callback(-1, EPIPE, cursor->attrs.io.data);
            }
            cursor = prev->next;
            continue;
        }
        else if (pollfd->revents & POLLHUP) {
            aio_delete_io_task(sched, pollfd, tasks, prev, cursor);
            if (cursor->attrs.io.callback) {
                cursor->attrs.io.callback(0, 0, cursor->attrs.io.data);
            }
            cursor = prev->next;
            continue;
        }

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
            aio_delete_io_task(sched, pollfd, tasks, prev, cursor);
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
        return 0;
    }
    // Wait for either io, timer of signal
    else {
        return (int) (sched->timers.arr[0].time - sched->loop_time) * 1000;
    }
}

static bool check_alive(aio_scheduler_t *sched, scheduler_run_mode_t run_mode) {
    if (sched->pending_tasks[0] != NULL) {
        return true;
    }
    else if (sched->fds.size == 1 && run_mode == RUN_FOR_IO) {
        return false;
    }
    else {
        return true;
    }
}

int aio_scheduler_proceed(aio_scheduler_t *sched, scheduler_run_mode_t run_mode) {

    while (check_alive(sched, run_mode)) {
        sched->loop_time = time(NULL);
        int poll_res;
        do {
            poll_res = poll(sched->fds.arr, sched->fds.size, get_timeout(sched));
        } while (poll_res < 0 && errno == EINTR);
        if (poll_res < 0) {
            return poll_res;
        }
        sched->loop_time = time(NULL);

        check_timers(sched);

        // If there is revents on fds besides the signals pipe - process i/o
        if (poll_res > (sched->fds.arr[0].revents ? 1 : 0)) {
            for (size_t i = 1; i < sched->fds.size; i++) {
                if (sched->fds.arr[i].revents & POLLNVAL) {
                    return -1;
                }
                else if (sched->fds.arr[i].revents) {
                    aio_proceed_io_tasks(sched, &sched->fds.arr[i], &sched->task_lists.arr[i]);
                }
            }
        }

        clear_signals_pipe(sched);

        process_pending_tasks(sched);

        if (sched->pending_signals) {
            check_signals(sched);
            return 1;
        }
    }

    return 0;
}

void aio_scheduler_destruct(aio_scheduler_t *sched) {
    map_int_size_t_destruct(sched->fdToIndex);

    close(sched->signals_pipe);
    close(sched->fds.arr[0].fd);

    // Cancel all io tasks
    for (size_t i = 1; i < sched->fds.size; i++) {
        task_list_t *task_list = &sched->task_lists.arr[i];
        struct pollfd *pollfd = &sched->fds.arr[i];
        for (task_t *cursor = task_list->first->next; cursor != NULL; cursor = task_list->first->next) {
            aio_delete_io_task(sched, pollfd, task_list, task_list->first, cursor);
        }
        task_list_destruct(task_list);
    }
    vector_task_list_t_destruct(&sched->task_lists);
    vector_pollfd_t_destruct(&sched->fds);

    // Cancel all pending tasks
    for (task_t *task = sched->pending_tasks[0]; task != NULL;) {
        task_t *next = task->next;

        switch (task->type) {
            case DELEGATE:
            case UNDELEGATE:
                if (task->attrs.ctl.callback) {
                    task->attrs.ctl.callback(ECANCELED, task->attrs.ctl.data);
                }
                break;
            case ADD_TIMER:
                if (task->attrs.timer.callback) {
                    task->attrs.timer.callback(ECANCELED, sched->loop_time, task->attrs.ctl.data);
                }
                break;
            case ACCEPT_CONNECTION_REQUESTS:
            case WAIT_FOR_CONNECTION:
            case READ_REQUEST:
            case WRITE_REQUEST:
                if (task->attrs.io.callback) {
                    task->attrs.io.callback(-1, ECANCELED, task->attrs.ctl.data);
                }
                break;
        }

        task = next;
    }

    // Cancel all signal handlers
    for (signal_handler_t *handler = sched->signal_handlers[0]; handler != NULL;) {
        signal_handler_t *next = handler->next;

        if (handler->callback) {
            handler->callback(ECANCELED, handler->data);
        }

        handler = next;
    }

    // Cancel all timers
    for (size_t i = 0; i < sched->timers.size; i++) {
        if (sched->timers.arr[i].callback) {
            sched->timers.arr[i].callback(ECANCELED, sched->loop_time, sched->timers.arr[i].data);
        }
    }
    vector_timer_t_destruct(&sched->timers);
}

