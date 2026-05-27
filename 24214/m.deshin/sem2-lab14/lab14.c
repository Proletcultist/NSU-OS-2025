#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_LINES 10

static sem_t parent_sem;
static sem_t child_sem;

static void die_pthread(int err, const char *what) {
    if (err != 0) {
        fprintf(stderr, "%s: %s\n", what, strerror(err));
        exit(EXIT_FAILURE);
    }
}

static void die_if_error(int err, const char *what) {
    if (err != 0) {
        perror(what);
        exit(EXIT_FAILURE);
    }
}

static void *child(void *arg) {
    (void)arg;
    for (int i = 1; i <= NUM_LINES; i++) {
        die_if_error(sem_wait(&child_sem), "sem_wait");

        printf("Child: %d\n", i);

        die_if_error(sem_post(&parent_sem), "sem_post");
    }
    return NULL;
}

int main() {
    pthread_t thread;

    die_if_error(sem_init(&parent_sem, 0, 1), "sem_init");
    die_if_error(sem_init(&child_sem, 0, 0), "sem_init");

    die_pthread(pthread_create(&thread, NULL, child, NULL), "pthread_create");
    
    for (int i = 1; i <= NUM_LINES; i++) {
        die_if_error(sem_wait(&parent_sem), "sem_wait");

        printf("Parent: %d\n", i);

        die_if_error(sem_post(&child_sem), "sem_post");
    }

    die_pthread(pthread_join(thread, NULL), "pthread_join");

    die_if_error(sem_destroy(&parent_sem), "sem_destroy");
    die_if_error(sem_destroy(&child_sem), "sem_destroy");

    return 0;
}
