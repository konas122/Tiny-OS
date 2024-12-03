#include "sync.h"
#include "debug.h"
#include "print.h"
#include "stdint.h"
#include "global.h"
#include "memory.h"
#include "string.h"
#include "process.h"
#include "interrupt.h"

#include "thread.h"


#define PG_SIZE 4096


lock pid_lock;
task_struct *main_thread;   // 主线程PCB
list thread_ready_list;	    // 就绪队列
list thread_all_list;	    // 所有任务队列
static list_elem *thread_tag;   // 用于保存队列中的线程结点


extern void switch_to(task_struct *cur, task_struct *next);


task_struct* running_thread() {
    uint32_t esp; 
    asm ("mov %%esp, %0" : "=g" (esp));
    // 取 esp 整数部分即 pcb 起始地址
    return (task_struct*)(esp & 0xfffff000);
}


static pid_t allocate_pid(void) {
    static pid_t next_pid = -1;
    lock_acquire(&pid_lock);
    next_pid++;
    lock_release(&pid_lock);
    return next_pid;
}


static void kernel_thread(thread_func* function, void* func_arg) {
    // 执行 function 前要开中断, 避免后面的时钟中断被屏蔽, 而无法调度其它线程
    intr_enable();

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


void init_thread(task_struct *pthread, char *name, int prio) {
    memset(pthread, 0, sizeof(*pthread));
    strcpy(pthread->name, name);
    pthread->pid = allocate_pid();
    pthread->status = TASK_RUNNING; 
    pthread->priority = prio;

    // 由于把 main 函数也封装成一个线程, 并且它一直是运行的, 故将其直接设为 TASK_RUNNING
    if (pthread == main_thread) {
        pthread->status = TASK_RUNNING;
    }
    else {
        pthread->status = TASK_READY;
    }

    // self_kstack 是线程自己在内核态下使用的栈顶地址
    pthread->self_kstack = (uint32_t *)((uint32_t)pthread + PG_SIZE);

    pthread->priority = prio;
    pthread->ticks = prio;
    pthread->elapsed_ticks = 0;
    pthread->pgdir = NULL;

    pthread->stack_magic = 0x19780506;
}


task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg) {
    task_struct* thread = get_kernel_pages(1);

    init_thread(thread, name, prio);
    thread_create(thread, function, func_arg);

    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    list_append(&thread_ready_list, &thread->general_tag);

    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    list_append(&thread_all_list, &thread->all_list_tag);

    return thread;
}


static void make_main_thread(void) {
    // 因为 main 线程早已运行, 咱们在 loader.S 中进入内核时的 mov esp, 0xc009f000,
    // 就是为其预留了 tcb, 地址为 0xc009e000, 因此不需要通过 get_kernel_page 另分配一页
    main_thread = running_thread();
    init_thread(main_thread, "main", 31);

    ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
    list_append(&thread_all_list, &main_thread->all_list_tag);
}


void schedule() {
    ASSERT(intr_get_status() == INTR_OFF);

    task_struct *cur = running_thread();
    if (cur->status == TASK_RUNNING) {
        ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
        list_append(&thread_ready_list, &cur->general_tag);
        cur->ticks = cur->priority;
        cur->status = TASK_READY;
    }

    ASSERT(!list_empty(&thread_ready_list));
    thread_tag = NULL;      // thread_tag 清空
    // 将 thread_ready_list 队列中的第一个就绪线程弹出, 准备将其调度上 cpu.
    thread_tag = list_pop(&thread_ready_list);
    task_struct *next = elem2entry(task_struct, general_tag, thread_tag);
    next->status = TASK_RUNNING;

    process_activate(next);
    switch_to(cur, next);
}


void thread_init(void) {
    put_str("\nthread_init start\n");
    list_init(&thread_ready_list);
    list_init(&thread_all_list);
    lock_init(&pid_lock);

    make_main_thread();
    put_str("thread_init done\n");
}


void thread_block(task_status stat) {
    ASSERT((stat == TASK_BLOCKED) || (stat == TASK_WAITING) || (stat == TASK_HANGING));
    intr_status old_stat = intr_disable();
    task_struct *cur_thread = running_thread();
    cur_thread->status = stat;
    schedule();
    intr_set_status(old_stat);
}


void thread_unblock(task_struct *pthread) {
    intr_status old_stat = intr_disable();
    ASSERT((pthread->status == TASK_BLOCKED) || (pthread->status == TASK_WAITING) || (pthread->status == TASK_HANGING));
    if (pthread->status != TASK_READY) {
        ASSERT(!elem_find(&thread_ready_list, &pthread->general_tag));
        list_push(&thread_ready_list, &pthread->general_tag);
        pthread->status = TASK_READY;
    }
    intr_set_status(old_stat);
}
