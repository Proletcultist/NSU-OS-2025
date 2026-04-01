#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TURN_PARENT = 0,
    TURN_CHILD = 1
} Turn;

typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t cv;
    Turn turn;
    int n;
} Shared;

static void die_pthread(int err, const char *what) {
    if (err != 0) {
        fprintf(stderr, "%s: %s\n", what, strerror(err));
        exit(EXIT_FAILURE);
    }
}

static void *worker(void *arg) {
    Shared *s = (Shared *)arg;

    for (int i = 1; i <= s->n; i++) {
        int err = pthread_mutex_lock(&s->mtx);
        die_pthread(err, "pthread_mutex_lock(child)");

        while (s->turn != TURN_CHILD) {
            err = pthread_cond_wait(&s->cv, &s->mtx);
            die_pthread(err, "pthread_cond_wait(child)");
        }

        printf("Child: line %d\n", i);
        fflush(stdout);

        s->turn = TURN_PARENT;

        err = pthread_cond_signal(&s->cv);
        die_pthread(err, "pthread_cond_signal(child)");

        err = pthread_mutex_unlock(&s->mtx);
        die_pthread(err, "pthread_mutex_unlock(child)");
    }

    return NULL;
}

int main(void) {
    pthread_t t;

    Shared s;
    s.turn = TURN_PARENT;
    s.n = 10;

    pthread_mutexattr_t a;
    int err = pthread_mutexattr_init(&a);
    die_pthread(err, "pthread_mutexattr_init");

    err = pthread_mutexattr_settype(&a, PTHREAD_MUTEX_ERRORCHECK);
    die_pthread(err, "pthread_mutexattr_settype(ERRORCHECK)");

    err = pthread_mutex_init(&s.mtx, &a);
    die_pthread(err, "pthread_mutex_init");

    err = pthread_mutexattr_destroy(&a);
    die_pthread(err, "pthread_mutexattr_destroy");

    err = pthread_cond_init(&s.cv, NULL);
    die_pthread(err, "pthread_cond_init");

    err = pthread_create(&t, NULL, worker, &s);
    die_pthread(err, "pthread_create");

    for (int i = 1; i <= s.n; i++) {
        err = pthread_mutex_lock(&s.mtx);
        die_pthread(err, "pthread_mutex_lock(parent)");

        while (s.turn != TURN_PARENT) {
            err = pthread_cond_wait(&s.cv, &s.mtx);
            die_pthread(err, "pthread_cond_wait(parent)");
        }

        printf("Parent: line %d\n", i);
        fflush(stdout);

        s.turn = TURN_CHILD;

        err = pthread_cond_signal(&s.cv);
        die_pthread(err, "pthread_cond_signal(parent)");

        err = pthread_mutex_unlock(&s.mtx);
        die_pthread(err, "pthread_mutex_unlock(parent)");
    }

    err = pthread_join(t, NULL);
    die_pthread(err, "pthread_join");

    err = pthread_cond_destroy(&s.cv);
    die_pthread(err, "pthread_cond_destroy");

    err = pthread_mutex_destroy(&s.mtx);
    die_pthread(err, "pthread_mutex_destroy");

    return 0;
}
