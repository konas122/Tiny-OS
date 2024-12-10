#include "fs.h"
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


void k_thread_a(void *);
void k_thread_b(void *);
void u_prog_a(void);
void u_prog_b(void);


int main(void) {
    put_str("\nI am Kernel\n");

    init_all();

    console_put_str("main_pid: 0x");
    console_put_int(sys_getpid());
    console_put_str("\n");

    thread_start("k_thread_a", 31, k_thread_a, "A_ ");
    thread_start("k_thread_b", 31, k_thread_b, "B_ ");

    process_execute(u_prog_a, "user_prog_a");
    process_execute(u_prog_b, "user_prog_b");

   uint32_t fd = sys_open("/file1", O_RDWR | O_CREAT);
   printf("open /file1, fd:%d\n", fd);
   char buf[64] = {0};
    sys_write(fd, "hello,world\n", 12);
    sys_close(fd);

   printf("________  close file1 and reopen  ________\n");
   fd = sys_open("/file1", O_RDWR);
   memset(buf, 0, 64);
   int read_bytes = sys_read(fd, buf, 24);
   printf("4_ read %d bytes:\n%s", read_bytes, buf);

    sys_close(fd);
    printf("%d closed now\n", fd);

    while(1) {
        // console_put_str("Main ");
    };

    return 0;
}


void k_thread_a(void* arg) {
    console_put_str(arg);
    console_put_str(": 0x");
    console_put_int(sys_getpid());
    console_put_str("\n");

    while (1);
}


void k_thread_b(void* arg) {
    console_put_str(arg);
    console_put_str(": 0x");
    console_put_int(sys_getpid());
    console_put_str("\n");

    while (1);
}


void u_prog_a(void) {
    char *name = "prog_a";
    printf("I am %s, my pid:%d%c", name, getpid(), '\n');

    while (1);
}


void u_prog_b(void) {
    char *name = "prog_b";
    printf("I am %s, my pid:%d%c", name, getpid(), '\n');
    void *a = malloc(12);
    free(a);

    while (1);
}
