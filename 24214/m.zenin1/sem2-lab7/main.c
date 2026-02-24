#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <synch.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <alloca.h>
#include <string.h>
#include <stdbool.h>
#include "thread_vec.h"

#define CREATION_MODS S_IRWXU
#define COPY_BUFFER_SIZE 8192

typedef struct copy_argument {
    char *src;
    char *dest;
} copy_argument;

void* copy_directory(void*);
void* copy_regfile(void*);
int spawn_copy_file_thread(copy_argument*, pthread_t*);

sema_t open_sem;

void close_dir(void *dir){
    if (closedir(dir)){
        perror("Failed to close dir");
    }
}
void close_file(void *fd){
    if (close(*((int*) fd))){
        perror("Failed to close file");
    }
}
void post_sem(void *sem){
    errno = sema_post(((sema_t*) sem));
    if (errno){
        perror("Failed to post semaphore");
    }
}

void* copy_directory(void *arg_any){
    copy_argument *arg = (copy_argument*) arg_any;

    errno = sema_wait(&open_sem);
    if (errno){
        fprintf(stderr, "Failed to wait on semaphore while copying %s: %s\n", arg->src, strerror(errno));
        return NULL;
    }
    pthread_cleanup_push(post_sem, &open_sem);

    struct stat dir_stat;
    if (stat(arg->dest, &dir_stat) != 0 || (dir_stat.st_mode & S_IFMT) != S_IFDIR){
#ifdef DEBUG
        printf("Creating directory %s\n", arg->dest);
#else
        if (mkdir(arg->dest, CREATION_MODS)){
            fprintf(stderr, "Failed to create directory %s: %s\n", arg->dest, strerror(errno));
            return NULL;
        }
#endif
    }

    DIR *dir = opendir(arg->src);
    if (dir == NULL){
        fprintf(stderr, "Failed to open directory %s: %s\n", arg->src, strerror(errno));
        return NULL;
    }
    pthread_cleanup_push(close_dir, dir);

    long max_path_len = pathconf(arg->src, _PC_NAME_MAX);
    if (max_path_len == -1){
        fprintf(stderr, "Failed to get max path len while copying %s: %s\n", arg->src, strerror(errno));
        return NULL;
    }

    struct dirent *entry = alloca(sizeof(struct dirent) + max_path_len + 1);
    struct dirent *result = NULL;

    thread_vec tids = thread_vec_construct();
    pthread_cleanup_push((void (*)(void*)) thread_vec_destruct, &tids);

    errno = readdir_r(dir, entry, &result);
    if (errno){
        fprintf(stderr, "Failed to read entry from %s: %s\n", arg->src, strerror(errno));
        return NULL;
    }
    while (result != NULL){
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0){
            char *next_src = alloca(strlen(arg->src) + strlen(entry->d_name) + 2);
            char *next_dst = alloca(strlen(arg->dest) + strlen(entry->d_name) + 2);
            strcat(strcat(stpcpy(next_src, arg->src), "/"), entry->d_name);
            strcat(strcat(stpcpy(next_dst, arg->dest), "/"), entry->d_name);
            copy_argument *arg = alloca(sizeof(copy_argument));
            arg->src = next_src;
            arg->dest = next_dst;

            pthread_t tid;
            if (spawn_copy_file_thread(arg, &tid) == 0){
                if (thread_vec_push(&tids, tid)){
                    return NULL;
                }
            }
        }

        errno = readdir_r(dir, entry, &result);
        if (errno){
            fprintf(stderr, "Failed to read entry from %s: %s\n", arg->src, strerror(errno));
            return NULL;
        }
    }

    for (size_t i = 0; i < tids.size; i++){
        void *ret;
        errno = pthread_join(tids.arr[i], &ret);
        if (errno){
            perror("Failed to join thread");
            exit(-1);
        }
    }

    pthread_cleanup_pop(1);
    pthread_cleanup_pop(1);
    pthread_cleanup_pop(1);

    return NULL;
}

