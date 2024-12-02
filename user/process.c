#include "tss.h"
#include "list.h"
#include "debug.h"
#include "global.h"
#include "memory.h"
#include "thread.h"
#include "string.h"
#include "console.h"
#include "interrupt.h"

#include "process.h"


extern void intr_exit(void);


void start_process(void *filename_) {
    void *function = filename_;
    task_struct *cur = running_thread();
    cur->self_kstack += sizeof(thread_stack);                   // 跨过 thread_stack, 指向 intr_stack
    intr_stack *proc_stack = (intr_stack *)cur->self_kstack;    // 可以不用定义成结构体指针

    proc_stack->edi = proc_stack->esi = proc_stack->ebp = proc_stack->esp_dummy \
                    = proc_stack->ebx = proc_stack->edx = proc_stack->ecx = proc_stack->eax = 0;

    proc_stack->gs = 0;         // 不太允许用户态直接访问显存资源, 用户态用不上, 直接初始为 0
    proc_stack->ds = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;
    proc_stack->eip = function; // 待执行的用户程序地址
    proc_stack->cs = SELECTOR_U_CODE;
    proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);
    proc_stack->esp = (void *)((uint32_t)get_a_page(PF_USER, USER_STACK3_VADDR) + PG_SIZE);
    proc_stack->ss = SELECTOR_U_DATA;
    asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (proc_stack) : "memory");
}


void page_dir_activate(task_struct *p_thread) {
    /********************************************************
     * 执行此函数时, 当前任务可能是线程
     * 之所以对线程也要重新安装页表, 原因是上一次被调度的可能是进程,
     * 否则不恢复页表的话, 线程就会使用上个被调度进程的页表了
     ********************************************************/

    // 若为内核线程, 需要重新填充页表为 0x100000
    uint32_t pagedir_phy_addr = 0x100000;   // 默认为内核的页目录物理地址, 也就是内核线程所用的页目录表
    if (p_thread->pgdir != NULL) {  // 用户态进程有自己的页目录表
        pagedir_phy_addr = addr_v2p((uint32_t)p_thread->pgdir);
    }

    asm volatile ("movl %0, %%cr3" : : "r" (pagedir_phy_addr) : "memory");
}


void process_activate(task_struct *p_thread) {
    ASSERT(p_thread != NULL);
    page_dir_activate(p_thread);

    // 内核线程特权级本身就是 0 特权级, 处理器进入中断时并不会从 tss 中获取 0 特权级栈地址, 故不需要更新 esp0
    if (p_thread->pgdir) {
        // 更新该进程的 esp0, 用于此进程被中断时保留上下文
        update_tss_esp(p_thread);
    }
}


uint32_t *create_page_dir(void) {
    uint32_t *page_dir_vaddr = (uint32_t *)get_kernel_pages(1);
    if (page_dir_vaddr == NULL) {
        console_put_str("create_page_dir: `get_kernel_page` failed!");
        return NULL;
    }

    // Build pde of kernel
    // page_dir_vaddr + 0x300*4 是内核页目录的第 768 项
    memcpy((uint32_t *)((uint32_t)page_dir_vaddr + 0x300 * 4),
           (uint32_t *)(0xfffff000 + 0x300 * 4), 1024);
    
    // 用户进程占据第 0-767 的页目录项, 内核则占据第 768-1023 的页目录项. ((1023-786+1)*4 = 1024)

    // 让 pde 中的最后一项指向 pde 的起始地址
    uint32_t new_page_dir_phyaddr = addr_v2p((uint32_t)page_dir_vaddr);
    page_dir_vaddr[1023] = new_page_dir_phyaddr | PG_US_U | PG_RW_W | PG_P_1;

    return page_dir_vaddr;
}


void create_user_vaddr_bitmap(task_struct *user_prog) {
    user_prog->userprog_vaddr.vaddr_start = USER_VADDR_START;
    uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE);
    user_prog->userprog_vaddr.vaddr_bitmap.bits = (uint8_t *)get_kernel_pages(bitmap_pg_cnt);
    user_prog->userprog_vaddr.vaddr_bitmap.btmp_bytes_len = (0xc0000000 - USER_VADDR_START) / PG_SIZE / 8;
    bitmap_init(&user_prog->userprog_vaddr.vaddr_bitmap);
}


void process_execute(void *filename, char *name) {
    task_struct *thread = (task_struct *)get_kernel_pages(1);
    init_thread(thread, name, default_prio);
    create_user_vaddr_bitmap(thread);
    thread_create(thread, start_process, filename);
    thread->pgdir = create_page_dir();

    intr_status old_status = intr_disable();

    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    list_append(&thread_ready_list, &thread->general_tag);

    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    list_append(&thread_all_list, &thread->all_list_tag);

    intr_set_status(old_status);
}
