#include <poll.h>
#include "scheduler/task_list.h"

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

typedef struct aio_scheduler {
    vector_pollfd_t fds;

    // TODO: use fd to task_list instead
    vector_task_list_t task_lists;
} aio_scheduler_t;

void aio_scheduler_schedule(task_t *task);
void aio_scheduler_proceed();
void aio_scheduler_destruct();