void* copy_regfile(void *arg_any){
    copy_argument *arg = (copy_argument*) arg_any;

    errno = sema_wait(&open_sem);
    if (errno){
        fprintf(stderr, "Failed to wait on semaphore while copying %s: %s\n", arg->src, strerror(errno));
        return NULL;
    }
    pthread_cleanup_push(post_sem, &open_sem);
    errno = sema_wait(&open_sem);
    if (errno){
        fprintf(stderr, "Failed to wait on semaphore while copying %s: %s\n", arg->src, strerror(errno));
        return NULL;
    }
    pthread_cleanup_push(post_sem, &open_sem);

#ifdef DEBUG
    printf("Copying %s to %s\n", arg->src, arg->dest);
#else
    int src_fd = open(arg->src, O_RDONLY);
    if (src_fd < 0){
        fprintf(stderr, "Failed to open file %s: %s\n", arg->src, strerror(errno));
        return NULL;
    }
    pthread_cleanup_push(close_file, &src_fd);

    int dest_fd = open(arg->dest, O_CREAT | O_TRUNC | O_WRONLY, CREATION_MODS);
    if (dest_fd < 0){
        fprintf(stderr, "Failed to create file %s: %s\n", arg->dest, strerror(errno));
        return NULL;
    }
    pthread_cleanup_push(close_file, &dest_fd);


    char buffer[COPY_BUFFER_SIZE];
    ssize_t readen_bytes = 0;
    do{
        readen_bytes = read(src_fd, buffer, sizeof(buffer));
        if (readen_bytes == -1){
            fprintf(stderr, "Failed to read from %s: %s\n", arg->src, strerror(errno));
            return NULL;
        }
        ssize_t written_bytes = write(dest_fd, buffer, readen_bytes);
        if (written_bytes == -1 || written_bytes != readen_bytes){
            fprintf(stderr, "Failed to write to %s: %s\n", arg->dest, strerror(errno));
            return NULL;
        }
    } while(readen_bytes != 0);

    pthread_cleanup_pop(1);
    pthread_cleanup_pop(1);
#endif
    pthread_cleanup_pop(1);
    pthread_cleanup_pop(1);

    return NULL;
}

int spawn_copy_file_thread(copy_argument *arg, pthread_t *tid){
    struct stat src_stat;
    if (stat(arg->src, &src_stat)){
        fprintf(stderr, "Failed to stat file %s: %s\n", arg->src, strerror(errno));
        return -1;
    }
    
    if ((src_stat.st_mode & S_IFMT) == S_IFREG){
        errno = pthread_create(tid, NULL, copy_regfile, arg);
    }
    else if ((src_stat.st_mode & S_IFMT) == S_IFDIR){
        errno = pthread_create(tid, NULL, copy_directory, arg);
    }
    else{
        fprintf(stderr, "[\033[33mWarning\033[0m] File %s not isn't regular file or directory\n", arg->src);
        return -1;
    }

    if (errno){
        fprintf(stderr, "Failed to spawn thread for copying %s: %s\n", arg->src, strerror(errno));
        return -1;
    }

    return 0;
}

int main(int argc, char **argv){
    if (argc < 2){
        fprintf(stderr, "Missing source argument\n");
        return -1;
    }
    else if (argc < 3){
        fprintf(stderr, "Missing destination argument\n");
        return -1;
    }

    long max_open_desc = sysconf(_SC_OPEN_MAX);
    if (max_open_desc == -1){
        perror("Failed to get max opened descriptors amount");
        return -1;
    }
    errno = sema_init(&open_sem, max_open_desc <= (long) UINT_MAX ? (unsigned) max_open_desc : UINT_MAX, 0, NULL);
    if (errno){
        perror("Failed to init semaphore");
        return -1;
    }

    pthread_t tid;
    copy_argument arg = {.src = argv[1], .dest = argv[2]};
    if (spawn_copy_file_thread(&arg, &tid) == 0){
        void *ret;
        errno = pthread_join(tid, &ret);
        if (errno){
            perror("Failed to join thread");
            return -1;
        }
    }

    if (sema_destroy(&open_sem)){
        perror("Failed to destroy semaphore");
        return -1;
    }

    return 0;
}
