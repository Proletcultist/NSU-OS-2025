#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

static void child_cleanup(void *arg)
{
	printf("Child thread: exiting after cancellation\n");
}

void *child_thread(void *arg)
{
	int counter = 0;
	pthread_cleanup_push(child_cleanup, NULL);
	while (1)
	{
		printf("Child thread: message %d\n", counter++);
		fflush(stdout);
		usleep(100000); // 100 ms.
	}
	pthread_cleanup_pop(0);

	return NULL;
}

int main()
{
	pthread_t thread;
	int result;

	result = pthread_create(&thread, NULL, child_thread, NULL);
	if (result != 0)
	{
		perror("Thread creation failed");
		exit(1);
	}

	printf("Parent thread: child thread created\n");

	sleep(2);

	printf("Parent thread: canceling child thread\n");
	result = pthread_cancel(thread);
	if (result != 0)
	{
		perror("Thread cancel failed");
		exit(1);
	}

	result = pthread_join(thread, NULL);
	if (result != 0)
	{
		perror("pthread_join failed");
		exit(1);
	}

	printf("Parent thread: child thread finished\n");

	return 0;
}
