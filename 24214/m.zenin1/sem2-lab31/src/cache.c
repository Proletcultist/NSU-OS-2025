#include <stdlib.h>
#include <memory.h>
#include <inttypes.h>
#include "cache.h"

#define INITIAL_HM_SIZE 1 // Prime number
#define HM_EXTENSION 2
#define MAX_HM_LOAD 90 // % 

static void cache_extend(cache_t *self);

size_t hash_string(char *str) {
    size_t hash = 0;
    while (*str) {
        hash += *str;
        hash += hash << 10;
        hash ^= hash >> 6;

        str++;
    }
    hash += hash << 3;
    hash ^= hash >> 11;
    hash += hash << 15;
    return hash;
}

int isPrime(size_t n){
	for (size_t i = 2;i * i <= n;i++){
		if (n % i == 0){
			return 0;
		}
	}
	return 1;
}

size_t nextPrime(size_t n){
	while (!isPrime(n)){
		n++;
	}
	return n;
}

void swapSize_t(size_t *l, size_t *r){
	*l ^= *r;
	*r ^= *l;
	*l ^= *r;
}

size_t calcProbDist(size_t pos, size_t probe_start, size_t size){
	if (pos >= probe_start){
		return pos - probe_start;
	}
	else{
		return (size - probe_start + pos);
	}
}

static void cache_extend(cache_t *self){
	cache_node_t *old_arr = self->arr;
	size_t old_cap = self->cap;
	self->cap = nextPrime(self->cap * HM_EXTENSION);
	self->arr = calloc(self->cap, sizeof(cache_node_t));

	size_t elems = self->size;
	self->size = 0;

	for (size_t i = 0; i < old_cap && elems;i++){
		// Not empty nor tombstone
		if (old_arr[i].type == VALUE_NODE){
			elems--;
			cache_put(self, old_arr[i].entry);
		}
	}

	free(old_arr);
}

void cache_check_load(cache_t *self){
	if (self->cap == 0){
		self->arr = calloc(INITIAL_HM_SIZE, sizeof(cache_node_t));
		self->cap = INITIAL_HM_SIZE;
	}
	else if (self->size >= self->cap / 100 * MAX_HM_LOAD){
		cache_extend(self);
	}
}

cache_t cache_construct() {
    return (cache_t) {.arr = NULL, .cap = 0, .size = 0};
}

cache_entry_t* cache_put(cache_t *cache, cache_entry_t entry) {
	cache_check_load(cache);
	cache->size++;

	size_t hash = hash_string(entry.uri) % cache->cap;
	size_t probe_dist = 0;
	for (size_t pos = hash;;pos = (pos + 1) % cache->cap){

		// Empty place
		if (cache->arr[pos].type == EMPTY_NODE){
			cache->arr[pos] = (cache_node_t){.entry = entry, .type = VALUE_NODE, .probe_start = hash};
			return &cache->arr[pos].entry;
		}

		// Current elem has prob sequence shorter than new elem
		if (calcProbDist(pos, cache->arr[pos].probe_start, cache->cap) < probe_dist){
			// Is tombstone
			if (cache->arr[pos].type == TOMBSTONE_NODE){
                cache->arr[pos] = (cache_node_t){.entry = entry, .type = VALUE_NODE, .probe_start = hash};
                return &cache->arr[pos].entry;
			}
			
			// Swaping current and new and continue to try find new place for current
			probe_dist = calcProbDist(pos, cache->arr[pos].probe_start, cache->cap);
            cache_entry_t buff = cache->arr[pos].entry;
            cache->arr[pos].entry = entry;
            entry = buff;
			swapSize_t(&cache->arr[pos].probe_start, &hash);

			probe_dist++;
			continue;
		}
		
		// Element with such key already in table
		if (cache->arr[pos].type != TOMBSTONE_NODE && cache->arr[pos].probe_start == hash && strcmp(cache->arr[pos].entry.uri, entry.uri) == 0){
			cache->arr[pos].entry = entry;
			cache->size--;
            return &cache->arr[pos].entry;
		}

		probe_dist++;
	}
}

cache_entry_t* cache_get(cache_t *cache, char *uri) {
	if (cache->cap == 0 || cache->size == 0){
		return NULL;
	}

	size_t hash = hash_string(uri) % cache->cap;
	size_t probe_dist = 0;
	
	for (size_t pos = hash;;pos = (pos + 1) % cache->cap){
		// Empty place
		if (cache->arr[pos].type == EMPTY_NODE){
			return NULL;
		}
		// Current elem has prob sequence shorter than new elem
		else if (calcProbDist(pos, cache->arr[pos].probe_start, cache->cap) < probe_dist){
			return NULL;	
		}
		// Element with such key already in table
		else if (cache->arr[pos].type != TOMBSTONE_NODE && cache->arr[pos].probe_start == hash && strcmp(uri, cache->arr[pos].entry.uri) == 0){
			return &cache->arr[pos].entry;
		}
		probe_dist++;
	}
}

void cache_remove(cache_t *cache, char *uri) {
	if (cache->cap == 0 || cache->size == 0){
		return;
	}

	size_t hash = hash_string(uri) % cache->cap;
	size_t probe_dist = 0;
	
	for (size_t pos = hash;;pos = (pos + 1) % cache->cap){
		// Empty place
		if (cache->arr[pos].type == EMPTY_NODE){
			return;
		}
		// Current elem has prob sequence shorter than new elem
		else if (calcProbDist(pos, cache->arr[pos].probe_start, cache->cap) < probe_dist){
			return;	
		}
		// Element with such key already in table
		else if (cache->arr[pos].type != TOMBSTONE_NODE && cache->arr[pos].probe_start == hash && strcmp(cache->arr[pos].entry.uri, uri) == 0){
			// Make tombstone
			cache->arr[pos].type = TOMBSTONE_NODE;
			cache->size--;
			return;
		}
		probe_dist++;
	}
}

void cache_destruct(cache_t *cache) {
    // TODO: Destruct all nodes
	if (cache->cap != 0){	
		free(cache->arr);
		cache->cap = 0;
		cache->size = 0;
	}
}
