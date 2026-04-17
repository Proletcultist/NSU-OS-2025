#include <stddef.h>
#include "scheduler/task_list.h"

size_t int_hash(int i) {
    return (size_t) i;
}

int int_cmp(int l, int r) {
    return l == r ? 0 : (l > r ? 1 : -1);
}

#define NAME map_int_size_t
#define KEY_TYPE int
#define VALUE_TYPE size_t
#define HASHMAP_DECL
#define HASHMAP_IMPL
#include "template/hashmap.h"
#undef HASHMAP_DECL
#undef HASHMAP_IMPL
#undef KEY_TYPE
#undef VALUE_TYPE
