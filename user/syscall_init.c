#include "fs.h"
#include "fork.h"
#include "exec.h"
#include "pipe.h"
#include "print.h"
#include "stdint.h"
#include "string.h"
#include "thread.h"
#include "console.h"
#include "syscall.h"
#include "wait_exit.h"
#include "stdio_kernel.h"

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


void sys_help(void) {
   printk("\
\n  buildin commands:\n\
      ls: show directory or file information\n\
      cd: change current work directory\n\
      mkdir: create a directory\n\
      rmdir: remove a empty directory\n\
      rm: remove a regular file\n\
      pwd: show current work directory\n\
      ps: show process information\n\
      clear: clear screen\n\
  shortcut key:\n\
      ctrl+l: clear screen\n\
      ctrl+u: clear input\n\n");
}


void syscall_init(void) {
    put_str("\nsyscall_init start\n");

    syscall_table[SYS_GETPID] = (void *)sys_getpid;
    syscall_table[SYS_WRITE] = (void *)sys_write;
    syscall_table[SYS_MALLOC] = (void *)sys_malloc;
    syscall_table[SYS_FREE] = (void *)sys_free;
    syscall_table[SYS_FORK] = (void *)sys_fork;
    syscall_table[SYS_READ] = (void *)sys_read;
    syscall_table[SYS_PUTCHAR] = (void *)sys_putchar;
    syscall_table[SYS_CLEAR] = (void *)cls_screen;
    syscall_table[SYS_GETCWD] = (void *)sys_getcwd;
    syscall_table[SYS_OPEN] = (void *)sys_open;
    syscall_table[SYS_CLOSE] = (void *)sys_close;
    syscall_table[SYS_LSEEK] = (void *)sys_lseek;
    syscall_table[SYS_UNLINK] = (void *)sys_unlink;
    syscall_table[SYS_MKDIR] = (void *)sys_mkdir;
    syscall_table[SYS_OPENDIR] = (void *)sys_opendir;
    syscall_table[SYS_CLOSEDIR] = (void *)sys_closedir;
    syscall_table[SYS_CHDIR] = (void *)sys_chdir;
    syscall_table[SYS_RMDIR] = (void *)sys_rmdir;
    syscall_table[SYS_READDIR] = (void *)sys_readdir;
    syscall_table[SYS_REWINDDIR] = (void *)sys_rewinddir;
    syscall_table[SYS_STAT] = (void *)sys_stat;
    syscall_table[SYS_PS] = (void *)sys_ps;
    syscall_table[SYS_EXECV] = (void *)sys_execv;
    syscall_table[SYS_EXIT] = (void *)sys_exit;
    syscall_table[SYS_WAIT] = (void *)sys_wait;
    syscall_table[SYS_PIPE] = (void *)sys_pipe;
    syscall_table[SYS_FD_REDIRECT] = (void *)sys_fd_redirect;
    syscall_table[SYS_HELP] = (void *)sys_help;

    put_str("syscall_init done\n");
}
