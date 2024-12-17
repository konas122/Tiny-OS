#include "dir.h"
#include "init.h"
#include "print.h"
#include "debug.h"
#include "string.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "process.h"
#include "interrupt.h"

#include "assert.h"
#include "shell.h"
#include "stdio.h"
#include "syscall.h"
#include "syscall_init.h"


void init(void);


int main(void) {
    put_str("\nI am Kernel\n");

    init_all();

    console_put_str("main_pid: 0x");
    console_put_int(sys_getpid());
    console_put_str("\n");

    cls_screen();
    process_execute(init, "init");

    while(1) {
        // console_put_str("Main ");
    };

    return 0;
}


void init(void) {
    uint32_t ret_pid = fork();
    if (ret_pid) {
        while(1);
    }
    else {
        my_shell();
    }
    panic("init: should not be here");
}
