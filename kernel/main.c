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

#include "shell.h"
#include "stdio.h"
#include "assert.h"
#include "syscall.h"
#include "syscall_init.h"
#include "stdio_kernel.h"


void init(void);


int main(void) {
    put_str("\nI am Kernel\n");

    init_all();

    console_put_str("main_pid: 0x");
    console_put_int(sys_getpid());
    console_put_str("\n");

    cls_screen();

    uint32_t file_size = 21828; 
    uint32_t sec_cnt = DIV_ROUND_UP(file_size, 512);
    disk *sda = &channels[0].devices[0];
    void *prog_buf = sys_malloc(file_size);
    ide_read(sda, 300, prog_buf, sec_cnt);
    int32_t fd = sys_open("/prog", O_CREAT | O_RDWR);
    if (fd != -1) {
        if (sys_write(fd, prog_buf, file_size) == -1) {
            sys_close(fd);
            printk("file write error!\n");
            while(1);
        }
    }
    sys_close(fd);

    process_execute(init, "init");

    while(1);

    return 0;
}


void init(void) {
    uint32_t ret_pid = fork();
    if (ret_pid) {
        int status;
        int child_pid;
        while(1) {
            child_pid = wait(&status);
            printf("I'm init, My pid is 1, I recieve a child, Its pid is %d, status is %d\n", child_pid, status);
            pause();
        }
    }
    else {
        my_shell();
    }
    panic("init: should not be here");
}
