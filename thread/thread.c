#include "stdint.h"
#include "global.h"
#include "memory.h"
#include "string.h"

#include "thread.h"


#define PG_SIZE 4096


static void kernel_thread(thread_func* function, void* func_arg) {
    function(func_arg); 
}


void thread_create(task_struct* pthread, thread_func function, void* func_arg) {
    pthread->self_kstack -= sizeof(intr_stack);      // 预留中断使用栈的空间
    pthread->self_kstack -= sizeof(thread_stack);    // 留出线程栈空间

    thread_stack *kthread_stack = (thread_stack *)pthread->self_kstack;
    kthread_stack->eip = kernel_thread;
    kthread_stack->function = function;
    kthread_stack->func_arg = func_arg;
    kthread_stack->ebp = kthread_stack->ebx = kthread_stack->esi = kthread_stack->edi = 0;
}


void init_thread(task_struct* pthread, char* name, int prio) {
    memset(pthread, 0, sizeof(*pthread));
    strcpy(pthread->name, name);
    pthread->status = TASK_RUNNING; 
    pthread->priority = prio;

    // self_kstack 是线程自己在内核态下使用的栈顶地址
    pthread->self_kstack = (uint32_t *)((uint32_t)pthread + PG_SIZE);
    pthread->stack_magic = 0x19780506;
}


task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg) {
    task_struct* thread = get_kernel_pages(1);

    init_thread(thread, name, prio);
    thread_create(thread, function, func_arg);

    asm volatile (" \
        movl %0, %%esp; \
        pop %%ebp; \
        pop %%ebx; \
        pop %%edi; \
        pop %%esi; \
        ret" : : "g" (thread->self_kstack) : "memory");
    return thread;
}
