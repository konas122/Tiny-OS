#ifndef __THREAD_SYNC_H__
#define __THREAD_SYNC_H__

#include "list.h"
#include "stdint.h"
#include "thread.h"


typedef struct semaphore {
    uint8_t value;
    list    waiters;
} semaphore;


typedef struct lock {
    task_struct *holder;            // 锁的持有者
    semaphore   semaphore;          // 用二元信号量实现锁
    uint32_t    holder_repeat_nr;   // 锁的持有者重复申请锁的次数
} lock;


void sem_init(semaphore *psema, uint8_t value);
void sem_wait(semaphore *psema);
void sem_post(semaphore *psema);

void lock_init(lock *plock);
void lock_acquire(lock *plock);
void lock_release(lock *plock);


#endif
