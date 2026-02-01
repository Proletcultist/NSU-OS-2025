#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <synch.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>

#define INIT_BUFFER_SIZE 256
#define BUFFER_ENLARGMENT_FACTOR 2
#define WAIT_FACTOR 10000

sema_t start_sem, finish_sem;
pthread_mutex_t start_mutex;
pthread_cond_t start_cond;

char* read_input(){
    char *buffer = malloc(INIT_BUFFER_SIZE * sizeof(char));
    size_t buffer_size = INIT_BUFFER_SIZE, bytes_readen = 0;

    if (buffer == NULL){
        perror("Failed to allocate input buffer");
        exit(-1);
    }

    while (1){
        if (bytes_readen + 1 == buffer_size){
            char *tmp = realloc(buffer, buffer_size * BUFFER_ENLARGMENT_FACTOR * sizeof(char));
            if (tmp == NULL){
                free(buffer);
                perror("Failed to enlarge input buffer size");
                exit(-1);
            }

            buffer = tmp;
            buffer_size *= BUFFER_ENLARGMENT_FACTOR;
        }

        ssize_t new_bytes = read(STDIN_FILENO, buffer + bytes_readen, buffer_size - bytes_readen - 1);
        if (new_bytes < 0){
            perror("Failed to read input");
            exit(-1);
        }
        else if (new_bytes == 0){
            break;
        }

        bytes_readen += (size_t) new_bytes;
    }
    buffer[bytes_readen] = '\0';

    return buffer;
}

void* sort_thread_routine(void *arg){
    char *line = arg;
    size_t line_size = strlen(line);

    pthread_mutex_lock(&start_mutex);
    sema_post(&start_sem);
    pthread_cond_wait(&start_cond, &start_mutex);
    pthread_mutex_unlock(&start_mutex);

    usleep(line_size * WAIT_FACTOR);
    printf("%s\n", line);

    sema_post(&finish_sem);

    return NULL;
}

void start_sort_thread(char *input){
    pthread_t tid;
    pthread_attr_t attrs;

    pthread_attr_init(&attrs);
    pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
    errno = pthread_create(&tid, &attrs, sort_thread_routine, input);
    if (errno){
        perror("Failed to start thread");
        exit(-1);
    }
    pthread_attr_destroy(&attrs);
}

size_t start_all_sort_threads(char *input){
    char *last_line_start = input;
    size_t threads_count = 0;

    for (size_t i = 0; input[i] != '\0'; i++){
        if (input[i] == '\n'){
            input[i] = '\0';    
            start_sort_thread(last_line_start);
            threads_count++;

            last_line_start = input + i + 1;
        }
    }
    if (*last_line_start != '\0'){
        start_sort_thread(last_line_start);
        threads_count++;
    }

    return threads_count;
}

int main(){
    // Initiate of all shit
    errno = sema_init(&finish_sem, 0, 0, NULL);
    if (errno){
        perror("Failed to init semaphore");
        return -1;
    }
    errno = sema_init(&finish_sem, 0, 0, NULL);
    if (errno){
        perror("Failed to init semaphore");
        return -1;
    }
    errno = pthread_cond_init(&start_cond, NULL);
    if (errno){
        perror("Failed to init conditional variable");
        return -1;
    }
    errno = pthread_mutex_init(&start_mutex, NULL);
    if (errno){
        perror("Failed to init mutex");
        return -1;
    }

    // Actually do all job
    char *input = read_input();

    size_t threads_count = start_all_sort_threads(input);
    for (size_t i = 0; i < threads_count; i++){
        sema_wait(&start_sem);
    }
    pthread_mutex_lock(&start_mutex);
    pthread_cond_broadcast(&start_cond);
    pthread_mutex_unlock(&start_mutex);
    for (size_t i = 0; i < threads_count; i++){
        sema_wait(&finish_sem);
    }

    // Destroy all shit
    free(input);
    errno = sema_destroy(&start_sem);
    if (errno){
        perror("Failed to desroy semaphore");
        return -1;
    }
    errno = sema_destroy(&finish_sem);
    if (errno){
        perror("Failed to desroy semaphore");
        return -1;
    }
    errno = pthread_cond_destroy(&start_cond);
    if (errno){
        perror("Failed to destroy conditional variable");
        return -1;
    }
    errno = pthread_mutex_destroy(&start_mutex);
    if (errno){
        perror("Failed to destroy mutex");
        return -1;
    }
    return 0;
}

