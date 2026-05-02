#pragma once

typedef struct cache_block {
    struct cache_block *next;
    bool last;
    bool unfinished;
    size_t size, cap;
    char data[];
} cache_block_t;

