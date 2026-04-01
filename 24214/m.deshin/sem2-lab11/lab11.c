#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>

#define NUM_LINES 10

pthread_mutex_t m_sync;
pthread_mutex_t m_parent;
pthread_mutex_t m_child;

volatile int child_ready = 0;

static void die_pthread(int err, const char *what) {
    if (err != 0) {
        fprintf(stderr, "%s: %s\n", what, strerror(err));
        exit(EXIT_FAILURE);
    }
}

void* child() {
    die_pthread(pthread_mutex_lock(&m_child), "child: initial lock m_child");
    child_ready = 1;

    for (int i = 0; i < NUM_LINES; i++) {
        die_pthread(pthread_mutex_lock(&m_parent), "child: lock m_parent");
        die_pthread(pthread_mutex_unlock(&m_child), "child: unlock m_child");

        printf("Child: %d\n", i);

        die_pthread(pthread_mutex_lock(&m_sync), "child: lock m_sync");
        die_pthread(pthread_mutex_unlock(&m_parent), "child: unlock m_parent");

        die_pthread(pthread_mutex_lock(&m_child), "child: lock m_child");
        die_pthread(pthread_mutex_unlock(&m_sync), "child: unlock m_sync");
    }
    
    die_pthread(pthread_mutex_unlock(&m_child), "child: final unlock m_child");
    return NULL;
}

int main() {
    pthread_t thread;
    
    pthread_mutexattr_t attr;
    die_pthread(pthread_mutexattr_init(&attr), "mutexattr_init");
    die_pthread(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK), "mutexattr_settype");
    
    die_pthread(pthread_mutex_init(&m_sync, &attr), "mutex_init m_sync");
    die_pthread(pthread_mutex_init(&m_parent, &attr), "mutex_init m_parent");
    die_pthread(pthread_mutex_init(&m_child, &attr), "mutex_init m_child");
    
    die_pthread(pthread_mutex_lock(&m_parent), "main: initial lock m_parent");
    
    die_pthread(pthread_create(&thread, NULL, child, NULL), "pthread_create");

    while (!child_ready) {
        sched_yield();
    }
    
    for (int i = 0; i < NUM_LINES; i++) {
        printf("Parent: %d\n", i);

        die_pthread(pthread_mutex_lock(&m_sync), "main: lock m_sync");
        die_pthread(pthread_mutex_unlock(&m_parent), "main: unlock m_parent");

        die_pthread(pthread_mutex_lock(&m_child), "main: lock m_child");
        die_pthread(pthread_mutex_unlock(&m_sync), "main: unlock m_sync");

        die_pthread(pthread_mutex_lock(&m_parent), "main: lock m_parent");
        die_pthread(pthread_mutex_unlock(&m_child), "main: unlock m_child");
    }
    
    die_pthread(pthread_mutex_unlock(&m_parent), "main: final unlock m_parent");
    
    die_pthread(pthread_join(thread, NULL), "pthread_join");
    
    die_pthread(pthread_mutex_destroy(&m_child), "mutex_destroy m_child");
    die_pthread(pthread_mutex_destroy(&m_parent), "mutex_destroy m_parent");
    die_pthread(pthread_mutex_destroy(&m_sync), "mutex_destroy m_sync");
    die_pthread(pthread_mutexattr_destroy(&attr), "mutexattr_destroy");
    
    return 0;
}
