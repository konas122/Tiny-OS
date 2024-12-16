#include "fs.h"
#include "fork.h"
#include "print.h"
#include "stdint.h"
#include "string.h"
#include "thread.h"
#include "console.h"
#include "syscall.h"

#include "syscall_init.h"


#define syscall_nr 32
typedef void *syscall;

syscall syscall_table[syscall_nr];


uint32_t sys_getpid(void) {
    return running_thread()->pid;
}


void sys_putchar(char char_asci) {
    console_put_char(char_asci);
}


void syscall_init(void) {
    put_str("\nsyscall_init start\n");

    syscall_table[SYS_GETPID] = (void *)sys_getpid;
    syscall_table[SYS_WRITE] = (void *)sys_write;
    syscall_table[SYS_MALLOC] = (void *)sys_malloc;
    syscall_table[SYS_FREE] = (void *)sys_free;
    syscall_table[SYS_FORK] = (void *)sys_fork;
    syscall_table[SYS_PUTCHAR] = (void *)sys_putchar;
    syscall_table[SYS_CLEAR] = (void *)cls_screen;

    put_str("syscall_init done\n");
}
