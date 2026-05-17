#ifndef _CONCAT
	#define _CONCAT(a,b) a##b
#endif

#ifndef CONCAT
	#define CONCAT(a,b) _CONCAT(a,b)
#endif

#ifndef HASHMAP_NODE_TYPE_42345678983221392132132456956
typedef enum HashMap_node_type{
	EMPTY_NODE = 0,
	VALUE_NODE,
	TOMBSTONE_NODE
}HashMap_node_type;
#define HASHMAP_NODE_TYPE_42345678983221392132132456956
#endif


#ifdef HASHMAP_DECL
#include <stddef.h>

#ifndef HASHMAP_INITIALIZER
    #define HASHMAP_INITIALIZER {NULL, 0, 0}
#endif

typedef struct CONCAT(NAME, _node){
	KEY_TYPE key;
	VALUE_TYPE value;
	HashMap_node_type type;
	size_t probe_start;
}CONCAT(NAME, _node);

typedef struct NAME{
	CONCAT(NAME, _node) *arr;
	size_t size;
	size_t elems; 
}NAME;


NAME CONCAT(NAME, _construct)();
void CONCAT(NAME, _destruct)(NAME self);
VALUE_TYPE* CONCAT(NAME, _get)(NAME *self, KEY_TYPE key);
int CONCAT(NAME, _set)(NAME *self, KEY_TYPE key, VALUE_TYPE value);
void CONCAT(NAME, _remove)(NAME *self, KEY_TYPE key);
#endif

#ifdef HASHMAP_IMPL
#include <stddef.h>
#include <stdlib.h>
#include <memory.h>
#include <inttypes.h>

#define INITIAL_HM_SIZE 113 // Prime number
#define HM_EXTENSION 2
#define MAX_HM_LOAD 90 // % 

size_t CONCAT(KEY_TYPE, _hash)(KEY_TYPE key);
int CONCAT(KEY_TYPE, _cmp)(KEY_TYPE l, KEY_TYPE r);

#ifndef HASHMAP_UTILS_21321456456456456
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
#define HASHMAP_UTILS_21321456456456456
#endif

static void CONCAT(NAME, _swap_value)(VALUE_TYPE *l, VALUE_TYPE *r){
	VALUE_TYPE buffer = *l;
	*l = *r;
	*r = buffer;
}

static void CONCAT(NAME, _swap_key)(KEY_TYPE *l, KEY_TYPE *r){
	KEY_TYPE buffer = *l;
	*l = *r;
	*r = buffer;
}


int CONCAT(NAME, _extend)(NAME *self){
	CONCAT(NAME, _node) *old_arr = self->arr;
	size_t old_size = self->size;
	self->size = nextPrime(self->size * HM_EXTENSION);
    void *tmp = calloc(self->size, sizeof(CONCAT(NAME, _node)));
    if (tmp == NULL) {
        return -1;
    }
	self->arr = tmp;

	size_t elems = self->elems;
	self->elems = 0;

	for (size_t i = 0; i < old_size && elems;i++){
		// Not empty nor tombstone
		if (old_arr[i].type == VALUE_NODE){
			elems--;
			CONCAT(NAME, _set)(self, old_arr[i].key, old_arr[i].value);
		}
	}

	free(old_arr);

    return 0;
}

int CONCAT(NAME, _check_load)(NAME *self){
	if (self->size == 0){
        self->arr = calloc(INITIAL_HM_SIZE, sizeof(CONCAT(NAME, _node)));
        if (self->arr == NULL) {
            return -1;
        }

		self->size = INITIAL_HM_SIZE;
	}
	else if (self->elems >= self->size / 100 * MAX_HM_LOAD){
		return CONCAT(NAME, _extend)(self);
	}

    return 0;
}

NAME CONCAT(NAME, _construct)(){
	return (NAME){NULL, 0, 0};
}

