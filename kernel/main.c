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

    process_execute(init, "init");

    while(1) {
        // console_put_str("Main ");
    };

    return 0;
}

void init(void) {
    uint32_t ret_pid = fork();
    if (ret_pid) {
        printf("i am father, my pid is %d, child pid is %d\n", getpid(), ret_pid);
    }
    else {
        printf("i am child, my pid is %d, ret pid is %d\n", getpid(), ret_pid);
    }
    while(1);
}
