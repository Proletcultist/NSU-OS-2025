#include <poll.h>
#include "task.h"

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
#define VECTOR_DECL
#define VECTOR_IMPL
#include "template/vector.h"
#undef VECTOR_DECL
#undef VECTOR_IMPL
#undef NAME
#undef TYPE

#define NAME vector_task_t_ptr
#define TYPE task_t*
#define VECTOR_DECL
#define VECTOR_IMPL
#include "template/vector.h"
#undef VECTOR_DECL
#undef VECTOR_IMPL
#undef NAME
#undef TYPE
