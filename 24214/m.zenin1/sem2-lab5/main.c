#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>

void thread_cleanup(void *arg){
    printf("Change da world. My final message. Goodbye\n");
}

void* thread_routine(void *arg){

    pthread_cleanup_push(thread_cleanup, NULL);

    while (1){
        printf("I'am happy to live in a such beautiful world!\n");
        usleep(100000);
    }

    pthread_cleanup_pop(1);
}

int main(){
    pthread_t child_tid;
    errno = pthread_create(&child_tid, NULL, thread_routine, NULL);
    if (errno){
        perror("Failed to create thread");
    }

    sleep(2);

    errno = pthread_cancel(child_tid);
    if (errno){
        perror("Failed to cancel thread");
    }
    
    pthread_exit(NULL);
}
