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


uint32_t sys_write(char *str) {
    console_put_str(str);
    return strlen(str);
}


void syscall_init(void) {
    put_str("\nsyscall_init start\n");

    syscall_table[SYS_GETPID] = (void *)sys_getpid;
    syscall_table[SYS_WRITE] = (void *)sys_write;
    syscall_table[SYS_MALLOC] = (void *)sys_malloc;
    syscall_table[SYS_FREE] = (void *)sys_free;

    put_str("syscall_init done\n");
}
