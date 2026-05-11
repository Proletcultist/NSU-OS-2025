#ifndef _CONCAT
	#define _CONCAT(a,b) a##b
#endif
#ifndef CONCAT
	#define CONCAT(a,b) _CONCAT(a,b)
#endif


#ifdef VECTOR_DECL
#include <stddef.h>
#include <sys/types.h>

#ifndef VECTOR_INITIALIZER
    #define VECTOR_INITIALIZER {NULL, 0, 0}
#endif

typedef struct NAME{
	TYPE *arr;
	size_t cap, size;
} NAME;

NAME CONCAT(NAME, _construct)();
void CONCAT(NAME, _destruct)(NAME *vec);
ssize_t CONCAT(NAME, _push)(NAME *vec, TYPE val);
TYPE CONCAT(NAME, _pop)(NAME *vec);
int CONCAT(NAME, _reserve)(NAME *vec, size_t cap);
void CONCAT(NAME, _resize)(NAME *vec, size_t newCnt, TYPE fill);
void CONCAT(NAME, _insert)(NAME *vec, size_t where, TYPE *arr, size_t num);
void CONCAT(NAME, _erase)(NAME *vec, size_t where, size_t num);
#endif

#ifdef VECTOR_IMPL
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <memory.h>

NAME CONCAT(NAME, _construct)(){ 
	return (NAME){NULL, 0, 0}; 
} 

void CONCAT(NAME, _destruct)(NAME *vec){ 
	if (vec->arr != NULL){ 
		free(vec->arr); 
		vec->arr = NULL; 
		vec->cap = 0; 
		vec->size = 0; 
	} 
} 

ssize_t CONCAT(NAME, _push)(NAME *vec, TYPE val){ 
	if (vec->cap == 0){ 
		vec->arr = malloc(sizeof(TYPE)); 
        if (vec->arr == NULL) {
            return -1;
        }
		vec->cap = 1; 
	} 
	else if (vec->cap == vec->size){ 
        void *tmp = realloc(vec->arr, vec->cap * 2 * sizeof(TYPE));
        if (tmp == NULL) {
            return -1;
        }
		vec->arr = tmp; 
		vec->cap *= 2; 
	} 
	vec->arr[vec->size] = val; 

	return vec->size++; 
} 

TYPE CONCAT(NAME, _pop)(NAME *vec){ 
	return vec->arr[--vec->size]; 
} 

int CONCAT(NAME, _reserve)(NAME *vec, size_t cap){ 
	if (vec->cap < cap){ 
        size_t new_cap = vec->cap * 2 > cap ? vec->cap * 2 : cap;
        void *tmp = realloc(vec->arr, new_cap * sizeof(TYPE));
        if (tmp == NULL) {
            return -1;
        }
        vec->arr = tmp; 
        vec->cap = new_cap; 
	}

    return 0;
} 

void CONCAT(NAME, _resize)(NAME *vec, size_t newCnt, TYPE fill){ 
	CONCAT(NAME, _reserve)(vec, newCnt); 

	while (vec->size < newCnt){ 
		vec->arr[vec->size++] = fill; 
	} 

	vec->size = newCnt; 
} 

void CONCAT(NAME, _insert)(NAME *vec, size_t where, TYPE *arr, size_t num){ 
	if (num <= 0){ 
		return; 
	} 

	CONCAT(NAME, _reserve)(vec, vec->size + num); 

	memmove(vec->arr + where + num, vec->arr + where, (vec->size - where) * sizeof(TYPE)); 
	memcpy(vec->arr + where, arr, num * sizeof(TYPE)); 

	vec->size += num; 
} 

void CONCAT(NAME, _erase)(NAME *vec, size_t where, size_t num){ 
	if (vec->size >= num && num > 0){ 
		memmove(vec->arr + where, vec->arr + where + num, (vec->size - where - num) * sizeof(TYPE)); 
		vec->size -= num; 
	} 
} 

#endif
