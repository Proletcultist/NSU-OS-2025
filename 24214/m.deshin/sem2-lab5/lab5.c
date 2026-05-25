#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void die_pthread(int err, const char *what) {
    if (err != 0) {
        fprintf(stderr, "%s: %s\n", what, strerror(err));
        exit(EXIT_FAILURE);
    }
}

static void msg(void *arg) {
    (void)arg;
    printf("Called clean-up handler! Child is cancelling\n");
}

static void *worker(void *arg) {
    (void)arg;

    pthread_cleanup_push(msg, NULL);

    while (1) {
        sleep(1);
        printf("Child thread worked...\n");
    }

    pthread_cleanup_pop(0);
    return NULL;
}

int main() {
    pthread_t t;

    int err = pthread_create(&t, NULL, worker, NULL);
    die_pthread(err, "pthread_create");

    sleep(2);

    err = pthread_cancel(t);
    die_pthread(err, "pthread_cancel");

    void *status;
    err = pthread_join(t, &status);
    die_pthread(err, "pthread_join");

    if (status == PTHREAD_CANCELED) printf("Child thread was cancelled\n");
    else printf("Child thread exited normally\n");

    return EXIT_SUCCESS;
}
