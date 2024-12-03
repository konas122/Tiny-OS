#ifndef __USERPROG_SYSCALLINIT_H__
#define __USERPROG_SYSCALLINIT_H__

#include "stdint.h"

uint32_t sys_getpid(void);
uint32_t sys_write(char *str);

void syscall_init(void);

#endif
