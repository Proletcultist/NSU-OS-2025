#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>

#define PHILO 5
#define DELAY 30000
#define MAX_THINK_DELAY 50000
#define FOOD 50

pthread_mutex_t forks[PHILO];
pthread_t phils[PHILO];

void* philosopher(void *id);
int food_on_table();
void get_fork(int, int, char *);
int try_get_fork (int, int, char *);
void put_fork(int);
void down_forks(int, int);
pthread_mutex_t foodlock, forkslock;
pthread_cond_t forkscond;

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

        // Lock mutex for getting forks
        pthread_mutex_lock(&forkslock);

        printf("Philosopher %d: get dish %d.\n", id, f);
        while (1) {
            get_fork(id, right_fork, "right");
            if (try_get_fork(id, left_fork, "left")){
                break;
            }
            else{
                put_fork(right_fork);
                pthread_cond_wait(&forkscond, &forkslock);
            }
        }

        pthread_mutex_unlock(&forkslock);

        printf("Philosopher %d: eating.\n", id);
        usleep(DELAY * (FOOD - f + 1));
        put_fork(left_fork);
        put_fork(right_fork);

        // Lock mutext for notifying all who wait for forks
        pthread_mutex_lock(&forkslock);
        pthread_cond_broadcast(&forkscond);
        pthread_mutex_unlock(&forkslock);

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

int try_get_fork (int phil, int fork, char *hand) {
    errno = pthread_mutex_trylock(&forks[fork]);
    if (errno == EBUSY){
        return 0;
    }

    printf("Philosopher %d: got %s fork %d\n", phil, hand, fork);
    return 1;
}

void put_fork(int f) {
    pthread_mutex_unlock(&forks[f]);
}

int main() {
    srand((unsigned int) time(NULL));

    pthread_mutex_init(&foodlock, NULL);
    pthread_mutex_init(&forkslock, NULL);
    pthread_cond_init(&forkscond, NULL);

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

