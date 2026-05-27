#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define PHILO 5
#define DELAY 30000
#define FOOD 50

static pthread_mutex_t forks[PHILO];
static pthread_t phils[PHILO];
static pthread_mutex_t foodlock;

static int sleep_seconds = 0;

static int food_on_table(void) {
    static int food = FOOD;
    int myfood;

    pthread_mutex_lock(&foodlock);
    if (food > 0) {
        food--;
    }
    myfood = food;
    pthread_mutex_unlock(&foodlock);

    return myfood;
}

static void get_fork(int phil, int fork, const char *hand) {
    pthread_mutex_lock(&forks[fork]);
    printf("Philosopher %d: got %s fork %d\n", phil, hand, fork);
}

static void down_forks(int f1, int f2) {
    pthread_mutex_unlock(&forks[f2]);
    pthread_mutex_unlock(&forks[f1]);
}

static void sleep_us(long usec) {
    struct timespec ts;

    ts.tv_sec = usec / 1000000L;
    ts.tv_nsec = (usec % 1000000L) * 1000L;

    nanosleep(&ts, NULL);
}

static void *philosopher(void *num) {
    int id = (int)(intptr_t)num;
    int left_fork;
    int right_fork;
    int first_fork;
    int second_fork;
    int f;

    printf("Philosopher %d sitting down to dinner.\n", id);

    right_fork = id;
    left_fork = id + 1;
    if (left_fork == PHILO) {
        left_fork = 0;
    }

    if (left_fork < right_fork) {
        first_fork = left_fork;
        second_fork = right_fork;
    } else {
        first_fork = right_fork;
        second_fork = left_fork;
    }

    while ((f = food_on_table()) != 0) {
        if (id == 1) {
            sleep(sleep_seconds);
        }

        printf("Philosopher %d: get dish %d.\n", id, f);

        get_fork(id, first_fork, first_fork == left_fork ? "left " : "right");
        get_fork(id, second_fork, second_fork == left_fork ? "left " : "right");

        printf("Philosopher %d: eating.\n", id);
        sleep_us(DELAY * (FOOD - f + 1));

        down_forks(first_fork, second_fork);
    }

    printf("Philosopher %d is done eating.\n", id);
    return NULL;
}

static void die_pthread(int err, const char *what) {
    if (err != 0) {
        fprintf(stderr, "%s: %s\n", what, strerror(err));
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char **argv) {
    int i;

    if (argc == 2) {
        sleep_seconds = atoi(argv[1]);
    }

    pthread_mutex_init(&foodlock, NULL);

    for (i = 0; i < PHILO; i++) {
        pthread_mutex_init(&forks[i], NULL);
    }

    for (i = 0; i < PHILO; i++) {
        die_pthread(pthread_create(&phils[i], NULL, philosopher, (void *)(intptr_t)i), "pthread_create");
    }

    for (i = 0; i < PHILO; i++) {
        die_pthread(pthread_join(phils[i], NULL), "pthread_join");
    }

    for (i = 0; i < PHILO; i++) {
        pthread_mutex_destroy(&forks[i]);
    }
    pthread_mutex_destroy(&foodlock);

    return 0;
}
