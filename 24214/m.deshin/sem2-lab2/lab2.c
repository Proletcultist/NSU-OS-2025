#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die_pthread(int err, const char *what) {
    if (err != 0) {
        fprintf(stderr, "%s: %s\n", what, strerror(err));
        exit(EXIT_FAILURE);
    }
}

static void *worker(void *arg) {
    (void)arg;
    for (int i = 1; i <= 10; i++) {
        printf("Child: line %d\n", i);
    }
    return NULL;
}

int main(void) {
    pthread_t t;

    int err = pthread_create(&t, NULL, worker, NULL);
    die_pthread(err, "pthread_create");

    err = pthread_join(t, NULL);
    die_pthread(err, "pthread_join");

    for (int i = 1; i <= 10; i++) {
        printf("Parent: line %d\n", i);
    }

    return 0;
}
