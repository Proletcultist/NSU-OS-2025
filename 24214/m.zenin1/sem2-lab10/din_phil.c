#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <synch.h>

#define PHILO 5
#define DELAY 30000
#define MAX_THINK_DELAY 50000
#define FOOD 50

pthread_mutex_t forks[PHILO];
pthread_t phils[PHILO];

void* philosopher(void *id);
int food_on_table();
void get_fork(int, int, char *);
void down_forks(int, int);
pthread_mutex_t foodlock;
sema_t table_sem;

void* philosopher(void *num) {
    int id;
    int left_fork, right_fork, f;

    id = (int) num;
    printf("Philosopher %d sitting down to dinner.\n", id);
    right_fork = id;
    left_fork = id + 1;

    /* Wrap around the forks. */
    if (left_fork == PHILO) {
        left_fork = 0;
    }

    while ((f = food_on_table())) {
        sema_wait(&table_sem);

        printf("Philosopher %d: get dish %d.\n", id, f);
        get_fork(id, right_fork, "right");
        get_fork(id, left_fork, "left");

        printf("Philosopher %d: eating.\n", id);
        usleep(DELAY * (FOOD - f + 1));
        down_forks(left_fork, right_fork);

        sema_post(&table_sem);

        printf("Philosopher %d: thinking.\n", id);
        usleep((unsigned int) rand() % MAX_THINK_DELAY);
    }
    printf("Philosopher %d is done eating.\n", id);
    return (NULL);
}

int food_on_table()
{
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

void get_fork (int phil, int fork, char *hand) {
    pthread_mutex_lock(&forks[fork]);
    printf("Philosopher %d: got %s fork %d\n", phil, hand, fork);
}

void down_forks (int f1, int f2) {
    pthread_mutex_unlock(&forks[f1]);
    pthread_mutex_unlock(&forks[f2]);
}

int main() {
    srand((unsigned int) time(NULL));

    pthread_mutex_init(&foodlock, NULL);
    sema_init(&table_sem, PHILO - 1, USYNC_THREAD, NULL);

    for (int i = 0; i < PHILO; i++) {
        pthread_mutex_init(&forks[i], NULL);
    }
    for (int i = 0; i < PHILO; i++) {
        pthread_create(&phils[i], NULL, philosopher, (void *)i);
    }
    for (int i = 0; i < PHILO; i++) {
        pthread_join(phils[i], NULL);
    }

    return 0;
}

