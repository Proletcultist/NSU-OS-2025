#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifndef NUM_STEPS
#define NUM_STEPS 200000000UL
#endif

typedef struct ThreadData {
    int thread_id;
    int threads_count;
} ThreadData;

static void die_pthread(int err, const char *msg) {
    fprintf(stderr, "%s: %s\n", msg, strerror(err));
    exit(EXIT_FAILURE);
}

static void *thread_func(void *arg) {
    ThreadData *data = (ThreadData *)arg;

    double *partial_sum = malloc(sizeof(double));
    if (partial_sum == NULL) {
        pthread_exit(NULL);
    }

    *partial_sum = 0.0;

    for (unsigned long i = (unsigned long)data->thread_id;
         i < NUM_STEPS;
         i += (unsigned long)data->threads_count) {
        *partial_sum += 1.0 / (i * 4.0 + 1.0);
        *partial_sum -= 1.0 / (i * 4.0 + 3.0);
    }

    pthread_exit(partial_sum);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <threads_count>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *endptr = NULL;
    errno = 0;
    long threads_count = strtol(argv[1], &endptr, 10);

    if (errno != 0 || *endptr != '\0' || threads_count <= 0) {
        fprintf(stderr, "Invalid threads count: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    pthread_t *threads = malloc(sizeof(pthread_t) * (size_t)threads_count);
    ThreadData *tdata = malloc(sizeof(ThreadData) * (size_t)threads_count);

    if (threads == NULL || tdata == NULL) {
        free(threads);
        free(tdata);
        perror("malloc");
        return EXIT_FAILURE;
    }

    for (long i = 0; i < threads_count; ++i) {
        tdata[i].thread_id = (int)i;
        tdata[i].threads_count = (int)threads_count;

        int err = pthread_create(&threads[i], NULL, thread_func, &tdata[i]);
        if (err != 0) {
            free(threads);
            free(tdata);
            die_pthread(err, "pthread_create");
        }
    }

    double pi = 0.0;

    for (long i = 0; i < threads_count; ++i) {
        void *retval = NULL;

        int err = pthread_join(threads[i], &retval);
        if (err != 0) {
            free(threads);
            free(tdata);
            die_pthread(err, "pthread_join");
        }

        if (retval == NULL) {
            fprintf(stderr, "Thread %ld failed to allocate memory\n", i);
            free(threads);
            free(tdata);
            return EXIT_FAILURE;
        }

        pi += *(double *)retval;
        free(retval);
    }

    pi *= 4.0;
    printf("pi done - %.15g\n", pi);

    free(threads);
    free(tdata);

    return EXIT_SUCCESS;
}
