#include "fs.h"
#include "sync.h"
#include "file.h"
#include "debug.h"
#include "print.h"
#include "stdio.h"
#include "stdint.h"
#include "global.h"
#include "memory.h"
#include "string.h"
#include "process.h"
#include "interrupt.h"

#include "thread.h"


#define PG_SIZE 4096


// pid 的位图, 最大支持 1024 个 pid
uint8_t pid_bitmap_bits[128] = {0};

// pid 池
struct pid_pool {
    bitmap pid_bitmap;  // pid 位图
    uint32_t pid_start; // 起始 pid
    lock pid_lock;      // 分配 pid 锁
} pid_pool;


task_struct *main_thread;       // 主线程 PCB
task_struct *idle_thread;       // idle 线程
list thread_ready_list;         // 就绪队列
list thread_all_list;           // 所有任务队列
static list_elem *thread_tag;   // 用于保存队列中的线程结点


extern void switch_to(task_struct *cur, task_struct *next);


// 系统空闲时运行的线程
static void idle(void *arg UNUSED) {
    while (1) {
        thread_block(TASK_BLOCKED);
        // 执行 hlt 时必须要保证目前处在开中断的情况下
        asm volatile ("sti; hlt" : : : "memory");
    }
}


task_struct* running_thread() {
    uint32_t esp;
    asm ("mov %%esp, %0" : "=g" (esp));
    // 取 esp 整数部分即 pcb 起始地址
    return (task_struct*)(esp & 0xfffff000);
}


static void pid_pool_init(void) {
    pid_pool.pid_start = 1;
    pid_pool.pid_bitmap.bits = pid_bitmap_bits;
    pid_pool.pid_bitmap.btmp_bytes_len = 128;
    bitmap_init(&pid_pool.pid_bitmap);
    lock_init(&pid_pool.pid_lock);
}


static pid_t allocate_pid(void) {
    lock_acquire(&pid_pool.pid_lock);
    int32_t bit_idx = bitmap_scan(&pid_pool.pid_bitmap, 1);
    bitmap_set(&pid_pool.pid_bitmap, bit_idx, 1);
    lock_release(&pid_pool.pid_lock);
    return (bit_idx + pid_pool.pid_start);
}


void release_pid(pid_t pid) {
    lock_acquire(&pid_pool.pid_lock);
    int32_t bit_idx = pid - pid_pool.pid_start;
    bitmap_set(&pid_pool.pid_bitmap, bit_idx, 0);
    lock_release(&pid_pool.pid_lock);
}


pid_t fork_pid(void) {
    return allocate_pid();
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

    pthread->fd_table[0] = 0;
    pthread->fd_table[1] = 1;
    pthread->fd_table[2] = 2;
    for (int i = 3; i < MAX_FILES_OPEN_PER_PROC; ++i) {
        pthread->fd_table[i] = -1;
    }

    pthread->cwd_inode_nr = 0;  // 默认工作目录是根目录
    pthread->parent_pid = -1;
    pthread->stack_magic = 0x19780506;
}


task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg) {
    task_struct* thread = (task_struct *)get_kernel_pages(1);

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

    // 如果就绪队列中没有可运行的任务, 就唤醒 idle
    if (list_empty(&thread_ready_list)) {
        thread_unblock(idle_thread);
    }

    thread_tag = NULL;      // thread_tag 清空
    // 将 thread_ready_list 队列中的第一个就绪线程弹出, 准备将其调度上 cpu.
    thread_tag = list_pop(&thread_ready_list);
    task_struct *next = elem2entry(task_struct, general_tag, thread_tag);
    next->status = TASK_RUNNING;

    process_activate(next);
    switch_to(cur, next);
}


