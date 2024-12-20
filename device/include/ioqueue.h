#ifndef __DEVICE_IOQUEUE_H__
#define __DEVICE_IOQUEUE_H__


#include "sync.h"
#include "stdint.h"
#include "thread.h"

#define bufsize 64


typedef struct ioqueue {
    lock lock;

    task_struct *producer;
    task_struct *consumer;

    char buf[bufsize];
    int32_t head;
    int32_t tail;
} ioqueue;


void ioqueue_init(ioqueue *ioq);
bool ioq_full(ioqueue *ioq);
bool ioq_empty(ioqueue *ioq);
char ioq_getchar(ioqueue *ioq);
void ioq_putchar(ioqueue *ioq, char byte);

uint32_t ioq_length(ioqueue *ioq);

#endif
