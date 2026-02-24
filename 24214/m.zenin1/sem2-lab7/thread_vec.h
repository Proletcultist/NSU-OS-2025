#pragma once

#include <pthread.h>

typedef struct thread_vec {
    pthread_t *arr;
    size_t size, cap;
} thread_vec;

thread_vec thread_vec_construct();
int thread_vec_push(thread_vec *vec, pthread_t tid);
void thread_vec_destruct(thread_vec *vec);
