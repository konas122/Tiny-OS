#include "list.h"
#include "debug.h"
#include "global.h"
#include "interrupt.h"

#include "sync.h"


void sem_init(semaphore *psema, uint8_t value) {
    psema->value = value;
    list_init(&psema->waiters);
}


void lock_init(lock *plock) {
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
    sem_init(&plock->semaphore, 1);
}


void sem_wait(semaphore *psema) {
    intr_status old_status = intr_disable();
    while (psema->value == 0) {
        task_struct *cur = running_thread();
        ASSERT(!elem_find(&psema->waiters, &cur->general_tag));
        list_append(&psema->waiters, &cur->general_tag);
        thread_block(TASK_BLOCKED);
    }
    psema->value--;
    ASSERT(psema->value == 0);
    intr_set_status(old_status);
}


void sem_post(semaphore *psema) {
    intr_status old_status = intr_disable();
    ASSERT(psema->value == 0);
    if (!list_empty(&psema->waiters)) {
        task_struct *thread_blocked = elem2entry(task_struct, general_tag, list_pop(&psema->waiters));
        thread_unblock(thread_blocked);
    }
    psema->value++;
    ASSERT(psema->value == 1);
    intr_set_status(old_status);
}


void lock_acquire(lock *plock) {
    task_struct *cur = running_thread();
    if (plock->holder != cur) {
        sem_wait(&plock->semaphore);
        plock->holder = cur;
        ASSERT(plock->holder_repeat_nr == 0);
        plock->holder_repeat_nr = 1;
    }
    else {
        plock->holder_repeat_nr++;
    }
}


void lock_release(lock *plock) {
#ifndef NDEBUG
    task_struct *cur = running_thread();
#endif
    ASSERT(plock->holder == cur);
    if (plock->holder_repeat_nr > 1) {
        plock->holder_repeat_nr--;
        return;
    }
    ASSERT(plock->holder_repeat_nr == 1);
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
    sem_post(&plock->semaphore);
}
