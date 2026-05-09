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

typedef struct aio_scheduler {
    map_int_size_t fdToIndex;

    time_t loop_time;

    vector_pollfd_t fds;
    vector_task_list_t task_lists;
    vector_timer_t timers;

    task_list_t pending_tasks;
} aio_scheduler_t;

aio_scheduler_t aio_scheduler_construct();
void aio_scheduler_schedule(aio_scheduler_t *sched, task_t *task);
void aio_scheduler_proceed(aio_scheduler_t *sched);
void aio_scheduler_destruct(aio_scheduler_t *sched);
