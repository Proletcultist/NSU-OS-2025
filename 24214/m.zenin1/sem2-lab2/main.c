#include <stdio.h>
#include <pthread.h>
#include <errno.h>

void* thread_routine(void *arg) {
    int n = (int) arg;

    for (int i = 0; i < 10; i++) {
        printf("%d: Line %d\n", n, i);
    }

    return NULL;
}

int main() {
    pthread_t t;
    errno = pthread_create(&t, NULL, thread_routine, (void*) 2);
    if (errno) {
        perror("Failed to create thread");
        return -1;
    }

    errno = pthread_join(t, NULL);
    if (errno) {
        perror("Failed to join thread");
        return -1;
    }

    thread_routine((void*) 1);

    return 0;
}

