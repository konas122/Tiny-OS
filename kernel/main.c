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

    printf("\n________  create and write file1  ________\n");
    int32_t fd = sys_open("/file1", O_RDWR | O_CREAT);
    printf("open /file1, fd:%d\n", fd);
    char buf[64] = {0};
    sys_write(fd, "hello,world\n", 12);
    sys_close(fd);

    printf("_________  read and close file1  _________\n");
    fd = sys_open("/file1", O_RDWR);
    memset(buf, 0, 64);
    int read_bytes = sys_read(fd, buf, 24);
    printf("read %d bytes: %s", read_bytes, buf);

    sys_close(fd);
    printf("%d closed now\n", fd);

    printf("____________  delete file1 _______________\n");
    printf("/file1 delete %s!\n", sys_unlink("/file1") == 0 ? "done" : "fail");

    printf("_____________  mkdir dir1 ________________\n");
    printf("/dir1 create %s!\n", sys_mkdir("/dir1") == 0 ? "done" : "fail");
    printf("/dir1/subdir1 create %s!\n", sys_mkdir("/dir1/subdir1") == 0 ? "done" : "fail");

    printf("________  create and write file2  ________\n");
    fd = sys_open("/dir1/subdir1/file2", O_CREAT|O_RDWR);
    if (fd != -1) {
        printf("/dir1/subdir1/file2 create done!\n");
        sys_write(fd, "Catch me if you can!\n", 21);
        sys_lseek(fd, 0, SEEK_SET);
        char buf[32] = {0};
        sys_read(fd, buf, 21);
        printf("/dir1/subdir1/file2 says: %s", buf);
        sys_close(fd);
        printf("%d closed now\n", fd);
    }

    printf("__________________________________________\n");
    printf("/dir1 content before delete /dir1/subdir1:\n");
    dir* dir_ptr = sys_opendir("/dir1/");
    char* type = NULL;
    dir_entry* dir_e = NULL;
    while((dir_e = sys_readdir(dir_ptr))) { 
        if (dir_e->f_type == FT_REGULAR) {
            type = "regular";
        }
        else {
            type = "directory";
        }
        printf("     %s    %s\n", type, dir_e->filename);
    }
    printf("try to delete an non-empty directory /dir1/subdir1\n");
    if (sys_rmdir("/dir1/subdir1") == -1) {
        printf("sys_rmdir: /dir1/subdir1 delete fail!\n");
    }

    printf("try to delete /dir1/subdir1/file2\n");
    if (sys_rmdir("/dir1/subdir1/file2") == -1) {
        printf("sys_rmdir: /dir1/subdir1/file2 delete fail!\n");
    } 
    if (sys_unlink("/dir1/subdir1/file2") == 0) {
        printf("sys_unlink: /dir1/subdir1/file2 delete done\n");
    }
    
    printf("try to delete directory /dir1/subdir1 again\n");
    if (sys_rmdir("/dir1/subdir1") == 0) {
        printf("/dir1/subdir1 delete done!\n");
    }

    printf("/dir1 content after delete /dir1/subdir1:\n");
    sys_rewinddir(dir_ptr);
    while((dir_e = sys_readdir(dir_ptr))) { 
        if (dir_e->f_type == FT_REGULAR) {
            type = "regular";
        }
        else {
            type = "directory";
        }
        printf("    %s    %s\n", type, dir_e->filename);
    }

    printf("__________________________________________\n");
    char cwd_buf[32] = {0};
    sys_getcwd(cwd_buf, 32);
    printf("cwd: %s\n", cwd_buf);
    sys_chdir("/dir1");
    sys_getcwd(cwd_buf, 32);
    printf("change cwd to: %s\n", cwd_buf);

    printf("try to delete directory /dir1\n");
    if (sys_rmdir("/dir1") == 0) {
        printf("/dir1 delete done!\n");
    }

    sys_chdir("/");
    sys_getcwd(cwd_buf, 32);
    printf("change cwd to: %s\n", cwd_buf);

    printf("try to delete directory /dir1 again\n");
    if (sys_rmdir("/dir1") == 0) {
        printf("/dir1 delete done!\n");
    }

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
