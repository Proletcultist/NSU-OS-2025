#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define PHILO 5
#define DELAY 30000
#define FOOD 50

static pthread_mutex_t fork_mutex[PHILO];
static pthread_mutex_t forks = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t forks_cond = PTHREAD_COND_INITIALIZER;
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

static void get_forks(int phil, int left_fork, int right_fork) {
    int got_left;
    int got_right;
    int err;

    pthread_mutex_lock(&forks);

    for (;;) {
        got_left = 0;
        got_right = 0;

        err = pthread_mutex_trylock(&fork_mutex[left_fork]);
        if (err == 0) {
            got_left = 1;
        } else if (err != EBUSY) {
            fprintf(stderr, "pthread_mutex_trylock failed\n");
            exit(EXIT_FAILURE);
        }

        err = pthread_mutex_trylock(&fork_mutex[right_fork]);
        if (err == 0) {
            got_right = 1;
        } else if (err != EBUSY) {
            if (got_left) {
                pthread_mutex_unlock(&fork_mutex[left_fork]);
            }
            fprintf(stderr, "pthread_mutex_trylock failed\n");
            exit(EXIT_FAILURE);
        }

        if (got_left && got_right) {
            break;
        }

        if (got_left) {
            pthread_mutex_unlock(&fork_mutex[left_fork]);
        }
        if (got_right) {
            pthread_mutex_unlock(&fork_mutex[right_fork]);
        }

        pthread_cond_wait(&forks_cond, &forks);
    }

    printf("Philosopher %d: got left  fork %d\n", phil, left_fork);
    printf("Philosopher %d: got right fork %d\n", phil, right_fork);

    pthread_mutex_unlock(&forks);
}

static void down_forks(int left_fork, int right_fork) {
    pthread_mutex_lock(&forks);

    pthread_mutex_unlock(&fork_mutex[left_fork]);
    pthread_mutex_unlock(&fork_mutex[right_fork]);

    pthread_cond_broadcast(&forks_cond);
    pthread_mutex_unlock(&forks);
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
    int f;

    printf("Philosopher %d sitting down to dinner.\n", id);

    right_fork = id;
    left_fork = id + 1;
    if (left_fork == PHILO) {
        left_fork = 0;
    }

    while ((f = food_on_table()) != 0) {
        if (id == 1) {
            sleep(sleep_seconds);
        }

        printf("Philosopher %d: get dish %d.\n", id, f);

        get_forks(id, left_fork, right_fork);

        printf("Philosopher %d: eating.\n", id);
        sleep_us(DELAY * (FOOD - f + 1));

        down_forks(left_fork, right_fork);
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
        pthread_mutex_init(&fork_mutex[i], NULL);
    }

    for (i = 0; i < PHILO; i++) {
        die_pthread(pthread_create(&phils[i], NULL, philosopher, (void *)(intptr_t)i), "pthread_create");
    }

    for (i = 0; i < PHILO; i++) {
        die_pthread(pthread_join(phils[i], NULL), "pthread_join");
    }

    for (i = 0; i < PHILO; i++) {
        pthread_mutex_destroy(&fork_mutex[i]);
    }
    pthread_mutex_destroy(&foodlock);
    pthread_mutex_destroy(&forks);
    pthread_cond_destroy(&forks_cond);

    return 0;
}
