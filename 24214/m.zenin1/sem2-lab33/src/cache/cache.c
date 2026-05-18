#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include "cache/cache.h"
#include "proxy/util.h"

static ssize_t cache_cap;
static size_t cache_size;
static map_uri_cache_entry_ptr_t cache;
static pthread_mutex_t cache_mtx = PTHREAD_MUTEX_INITIALIZER;

void cache_init(ssize_t c_cap) {
    cache = (map_uri_cache_entry_ptr_t) HASHMAP_INITIALIZER;
    cache_cap = c_cap;
    cache_size = 0;
}

bool commit_entry(cache_entry_t *entry) {
    pthread_mutex_lock(&cache_mtx);

    // If there is no upper bound for cache size - do nothing
    if (cache_cap == -1) {
        pthread_mutex_unlock(&cache_mtx);
        return true;
    }

    // If entry is too big - delete it
    if (cache_size + entry->entry_size <= (size_t) cache_cap) {
        cache_size += entry->entry_size;

        pthread_mutex_unlock(&cache_mtx);
        return true;
    }
    else {
        map_uri_cache_entry_ptr_t_remove(&cache, entry->uri);
        cache_entry_put(entry);

        pthread_mutex_unlock(&cache_mtx);
        return false;
    }
}

void cache_expired_callback(int err, time_t time, void *udata) {
    cache_expire_timer_t *timer = udata;

    if (err == ENOMEM) {
        panic("Out of memory");
    }
    else if (err != ECANCELED) {
        cache_delete(timer->entry->uri);
    }

    free(timer);
}

cache_entry_t* cache_encache_or_get_ref(uri_t uri, cache_entry_t *entry) {
    pthread_mutex_lock(&cache_mtx);

    cache_entry_t *ret = NULL;

    cache_entry_t **ptr = map_uri_cache_entry_ptr_t_get(&cache, uri);
    if (ptr == NULL) {
        if (map_uri_cache_entry_ptr_t_set(&cache, uri, entry)) {
            ret = NULL;
        }
        else {
            __atomic_add_fetch(&entry->references, 1, __ATOMIC_RELAXED);
            ret = entry;
        }
    }
    else {
        __atomic_add_fetch(&((*ptr)->references), 1, __ATOMIC_RELAXED);
        ret = *ptr;
    }

    pthread_mutex_unlock(&cache_mtx);
    return ret;
}

void cache_entry_add_pending(cache_entry_t *entry, proxy_client_t *client) {
    client->next = entry->pending;
    entry->pending = client;
}

void cache_entry_occupy_last_block(cache_entry_t *entry, size_t size) {
    entry->entry_size += size;
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
    if (__atomic_sub_fetch(&entry->references, 1, __ATOMIC_RELAXED) != 0) {
        return;
    }

    free(entry->uri.buffer);
    cache_block_t *cursor = entry->first_block;
    while (cursor != NULL) {
        cache_block_t *next = cursor->next;

        if (cursor->type == EXTERNAL_CACHE_BLOCK) {
            free(((cache_block_external_t*) cursor)->data);
        }
        free(cursor);

        cursor = next;
    }
    free(entry);
}

void cache_delete(uri_t uri) {
    pthread_mutex_lock(&cache_mtx);

    cache_entry_t **ptr = map_uri_cache_entry_ptr_t_get(&cache, uri);
    if (ptr != NULL) {
        map_uri_cache_entry_ptr_t_remove(&cache, uri);
        cache_entry_put(*ptr);
    }

    pthread_mutex_unlock(&cache_mtx);
}

void cache_destruct() {
    for (size_t i = 0; i < cache.size; i++) {
        if (cache.arr[i].type == VALUE_NODE) {
            cache_entry_put(cache.arr[i].value);
        }
    }
    map_uri_cache_entry_ptr_t_destruct(cache);

    pthread_mutex_destroy(&cache_mtx);
}

