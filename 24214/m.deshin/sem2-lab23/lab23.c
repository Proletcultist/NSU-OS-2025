#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define MAX_LEN 4096
#define MAX_LINES 100
#define K 100000000

typedef struct Node {
    char *str;
    struct Node *next;
} Node;

static Node *head = NULL;
static Node *tail = NULL;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void die_pthread(int err, const char *what) {
    if (err != 0) {
        fprintf(stderr, "%s: %s", what, strerror(err));
        exit(EXIT_FAILURE);
    }
}

static void die_errno(const char *what) {
    perror(what);
    exit(EXIT_FAILURE);
}

static void *worker(void *arg) {
    char *str = (char *)arg;

    long ns = (long)strlen(str) * K;
    struct timespec ts;
    ts.tv_sec = ns / 1000000000L;
    ts.tv_nsec = ns % 1000000000L;
    nanosleep(&ts, NULL);

    Node *node = malloc(sizeof(Node));
    if (!node) die_errno("malloc");

    node->str = str;
    node->next = NULL;

    pthread_mutex_lock(&mutex);

    if (tail == NULL) {
        head = node;
        tail = node;
    } else {
        tail->next = node;
        tail = node;
    }

    pthread_mutex_unlock(&mutex);

    return NULL;
}

int main() {
    char *strings[MAX_LINES];

    char *line = malloc(MAX_LEN);
    if (!line) die_errno("malloc");

    int count = 0;
    while (count < MAX_LINES && fgets(line, MAX_LEN, stdin) != NULL) {
        line[strcspn(line, "\n")] = '\0';

        strings[count] = strdup(line);
        if (!strings[count]) die_errno("strdup");
        
        count++;
    }
    free(line);

    printf("\nArray output:\n");

    pthread_t *threads = NULL;
    if (count > 0) {
        threads = malloc(sizeof(pthread_t) * count);
        if (!threads) die_errno("malloc");
    }

    int err;
    for (int i = 0; i < count; i++) {
        err = pthread_create(&threads[i], NULL, worker, (void *)strings[i]);
        die_pthread(err, "pthread_create");
    }

    for (int i = 0; i < count; i++) {
        err = pthread_join(threads[i], NULL);
        die_pthread(err, "pthread_join");
    }

    for (Node *cur = head; cur != NULL; cur = cur->next) {
        printf("%s\n", cur->str);
    }

    Node *cur = head;
    while (cur != NULL) {
        Node *next = cur->next;
        free(cur->str);
        free(cur);
        cur = next;
    }
    free(threads);

    return 0;
}