static void pad_print(char* buf, int32_t buf_len, void* ptr, char format) {
    memset(buf, 0, buf_len);
    uint8_t out_pad_0idx = 0;
    switch(format) {
    case 's':
        out_pad_0idx = sprintf(buf, "%s", ptr);
        break;
    case 'd':
        out_pad_0idx = sprintf(buf, "%d", *((int16_t*)ptr));
        __attribute__((fallthrough));
    case 'x':
        out_pad_0idx = sprintf(buf, "%x", *((uint32_t*)ptr));
    }
    while(out_pad_0idx < buf_len) { // 以空格填充
        buf[out_pad_0idx] = ' ';
        out_pad_0idx++;
    }
    sys_write(stdout_no, buf, buf_len - 1);
}


static bool elem2thread_info(struct list_elem* pelem, int arg UNUSED) {
    task_struct *pthread = elem2entry(task_struct, all_list_tag, pelem);
    char out_pad[16] = {0};

    pad_print(out_pad, 16, &pthread->pid, 'd');

    if (pthread->parent_pid == -1) {
        pad_print(out_pad, 16, "NULL", 's');
    }
    else {
        pad_print(out_pad, 16, &pthread->parent_pid, 'd');
    }

    switch (pthread->status) {
    case 0:
        pad_print(out_pad, 16, "RUNNING", 's');
        break;
    case 1:
        pad_print(out_pad, 16, "READY", 's');
        break;
    case 2:
        pad_print(out_pad, 16, "BLOCKED", 's');
        break;
    case 3:
        pad_print(out_pad, 16, "WAITING", 's');
        break;
    case 4:
        pad_print(out_pad, 16, "HANGING", 's');
        break;
    case 5:
        pad_print(out_pad, 16, "DIED", 's');
    }
    pad_print(out_pad, 16, &pthread->elapsed_ticks, 'x');

    memset(out_pad, 0, 16);
    ASSERT(strlen(pthread->name) < 17);
    memcpy(out_pad, pthread->name, strlen(pthread->name));
    strcat(out_pad, "\n");
    sys_write(stdout_no, out_pad, strlen(out_pad));

    // 此处返回 false 是为了迎合主调函数 list_traversal, 只有回调函数返回 false 时才会继续调用此函数
    return false;
}


void sys_ps(void) {
    char* ps_title = "PID            PPID           STAT           TICKS          COMMAND\n";
    sys_write(stdout_no, ps_title, strlen(ps_title));
    list_traversal(&thread_all_list, elem2thread_info, 0);
}


void thread_exit(task_struct *thread_over, bool need_schedule) {
    intr_status old_status = intr_disable();
    thread_over->status = TASK_DIED;

    if (elem_find(&thread_ready_list, &thread_over->general_tag)) {
        list_remove(&thread_over->general_tag);
    }
    if (thread_over->pgdir) {
        mfree_page(PF_KERNEL, thread_over->pgdir, 1);
    }

    list_remove(&thread_over->all_list_tag);

    // main_thread 不在 pcb 堆中
    if (thread_over != main_thread) {
        mfree_page(PF_KERNEL, thread_over, 1);
    }

    release_pid(thread_over->pid);

    if (need_schedule) {
        schedule();
        PANIC("thread_exit: should not be here\n");
    }
    else {
        intr_set_status(old_status);
    }
}


static bool pid_check(list_elem* pelem, int pid) {
    pid = (pid_t)pid;
    task_struct *pthread = elem2entry(task_struct, all_list_tag, pelem);
    if (pthread->pid == pid) {
        return true;
    }
    return false;
}


task_struct* pid2thread(pid_t pid) {
    list_elem *pelem = list_traversal(&thread_all_list, pid_check, pid);
    if (pelem == NULL) {
        return NULL;
    }
    task_struct *thread = elem2entry(task_struct, all_list_tag, pelem);
    return thread;
}


void thread_init(void) {
    put_str("\nthread_init start\n");
    list_init(&thread_ready_list);
    list_init(&thread_all_list);
    pid_pool_init();

    make_main_thread();
    idle_thread = thread_start("idle", 10, idle, NULL);

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


void thread_yield(void) {
    task_struct *cur = running_thread();
    intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
    list_append(&thread_ready_list, &cur->general_tag);
    cur->status = TASK_READY;
    schedule();
    intr_set_status(old_status);
}
