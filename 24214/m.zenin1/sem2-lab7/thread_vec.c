#include <stdlib.h>
#include <stdio.h>
#include "thread_vec.h"

thread_vec thread_vec_construct(){
    return (thread_vec) {.arr = NULL, .size = 0, .cap = 0};
}

int thread_vec_push(thread_vec *vec, pthread_t tid){
	if (vec->cap == 0){ 
		vec->arr = malloc(sizeof(pthread_t)); 
		vec->cap = 1; 
	} 
	else if (vec->cap == vec->size){ 
        pthread_t *tmp = realloc(vec->arr, vec->cap * 2 * sizeof(pthread_t));
        if (tmp == NULL){
            perror("Failed to enlarge threads vector");
            return -1;
        }
        vec->arr = tmp;
		vec->cap *= 2; 
	} 

	vec->arr[vec->size++] = tid; 

    return 0;
}

void thread_vec_destruct(thread_vec *vec){
    if (vec->arr != NULL){
        free(vec->arr);
    }
}
