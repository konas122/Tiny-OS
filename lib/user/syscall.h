#ifndef __LIB_USER_SYSCALL_H__
#define __LIB_USER_SYSCALL_H__

#include "stdint.h"


typedef enum SYSCALL_NR {
    SYS_GETPID,
    SYS_WRITE,
    SYS_MALLOC,
    SYS_FREE,
} SYSCALL_NR;


uint32_t getpid(void);
uint32_t write(int32_t fd, const void *buf, uint32_t count);
void *malloc(uint32_t size);
void free(void *ptr);

#endif
