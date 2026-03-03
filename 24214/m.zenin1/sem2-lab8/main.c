#include <stdio.h>
#include <pthread.h>
#include <inttypes.h>
#include <stdlib.h>
#include <errno.h>

#define ITERATION_PER_THREAD 256

struct task {
    uint32_t x;
    uint32_t seed;
    uint32_t step;
    
    uint32_t salt;
    uint32_t n;
    uint32_t hash;
};

uint32_t toy_hash(uint32_t x, uint32_t salt) {
    uint32_t h = x ^ salt;
    h *= 0x7feb352d;
    h ^= h >> 15;
    h *= 0x846ca68b;
    h ^= h >> 16;
    return h;
}

int str_to_u32(char *str, uint32_t *out) {
    if (*str == '\0') {
        errno = EINVAL;
        return -1;
    }

    errno = 0;
    char *end;
    unsigned long parsed = strtoul(str, &end, 10);
    if (errno != 0){
        return -1;
    }
    else if (*end != '\0') {
        errno = EINVAL;
        return -1;
    }
    else if (parsed > UINT32_MAX) {
        errno = ERANGE;
        return -1;
    }

    *out = (uint32_t) parsed;
    return 0;
}

void print_u32_bin(uint32_t n) {
    char buff[33];
    buff[32] = '\0';

    size_t write_index = 31;
    if (n == 0) {
        buff[write_index--] = '0';
    }
    while (n) {
        if (n % 2) {
            buff[write_index--] = '1';
        }
        else {
            buff[write_index--] = '0';
        }
        n >>= 1;
    }

    printf("0b%s", buff + (write_index + 1));
}

uint32_t count_trailing_zeroes(uint32_t n) {
    if (n == 0) {
        return 0;
    }

    uint32_t out = 0;
    while (1) {
        if (n % 2 == 0) {
            out++;
        }
        else {
            break;
        }
        n >>= 1;
    }

    return out;
}

void* worker_thread_routine(void *arg) {
    struct task *real_arg = arg;

    real_arg->salt = real_arg->seed;
    real_arg->hash = toy_hash(real_arg->x, real_arg->salt);
    real_arg->n = count_trailing_zeroes(real_arg->hash);

    uint32_t salt = real_arg->seed + real_arg->step;
    for (size_t i = 1; i < ITERATION_PER_THREAD; i++){
        if (count_trailing_zeroes(toy_hash(real_arg->x, salt)) > real_arg->n){
            real_arg->salt = salt;
            real_arg->hash = toy_hash(real_arg->x, real_arg->salt);
            real_arg->n = count_trailing_zeroes(real_arg->hash);
        }

        salt += real_arg->step;
    }

    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Missing threads amount argument\n");
        return -1;
    }
    else if (argc < 3) {
        fprintf(stderr, "Missing x argument\n");
        return -1;
    }


    uint32_t t_amount, x;
    if (str_to_u32(argv[1], &t_amount)){
        perror("Failed to parse threads amount argument");
        return -1;
    }
    if (str_to_u32(argv[2], &x)){
        perror("Failed to parse x argument");
        return -1;
    }
    if (t_amount == 0){
        fprintf(stderr, "Threads amount argument cannot be zero\n");
        return -1;
    }

    pthread_t tids[t_amount];
    struct task args[t_amount];

    for (uint32_t i = 0; i < t_amount; i++){
        args[i] = (struct task) {.seed = i, .x = x, .step = t_amount};

        errno = pthread_create(&tids[i], NULL, worker_thread_routine, &args[i]);
        if (errno) {
            perror("Failed to create thread");
            return -1;
        }
    }

    void *ret;
    errno = pthread_join(tids[0], &ret);
    if (errno) {
        perror("Failed to join thread");
        return -1;
    }
    uint32_t max_n = args[0].n, hash = args[0].hash, salt = args[0].salt;

    for (uint32_t i = 1; i < t_amount; i++) {
        void *ret;
        errno = pthread_join(tids[i], &ret);
        if (errno) {
            perror("Failed to join thread");
            return -1;
        }
        if (args[i].n > max_n){
            max_n = args[i].n;
            hash = args[i].hash;
            salt = args[i].salt;
        }
    }

    printf("Max n found: %" PRIu32 " salt: %" PRIu32 " hash: ", max_n, salt);
    print_u32_bin(hash);
    printf("\n");

    return 0;
}
