#ifndef __USERPROG_SYSCALLINIT_H__
#define __USERPROG_SYSCALLINIT_H__

#include "stdint.h"

uint32_t sys_getpid(void);
void sys_putchar(char char_asci);

void syscall_init(void);

#endif