void CONCAT(NAME, _destruct)(NAME self){
	if (self.size != 0){	
		free(self.arr);
		self.size = 0;
		self.elems = 0;
	}
}

VALUE_TYPE* CONCAT(NAME, _get)(NAME *self, KEY_TYPE key){
	if (self->size == 0 || self->elems == 0){
		return NULL;
	}

    size_t hash = CONCAT(KEY_TYPE, _hash)(key) % self->size;
	size_t probe_dist = 0;
	
	for (size_t pos = hash;;pos = (pos + 1) % self->size){
		// Empty place
		if (self->arr[pos].type == EMPTY_NODE){
			return NULL;
		}
		// Current elem has prob sequence shorter than new elem
		else if (calcProbDist(pos, self->arr[pos].probe_start, self->size) < probe_dist){
			return NULL;	
		}
		// Element with such key already in table
		else if (self->arr[pos].type != TOMBSTONE_NODE && self->arr[pos].probe_start == hash && CONCAT(KEY_TYPE, _cmp)(self->arr[pos].key, key) == 0){
			return &self->arr[pos].value;
		}
		probe_dist++;
	}
}

int CONCAT(NAME, _set)(NAME *self, KEY_TYPE key, VALUE_TYPE value){
	if (CONCAT(NAME, _check_load)(self)) {
        return -1;
    }
	self->elems++;

	size_t hash = CONCAT(KEY_TYPE, _hash)(key) % self->size;
	size_t probe_dist = 0;
	for (size_t pos = hash;;pos = (pos + 1) % self->size){

		// Empty place
		if (self->arr[pos].type == EMPTY_NODE){
			self->arr[pos] = (CONCAT(NAME, _node)){key, value, VALUE_NODE, hash};
			return 0;
		}

		// Current elem has prob sequence shorter than new elem
		if (calcProbDist(pos, self->arr[pos].probe_start, self->size) < probe_dist){
			// Is tombstone
			if (self->arr[pos].type == TOMBSTONE_NODE){
				self->arr[pos] = (CONCAT(NAME, _node)){key, value, VALUE_NODE, hash};
				return 0;
			}
			
			// Swaping current and new and continue to try find new place for current
			probe_dist = calcProbDist(pos, self->arr[pos].probe_start, self->size);
			CONCAT(NAME, _swap_key)(&self->arr[pos].key, &key);
			CONCAT(NAME, _swap_value)(&self->arr[pos].value, &value);
			swapSize_t(&self->arr[pos].probe_start, &hash);

			probe_dist++;
			continue;
		}
		
		// Element with such key already in table
		if (self->arr[pos].type != TOMBSTONE_NODE && self->arr[pos].probe_start == hash && CONCAT(KEY_TYPE, _cmp)(self->arr[pos].key, key) == 0){
			self->arr[pos].value = value;
			self->elems--;
			return 0; 
		}

		probe_dist++;
	}

    return 0;
}


void CONCAT(NAME, _remove)(NAME *self, KEY_TYPE key){
	if (self->size == 0 || self->elems == 0){
		return;
	}

	size_t hash = CONCAT(KEY_TYPE, _hash)(key) % self->size;
	size_t probe_dist = 0;
	
	for (size_t pos = hash;;pos = (pos + 1) % self->size){
		// Empty place
		if (self->arr[pos].type == EMPTY_NODE){
			return;
		}
		// Current elem has prob sequence shorter than new elem
		else if (calcProbDist(pos, self->arr[pos].probe_start, self->size) < probe_dist){
			return;	
		}
		// Element with such key already in table
		else if (self->arr[pos].type != TOMBSTONE_NODE && self->arr[pos].probe_start == hash && CONCAT(KEY_TYPE, _cmp)(self->arr[pos].key, key) == 0){
			// Make tombstone
			self->arr[pos].type = TOMBSTONE_NODE;
			self->elems--;
			return;
		}
		probe_dist++;
	}
}

#endif
