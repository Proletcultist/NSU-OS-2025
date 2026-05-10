#include <stdlib.h>
#include "cache/cache.h"

static map_uri_cache_entry_ptr_t cache = (map_uri_cache_entry_ptr_t) HASHMAP_INITIALIZER;

void cache_entry_add_pending(cache_entry_t *entry, proxy_client_t *client) {
    client->next = entry->pending;
    entry->pending = client;
}

void cache_entry_occupy_last_block(cache_entry_t *entry, size_t size) {
    entry->last_block->size += size;
}

void cache_entry_add_block(cache_entry_t *entry, cache_block_t *block) {
    block->next = NULL;
    if (entry->first_block == NULL) {
        entry->first_block = block;
        entry->last_block = block;
    }
    else {
        entry->last_block->next = block;
        entry->last_block = block;
    }
}

void cache_enchache(uri_t uri, cache_entry_t *entry) {
    map_uri_cache_entry_ptr_t_set(&cache, uri, entry);
}

void cache_delete(uri_t uri) {
    map_uri_cache_entry_ptr_t_remove(&cache, uri);
}

cache_entry_t* cache_lookup(uri_t uri) {
    cache_entry_t **ptr = map_uri_cache_entry_ptr_t_get(&cache, uri);
    return ptr == NULL ? NULL : *ptr;
}
