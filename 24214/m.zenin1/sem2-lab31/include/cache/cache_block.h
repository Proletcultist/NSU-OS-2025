#pragma once

#define get_cache_block_buffer(cache_block_ptr) ((cache_block_ptr)->external ? ((cache_block_external_t*)(cache_block_ptr))->data : ((char*) &(((cache_block_in_place_t*)(cache_block_ptr))->data)))

typedef struct cache_block {
    bool external;
    struct cache_block *next;
    size_t size, cap;
    bool finished;
} cache_block_t;

typedef struct cache_block_in_place {
    bool external;
    struct cache_block *next;
    size_t size, cap;
    bool finished;

    char data[];
} cache_block_in_place_t;

typedef struct cache_block_external {
    bool external;
    struct cache_block *next;
    size_t size, cap;
    bool finished;

    char *data;
} cache_block_external_t;

