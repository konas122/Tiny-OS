#include "fs.h"
#include "thread.h"
#include "syscall.h"


#define _syscall0(NUMBER) ({ \
    int retval;              \
    asm volatile(            \
        "int $0x80"          \
        : "=a"(retval)       \
        : "a"(NUMBER)        \
        : "memory");         \
    retval;                  \
})

#define _syscall1(NUMBER, ARG1) ({ \
    int retval;                    \
    asm volatile(                  \
        "int $0x80"                \
        : "=a"(retval)             \
        : "a"(NUMBER), "b"(ARG1)   \
        : "memory");               \
    retval;                        \
})

#define _syscall2(NUMBER, ARG1, ARG2) ({    \
    int retval;                             \
    asm volatile(                           \
        "int $0x80"                         \
        : "=a"(retval)                      \
        : "a"(NUMBER), "b"(ARG1), "c"(ARG2) \
        : "memory");                        \
    retval;                                 \
})

#define _syscall3(NUMBER, ARG1, ARG2, ARG3) ({         \
    int retval;                                        \
    asm volatile(                                      \
        "int $0x80"                                    \
        : "=a"(retval)                                 \
        : "a"(NUMBER), "b"(ARG1), "c"(ARG2), "d"(ARG3) \
        : "memory");                                   \
    retval;                                            \
})

#define _syscall4(NUMBER, ARG1, ARG2, ARG3, ARG4) ({              \
    int retval;                                                   \
    asm volatile(                                                 \
        "int $0x80"                                               \
        : "=a"(retval)                                            \
        : "a"(NUMBER), "b"(ARG1), "c"(ARG2), "d"(ARG3), "S"(ARG4) \
        : "memory");                                              \
    retval;                                                       \
})

#define _syscall5(NUMBER, ARG1, ARG2, ARG3, ARG4, ARG5) ({                   \
    int retval;                                                              \
    asm volatile(                                                            \
        "int $0x80"                                                          \
        : "=a"(retval)                                                       \
        : "a"(NUMBER), "b"(ARG1), "c"(ARG2), "d"(ARG3), "S"(ARG4), "D"(ARG5) \
        : "memory");                                                         \
    retval;                                                                  \
})


uint32_t getpid() {
    return _syscall0(SYS_GETPID);
}


uint32_t write(int32_t fd, const void* buf, uint32_t count) {
    return _syscall3(SYS_WRITE, fd, buf, count);
}


void *malloc(uint32_t size) {
    return (void *)_syscall1(SYS_MALLOC, size);
}


void free(void *ptr) {
    _syscall1(SYS_FREE, ptr);
}


pid_t fork(void){
    return _syscall0(SYS_FORK);
}


int32_t read(int32_t fd, void* buf, uint32_t count) {
    return _syscall3(SYS_READ, fd, buf, count);
}


void putchar(char char_asci) {
    _syscall1(SYS_PUTCHAR, char_asci);
}


void clear(void) {
    _syscall0(SYS_CLEAR);
}


char* getcwd(char* buf, uint32_t size) {
    return (char*)_syscall2(SYS_GETCWD, buf, size);
}


int32_t open(char* pathname, uint8_t flag) {
    return _syscall2(SYS_OPEN, pathname, flag);
}


int32_t close(int32_t fd) {
    return _syscall1(SYS_CLOSE, fd);
}


int32_t lseek(int32_t fd, int32_t offset, uint8_t whence) {
    return _syscall3(SYS_LSEEK, fd, offset, whence);
}


int32_t unlink(const char* pathname) {
    return _syscall1(SYS_UNLINK, pathname);
}


int32_t mkdir(const char* pathname) {
    return _syscall1(SYS_MKDIR, pathname);
}


struct dir* opendir(const char* name) {
    return (struct dir*)_syscall1(SYS_OPENDIR, name);
}


int32_t closedir(struct dir* dir) {
    return _syscall1(SYS_CLOSEDIR, dir);
}


int32_t rmdir(const char* pathname) {
    return _syscall1(SYS_RMDIR, pathname);
}


struct dir_entry* readdir(struct dir* dir) {
    return (struct dir_entry*)_syscall1(SYS_READDIR, dir);
}


void rewinddir(struct dir* dir) {
    _syscall1(SYS_REWINDDIR, dir);
}


int32_t file_stat(const char* path, stat* buf) {
    return _syscall2(SYS_STAT, path, buf);
}


int32_t chdir(const char* path) {
    return _syscall1(SYS_CHDIR, path);
}


void ps(void) {
    _syscall0(SYS_PS);
}


int execv(const char* pathname, char** argv) {
   return _syscall2(SYS_EXECV, pathname, argv);
}


void exit(int32_t status) {
    _syscall1(SYS_EXIT, status);
}


pid_t wait(int32_t* status) {
    return _syscall1(SYS_WAIT, status);
}


int32_t pipe(int32_t pipefd[2]) {
    return _syscall1(SYS_PIPE, pipefd);
}


void fd_redirect(uint32_t old_local_fd, uint32_t new_local_fd) {
    _syscall2(SYS_REDIRECT, old_local_fd, new_local_fd);
}


void help(void) {
    _syscall0(SYS_HELP);
}


void pause(void) {
    _syscall0(SYS_PAUSE);
}

int32_t ldprog(char *filename, uint32_t file_size) {
    return _syscall2(SYS_LDPROG, filename, file_size);
}
