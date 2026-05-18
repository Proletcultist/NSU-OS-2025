#pragma once

#include <stdatomic.h>
#include <poll.h>
#include <stdint.h>
#include <signal.h>
#include "scheduler/task_list.h"
#include "scheduler/timer.h"
#include "scheduler/aio_signal.h"

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
    RUN_FOR_IO,     // Run till there is no delegated filedescs or signal fires
    RUN_DEFAULT     // Run till the first signal fires
} scheduler_run_mode_t;

typedef struct aio_scheduler {
    map_int_size_t fdToIndex;

    time_t loop_time;

    vector_pollfd_t fds;
    vector_task_list_t task_lists;
    vector_timer_t timers;

    int signals_pipe;
    atomic_uint_fast32_t pending_signals;
    signal_handler_t *signal_handlers[2];

    task_t *pending_tasks[2];
} aio_scheduler_t;

// Not MT-Safe, not Async Signal safe
int aio_scheduler_construct(aio_scheduler_t *sched);
void aio_add_signal_handler(aio_scheduler_t *sched, signal_handler_t *handler);
// 0 - loop died, > 0 - signal occured , < 0 - error
int aio_scheduler_proceed(aio_scheduler_t *sched, scheduler_run_mode_t run_mode);
void aio_scheduler_destruct(aio_scheduler_t *sched);
void aio_scheduler_schedule_all(aio_scheduler_t *sched, task_t *task);
void aio_scheduler_schedule(aio_scheduler_t *sched, task_t *task);

// Async-signal safe
int aio_signal(aio_scheduler_t *sched, uint8_t signum);
