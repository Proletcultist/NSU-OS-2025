#define _POSIX_C_SOURCE 200809L

#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#define NUM_LINES 10
#define PARENT_SEM_NAME "/parent_sem_deshin"
#define CHILD_SEM_NAME "/child_sem_deshin"

static sem_t *parent_sem = NULL;
static sem_t *child_sem = NULL;

static void unlink_old_sem(const char *name) {
    if (sem_unlink(name) == -1 && errno != ENOENT) {
        perror("sem_unlink");
        exit(EXIT_FAILURE);
    }
}

static void close_sem(sem_t **sem) {
    if (*sem != NULL && *sem != SEM_FAILED) {
        if (sem_close(*sem) == -1) {
            perror("sem_close");
        }
        *sem = NULL;
    }
}

static void cleanup_sems(int unlink_names) {
    close_sem(&parent_sem);
    close_sem(&child_sem);

    if (unlink_names) {
        if (sem_unlink(PARENT_SEM_NAME) == -1 && errno != ENOENT) {
            perror("sem_unlink");
        }

        if (sem_unlink(CHILD_SEM_NAME) == -1 && errno != ENOENT) {
            perror("sem_unlink");
        }
    }
}

static sem_t *open_sem(const char *name, unsigned int value, int unlink_names) {
    sem_t *sem = sem_open(name, O_CREAT, 0600, value);
    if (sem == SEM_FAILED) {
        perror("sem_open");
        cleanup_sems(unlink_names);
        exit(EXIT_FAILURE);
    }
    return sem;
}

static void check_error(int err, const char *what, int unlink_names) {
    if (err == -1) {
        perror(what);
        cleanup_sems(unlink_names);
        exit(EXIT_FAILURE);
    }
}

int main() {
    unlink_old_sem(PARENT_SEM_NAME);
    unlink_old_sem(CHILD_SEM_NAME);

    pid_t pid = fork();
    check_error(pid, "fork", 1);

    if (pid == 0) {
        parent_sem = open_sem(PARENT_SEM_NAME, 1, 0);
        child_sem = open_sem(CHILD_SEM_NAME, 0, 0);

        for (int i = 1; i <= NUM_LINES; i++) {
            check_error(sem_wait(child_sem), "sem_wait", 0);

            printf("Child: %d\n", i);

            check_error(sem_post(parent_sem), "sem_post", 0);
        }

        cleanup_sems(0);
        exit(EXIT_SUCCESS);
    }

    parent_sem = open_sem(PARENT_SEM_NAME, 1, 1);
    child_sem = open_sem(CHILD_SEM_NAME, 0, 1);

    for (int i = 1; i <= NUM_LINES; i++) {
        check_error(sem_wait(parent_sem), "sem_wait", 1);

        printf("Parent: %d\n", i);

        check_error(sem_post(child_sem), "sem_post", 1);
    }

    check_error(waitpid(pid, NULL, 0), "waitpid", 1);

    cleanup_sems(1);

    return 0;
}
