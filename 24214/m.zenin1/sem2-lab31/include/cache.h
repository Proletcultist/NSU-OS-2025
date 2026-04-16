#pragma once

#include <stddef.h>
#include "exchange_buffer.h"

typedef struct cache_entry {
    char *uri;
    exchange_buffer_t buffer;
} cache_entry_t;

typedef enum node_type{
	EMPTY_NODE = 0,
	VALUE_NODE,
	TOMBSTONE_NODE
} node_type_t;

typedef struct cache_node {
    cache_entry_t entry;

    node_type_t type;
    size_t probe_start;
} cache_node_t;

typedef struct cache {
    cache_node_t *arr;
    size_t cap;
    size_t size;
} cache_t;

cache_t cache_construct();
cache_entry_t* cache_get(cache_t *cache, char *uri);
cache_entry_t* cache_put(cache_t *cache, cache_entry_t entry);
void cache_remove(cache_t *cache, char *uri);
void cache_destruct(cache_t *cache);
