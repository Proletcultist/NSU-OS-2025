#pragma once

#define get_cache_block_buffer(cache_block_ptr) ((cache_block_ptr)->type == IN_PLACE_CACHE_BLOCK \
                                                 ? ((char*) &(((cache_block_in_place_t*)(cache_block_ptr))->data)) \
                                                 : ((cache_block_external_t*)(cache_block_ptr))->data)

typedef enum cache_block_type {
    IN_PLACE_CACHE_BLOCK,
    EXTERNAL_CACHE_BLOCK,
    STATIC_EXTERNAL_CACHE_BLOCK
} cache_block_type_t;

typedef struct cache_block {
    cache_block_type_t type;
    struct cache_block *next;
    size_t size, cap;
    bool finished;
} cache_block_t;

typedef struct cache_block_in_place {
    cache_block_type_t type;
    struct cache_block *next;
    size_t size, cap;
    bool finished;

    char data[];
} cache_block_in_place_t;

typedef struct cache_block_external {
    cache_block_type_t type;
    struct cache_block *next;
    size_t size, cap;
    bool finished;

    char *data;
} cache_block_external_t;
