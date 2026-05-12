/*
 * File:   din_phil.c
 * Author: nd159473 (Nickolay Dalmatov, Sun Microsystems)
 * adapted from http://developers.sun.com/sunstudio/downloads/ssx/tha/tha_using_deadlock.html
 *
 * Created on January 1, 1970, 9:53 AM
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>

#define PHILO 5
#define DELAY 30000
#define FOOD 50

pthread_mutex_t forks[PHILO];
pthread_t phils[PHILO];
void *philosopher(void *id);
int food_on_table();
void take_forks(int, int, int);
void down_forks(int, int);
pthread_mutex_t foodlock;
pthread_mutex_t forks_lock;
pthread_cond_t forks_cv;

int sleep_seconds = 0;

int main(int argn,
		 char **argv)
{
	int i;

	if (argn == 2)
		sleep_seconds = atoi(argv[1]);

	pthread_mutex_init(&foodlock, NULL);
	pthread_mutex_init(&forks_lock, NULL);
	pthread_cond_init(&forks_cv, NULL);
	for (i = 0; i < PHILO; i++)
		pthread_mutex_init(&forks[i], NULL);
	for (i = 0; i < PHILO; i++)
		pthread_create(&phils[i], NULL, philosopher, (void *)(intptr_t)i);
	for (i = 0; i < PHILO; i++)
		pthread_join(phils[i], NULL);
	return 0;
}

void *
philosopher(void *num)
{
	int id;
	int left_fork, right_fork, f;

	id = (int)(intptr_t)num;
	printf("Philosopher %d sitting down to dinner.\n", id);
	right_fork = id;
	left_fork = id + 1;

	/* Wrap around the forks. */
	if (left_fork == PHILO)
		left_fork = 0;

	while ((f = food_on_table()) > 0)
	{

		/* Thanks to philosophers #1 who would like to
		 * take a nap before picking up the forks, the other
		 * philosophers may be able to eat their dishes and
		 * not deadlock.
		 */
		if (id == 1)
			sleep(sleep_seconds);

		printf("Philosopher %d: get dish %d.\n", id, f);
		take_forks(id, left_fork, right_fork);

		printf("Philosopher %d: eating.\n", id);
		usleep(DELAY * (FOOD - f + 1));
		down_forks(left_fork, right_fork);
	}
	printf("Philosopher %d is done eating.\n", id);
	return (NULL);
}

int food_on_table()
{
	static int food = FOOD;
	int myfood;

	pthread_mutex_lock(&foodlock);
	if (food > 0)
	{
		food--;
	}
	myfood = food;
	pthread_mutex_unlock(&foodlock);
	return myfood;
}

void take_forks(int phil,
				int left,
				int right)
{
	int left_taken = 0;
	int right_taken = 0;

	pthread_mutex_lock(&forks_lock);
	while (1)
	{
		if (!left_taken && pthread_mutex_trylock(&forks[left]) == 0)
			left_taken = 1;

		if (!right_taken && pthread_mutex_trylock(&forks[right]) == 0)
			right_taken = 1;

		if (left_taken && right_taken)
			break;

		if (left_taken)
		{
			pthread_mutex_unlock(&forks[left]);
			left_taken = 0;
		}
		if (right_taken)
		{
			pthread_mutex_unlock(&forks[right]);
			right_taken = 0;
		}

		pthread_cond_wait(&forks_cv, &forks_lock);
	}

	printf("Philosopher %d: got left fork %d\n", phil, left);
	printf("Philosopher %d: got right fork %d\n", phil, right);
	pthread_mutex_unlock(&forks_lock);
}

void down_forks(int f1,
				int f2)
{
	pthread_mutex_lock(&forks_lock);
	pthread_mutex_unlock(&forks[f1]);
	pthread_mutex_unlock(&forks[f2]);
	pthread_cond_broadcast(&forks_cv);
	pthread_mutex_unlock(&forks_lock);
}
