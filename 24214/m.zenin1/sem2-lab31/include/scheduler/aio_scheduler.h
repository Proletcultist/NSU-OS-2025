#pragma once

#include <poll.h>
#include "scheduler/task_list.h"
#include "scheduler/timer.h"

#define NAME vector_pollfd_t 
#define TYPE struct pollfd
#define VECTOR_DECL
#include "template/vector.h"
#undef VECTOR_DECL
#undef NAME
#undef TYPE

#define NAME vector_task_list_t
#define TYPE task_list_t
#define VECTOR_DECL
#include "template/vector.h"
#undef VECTOR_DECL
#undef NAME
#undef TYPE

#define NAME vector_timer_t
#define TYPE aio_timer_t
#define VECTOR_DECL
#include "template/vector.h"
#undef VECTOR_DECL
#undef NAME
#undef TYPE

#define NAME map_int_size_t
#define KEY_TYPE int
#define VALUE_TYPE size_t
#define HASHMAP_DECL
#include "template/hashmap.h"
#undef KEY_TYPE
#undef VALUE_TYPE
#undef HASHMAP_DECL
#undef NAME

typedef enum scheduler_run_mode {
    RUN_NO_TIMER_WAIT,      // Poll for i/o, don't block if there are no i/o events to wait for
    RUN_DEFAULT             // Poll for i/o, if there are no i/o events to wait for, block until timer fires
} scheduler_run_mode_t;

typedef struct aio_scheduler {
    map_int_size_t fdToIndex;

    time_t loop_time;
    size_t io_events;

    vector_pollfd_t fds;
    vector_task_list_t task_lists;
    vector_timer_t timers;

    task_list_t pending_tasks;
} aio_scheduler_t;

int aio_scheduler_construct(aio_scheduler_t *sched);
void aio_scheduler_schedule(aio_scheduler_t *sched, task_t *task);
bool aio_scheduler_proceed(aio_scheduler_t *sched, scheduler_run_mode_t run_mode);
void aio_scheduler_destruct(aio_scheduler_t *sched);
