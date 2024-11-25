#ifndef __THREAD_THREAD_H__
#define __THREAD_THREAD_H__

#include "stdint.h"


typedef void thread_func(void *);


enum task_status {
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_WAITING,
    TASK_HANGING,
    TASK_DIED
};


typedef struct intr_stack {
    uint32_t vec_no;    // kernel.S 宏 VECTOR 中 push %1 压入的中断号
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy; // 虽然 pushad 把 esp 也压入, 但 esp 是不断变化的, 所以会被 popad 忽略
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;

    // 以下由 cpu 从低特权级进入高特权级时压入
    uint32_t err_code;  // err_code 会被压入在 eip 之后
    void (*eip) (void);
    uint32_t cs;
    uint32_t eflags;
    void* esp;
    uint32_t ss;
} intr_stack;


typedef struct thread_stack {
    // ABI 规定
    uint32_t ebp;
    uint32_t ebx;
    uint32_t edi;
    uint32_t esi;

    // 线程第一次执行时, eip 指向待调用的函数 kernel_thread.
    // 其它时候, eip 是指向 switch_to 的返回地址,
    // 即用于保存任务切换后的新任务的返回地址.
    void (*eip)(thread_func* func, void* func_arg);

    // 参数 unused_ret 只为占位置充数为返回地址
    void(*unused_retaddr);
    thread_func *function;
    void* func_arg;
} thread_stack;


typedef struct task_struct {
    uint32_t *self_kstack;  // 各内核线程都用自己的内核栈
    enum task_status status;
    uint8_t priority;
    char name[16];
    uint32_t stack_magic;   // 用这串数字做栈的边界标记,用于检测栈的溢出
} task_struct;


void thread_create(task_struct *pthread, thread_func function, void *func_arg);
void init_thread(task_struct *pthread, char *name, int prio);
task_struct *thread_start(char *name, int prio, thread_func function, void *func_arg);


#endif
