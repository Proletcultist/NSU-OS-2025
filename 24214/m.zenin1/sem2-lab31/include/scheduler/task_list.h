#pragma once

#include <time.h>
#include "scheduler/task.h"

typedef struct task_list {
    task_t *first;
    task_t *last;

    size_t reads_amount;
    size_t writes_amount;
} task_list_t;

int task_list_construct(task_list_t *tl);
void task_list_append(task_list_t *tl, task_t *task);
void task_list_add_first(task_list_t *tl, task_t *task);
void task_list_delete(task_list_t *tl, task_t *prev, task_t *this);
void task_list_destruct(task_list_t *tl);
