#pragma once

#include "http.h"
#include "proxy/client.h"
#include "cache/cache_block.h"

#define CACHE_ENTRY_INITIALIZER ((cache_entry_t) \
                                 { \
                                    .entry_size = 0, \
                                    .first_block = NULL, \
                                    .last_block = NULL, \
                                    .pending = NULL \
                                 })

struct proxy_client;

typedef struct cache_entry {
    uri_t uri;
    size_t references;
    size_t entry_size;

    cache_block_t *first_block;
    cache_block_t *last_block;
    struct proxy_client *pending;
} cache_entry_t;

void cache_entry_put(cache_entry_t *entry);
void cache_entry_add_pending(cache_entry_t *entry, struct proxy_client *client);
void cache_entry_occupy_last_block(cache_entry_t *entry, size_t size);
void cache_entry_add_block(cache_entry_t *entry, cache_block_t *block);

typedef cache_entry_t *cache_entry_ptr_t;

#define NAME map_uri_cache_entry_ptr_t
#define KEY_TYPE uri_t
#define VALUE_TYPE cache_entry_ptr_t
#define HASHMAP_DECL
#include "template/hashmap.h"
#undef KEY_TYPE
#undef VALUE_TYPE
#undef HASHMAP_DECL
#undef NAME

void cache_init(ssize_t cache_cap);
cache_entry_t* cache_encache_or_get_ref(uri_t uri, cache_entry_t *entry);
void commit_entry(cache_entry_t *entry);
void cache_delete(uri_t uri);
void cache_destruct();
