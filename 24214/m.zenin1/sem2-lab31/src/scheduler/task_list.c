#include <stdlib.h>
#include "scheduler/task_list.h"

task_list_t task_list_construct() {
    task_t *sentinel = malloc(sizeof(task_t));
    sentinel->next = NULL;

    return (task_list_t)
           {
               .first = sentinel,
               .last = sentinel,
               .reads_amount = 0,
               .writes_amount = 0
           };
}

void task_list_append(task_list_t *tl, task_t *task) {
    task->next = NULL;
    tl->last->next = task;
    tl->last = task;

    switch (task->type) {
        case ACCEPT_CONNECTION_REQUESTS:
        case READ_REQUEST:
            tl->reads_amount++;
            break;
        case WRITE_REQUEST:
            tl->writes_amount++;
            break;
    }
}

void task_list_delete(task_list_t *tl, task_t *prev, task_t *this) {
    // Delete task from linked list
    prev->next = this->next;
    
    // If it was last, prev become last
    if (tl->last == this) {
        tl->last = prev;
    }

    switch (this->type) {
        case ACCEPT_CONNECTION_REQUESTS:
        case READ_REQUEST:
            tl->reads_amount--;
            break;
        case WRITE_REQUEST:
            tl->writes_amount--;
            break;
    }
}

void task_list_destruct(task_list_t *tl) {
}
