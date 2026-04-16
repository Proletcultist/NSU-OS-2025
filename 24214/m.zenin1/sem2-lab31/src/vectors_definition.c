#include <poll.h>
#include "aio_scheduler.h"

#define NAME vector_byte_t
#define TYPE char
#define VECTOR_DECL
#define VECTOR_IMPL
#include "template/vector.h"
#undef VECTOR_DECL
#undef VECTOR_IMPL
#undef NAME
#undef TYPE

#define NAME vector_pollfd_t 
#define TYPE struct pollfd
#define VECTOR_IMPL
#include "template/vector.h"
#undef VECTOR_DECL
#undef VECTOR_IMPL
#undef NAME
#undef TYPE

#define NAME vector_task_descriptor_t
#define TYPE tasks_descriptor_t
#define VECTOR_IMPL
#include "template/vector.h"
#undef VECTOR_DECL
#undef VECTOR_IMPL
#undef NAME
#undef TYPE
