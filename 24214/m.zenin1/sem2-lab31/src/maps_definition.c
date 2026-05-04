#include <stddef.h>
#include <string.h>
#include "http.h"
#include "cache/cache.h"

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
#undef NAME

size_t uri_t_hash(uri_t uri) {
    size_t hash = 0;

    for(char *cursor = uri.hostname; *cursor != '\0'; cursor++) {
        hash += *cursor;
        hash += hash << 10;
        hash ^= hash >> 6;
    }
    for(char *cursor = uri.port; *cursor != '\0'; cursor++) {
        hash += *cursor;
        hash += hash << 10;
        hash ^= hash >> 6;
    }
    for(char *cursor = uri.path; *cursor != '\0'; cursor++) {
        hash += *cursor;
        hash += hash << 10;
        hash ^= hash >> 6;
    }

    hash += hash << 3;
    hash ^= hash >> 11;
    hash += hash << 15;
    
    return hash;
}

int uri_t_cmp(uri_t uri_l, uri_t uri_r) {
    int cmp = strcmp(uri_l.hostname, uri_r.hostname);
    if (cmp) {
        return cmp;
    }

    cmp = strcmp(uri_l.port, uri_r.port);
    if (cmp) {
        return cmp;
    }

    return strcmp(uri_l.path, uri_r.path);
}

#define NAME map_uri_cache_entry_ptr_t
#define KEY_TYPE uri_t
#define VALUE_TYPE cache_entry_ptr_t
#define HASHMAP_IMPL
#include "template/hashmap.h"
#undef KEY_TYPE
#undef VALUE_TYPE
#undef HASHMAP_IMPL
#undef NAME
