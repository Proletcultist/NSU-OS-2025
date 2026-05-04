#include <stdlib.h>
#include "cache/cache.h"

static map_uri_cache_entry_ptr_t cache = (map_uri_cache_entry_ptr_t) HASHMAP_INITIALIZER;

void cache_entry_add_pending(cache_entry_t *entry, int fd) {
    pending_client_t *new = malloc(sizeof(pending_client_t));
    *new = (pending_client_t)
           {
               .fd = fd,
               .next = entry->pending
           };
    entry->pending = new;
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

cache_entry_t* cache_lookup(uri_t uri) {
    cache_entry_t **ptr = map_uri_cache_entry_ptr_t_get(&cache, uri);
    return ptr == NULL ? NULL : *ptr;
}
