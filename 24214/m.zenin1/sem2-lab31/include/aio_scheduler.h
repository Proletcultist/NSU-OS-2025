#include <poll.h>
#include <stddef.h>
#include <sys/types.h>

#define NAME vector_pollfd_t 
#define TYPE struct pollfd
#define VECTOR_DECL
#include "template/vector.h"
#undef VECTOR_DECL
#undef NAME
#undef TYPE

typedef enum task_type {
    ACCEPT_CONNECTION_REQUESTS,
    READ_REQUEST,
    WRITE_REQUEST
} task_type_t;

typedef struct task {
    task_type_t type;

    int fd;
    void *buffer;
    size_t size;

    void *data;
    void (*callback)(ssize_t, int, void*);

    struct task *next;
} task_t;

typedef struct tasks_descriptor {
    task_t *first;
    task_t *last;

    size_t reads_amount;
    size_t writes_amount;
} tasks_descriptor_t;

#define NAME vector_task_descriptor_t
#define TYPE tasks_descriptor_t
#define VECTOR_DECL
#include "template/vector.h"
#undef VECTOR_DECL
#undef NAME
#undef TYPE

typedef struct aio_scheduler {
    // fd_to_id
    vector_pollfd_t fds;
    vector_task_descriptor_t tasks;
} aio_scheduler_t;

void aio_scheduler_schedule(task_t *task);
void aio_scheduler_proceed();
void aio_scheduler_destruct();
