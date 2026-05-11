#include <stdio.h>
#include <stdlib.h>
#include "cache/cache.h"

static map_uri_cache_entry_ptr_t cache;

void cache_init() {
    cache = (map_uri_cache_entry_ptr_t) HASHMAP_INITIALIZER;
}

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

void cache_entry_put(cache_entry_t *entry) {
    entry->references--;

    if (entry->references != 0) {
        return;
    }

    free(entry->uri.buffer);
    cache_block_t *cursor = entry->first_block;
    while (cursor != NULL) {
        cache_block_t *next = cursor->next;

        if (cursor->external) {
            free(((cache_block_external_t*) cursor)->data);
        }
        free(cursor);

        cursor = next;
    }
    free(entry);
}

int cache_enchache(uri_t uri, cache_entry_t *entry) {
    entry->references++;
    return map_uri_cache_entry_ptr_t_set(&cache, uri, entry);
}

void cache_delete(uri_t uri) {
    cache_entry_t **ptr = map_uri_cache_entry_ptr_t_get(&cache, uri);
    if (ptr != NULL) {
        cache_entry_put(*ptr);
        map_uri_cache_entry_ptr_t_remove(&cache, uri);
    }
}

cache_entry_t* cache_get_ref(uri_t uri) {
    cache_entry_t **ptr = map_uri_cache_entry_ptr_t_get(&cache, uri);
    if (ptr == NULL) {
        return NULL;
    }
    else {
        (*ptr)->references++;
        return *ptr;
    }
}

void cache_destruct() {
    for (size_t i = 0; i < cache.size; i++) {
        if (cache.arr[i].type == VALUE_NODE) {
            cache_entry_put(cache.arr[i].value);
        }
    }
    map_uri_cache_entry_ptr_t_destruct(cache);
}
