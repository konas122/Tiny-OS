#include "fs.h"
#include "file.h"
#include "pipe.h"
#include "debug.h"
#include "thread.h"
#include "string.h"
#include "memory.h"
#include "process.h"
#include "interrupt.h"

#include "fork.h"


extern void intr_exit(void);


static int32_t copy_pcb_vaddrbitmap_stack0(task_struct *child_thread, task_struct *parent_thread) {
    memcpy(child_thread, parent_thread, PG_SIZE);
    child_thread->pid = fork_pid();
    child_thread->elapsed_ticks = 0;
    child_thread->status = TASK_READY;
    child_thread->ticks = child_thread->priority;
    child_thread->parent_pid = parent_thread->pid;
    child_thread->general_tag.prev = child_thread->general_tag.next = NULL;
    child_thread->all_list_tag.prev = child_thread->all_list_tag.next = NULL;
    block_desc_init(child_thread->u_block_desc);

    uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8 , PG_SIZE);
    void *vaddr_btmp = get_kernel_pages(bitmap_pg_cnt);
    if (vaddr_btmp == NULL) {
        return -1;
    }

    memcpy(vaddr_btmp, parent_thread->userprog_vaddr.vaddr_bitmap.bits, bitmap_pg_cnt * PG_SIZE);
    child_thread->userprog_vaddr.vaddr_bitmap.bits = (uint8_t *)vaddr_btmp;
    ASSERT(strlen(child_thread->name) < 15);    // pcb.name 的长度是 16, 为避免下面 strcat 越界
    strcat(child_thread->name, "f");
    return 0;
}


static void copy_body_stack3(task_struct *child_thread, task_struct *parent_thread, void *buf_page) {
    uint32_t prog_vaddr = 0;
    uint8_t *vaddr_btmp = parent_thread->userprog_vaddr.vaddr_bitmap.bits;
    uint32_t btmp_bytes_len = parent_thread->userprog_vaddr.vaddr_bitmap.btmp_bytes_len;
    uint32_t vaddr_start = parent_thread->userprog_vaddr.vaddr_start;

    for (uint32_t idx_byte = 0; idx_byte < btmp_bytes_len; idx_byte++) {
        if (vaddr_btmp[idx_byte]) {
            for (uint32_t idx_bit = 0; idx_bit < 8; idx_bit++) {
                if ((BITMAP_MASK << idx_bit) & vaddr_btmp[idx_byte]) {
                    prog_vaddr = (idx_byte * 8 + idx_bit) * PG_SIZE + vaddr_start;

                    /**
                     * a 将父进程在用户空间中的数据复制到内核缓冲区 buf_page,
                     * 目的是下面切换到子进程的页表后,还能访问到父进程的数据
                     */
                    memcpy(buf_page, (void*)prog_vaddr, PG_SIZE);

                    /* b 将页表切换到子进程, 目的是避免下面申请内存的函数将 pte 及 pde 安装在父进程的页表中 */
                    page_dir_activate(child_thread);
                    /* c 申请虚拟地址 prog_vaddr */
                    get_a_page_without_opvaddrbitmap(PF_USER, prog_vaddr);

                    /* d 从内核缓冲区中将父进程数据复制到子进程的用户空间 */
                    memcpy((void *)prog_vaddr, buf_page, PG_SIZE);

                    /* e 恢复父进程页表 */
                    page_dir_activate(parent_thread);
                }
            }
        }
    }
}


static int32_t build_child_stack(task_struct *child_thread) {
    intr_stack *intr_0_stack = (intr_stack *)((uint32_t)child_thread + PG_SIZE - sizeof(intr_stack));
    intr_0_stack->eax = 0;  // 修改子进程的返回值为 0

    // 为 switch_to 构建栈帧, 将其构建在紧临 intr_stack 之下的空间
    uint32_t *ret_addr_in_thread_stack = (uint32_t *)intr_0_stack - 1;

    uint32_t *esi_ptr_in_thread_stack = (uint32_t *)intr_0_stack - 2;
    uint32_t *edi_ptr_in_thread_stack = (uint32_t *)intr_0_stack - 3;
    uint32_t *ebx_ptr_in_thread_stack = (uint32_t *)intr_0_stack - 4;
    uint32_t *ebp_ptr_in_thread_stack = (uint32_t *)intr_0_stack - 5;

    // switch_to 的返回地址更新为 intr_exit, 直接从中断返回
    *ret_addr_in_thread_stack = (uint32_t)intr_exit;

    *ebp_ptr_in_thread_stack = *ebx_ptr_in_thread_stack =
        *edi_ptr_in_thread_stack = *esi_ptr_in_thread_stack = 0;

    // 把构建的 switch_to 栈帧的栈顶做为 switch_to 恢复数据时的栈顶
    child_thread->self_kstack = ebp_ptr_in_thread_stack;
    return 0;
}


static void update_inode_open_cnts(task_struct *thread) {
    int32_t local_fd = 3, global_fd = 0;
    while (local_fd < MAX_FILES_OPEN_PER_PROC) {
        global_fd = thread->fd_table[local_fd];
        ASSERT(global_fd < MAX_FILE_OPEN);
        if (global_fd != -1) {
            if (is_pipe(local_fd)) {
                file_table[global_fd].fd_pos++;
            }
            else {
                file_table[global_fd].fd_inode->i_open_cnts++;
            }
        }
        local_fd++;
    }
}


static int32_t copy_process(task_struct *child_thread, task_struct *parent_thread) {
    // 内核缓冲区, 作为父进程用户空间的数据复制到子进程用户空间的中转
    void *buf_page = get_kernel_pages(1);
    if (buf_page == NULL) {
        return -1;
    }

    // 复制父进程的 pcb, 虚拟地址位图, 内核栈到子进程
    if (copy_pcb_vaddrbitmap_stack0(child_thread, parent_thread) == -1) {
        return -1;
    }

    // 为子进程创建页表, 此页表仅包括内核空间
    child_thread->pgdir = create_page_dir();
    if (child_thread->pgdir == NULL) {
        return -1;
    }

    // 复制父进程进程体及用户栈给子进程
    copy_body_stack3(child_thread, parent_thread, buf_page);

    // 构建子进程 thread_stack 和修改返回值 pid
    build_child_stack(child_thread);

    // 更新文件 inode 的打开数
    update_inode_open_cnts(child_thread);

    add_cwd(child_thread->cwd_inode_nr);

    mfree_page(PF_KERNEL, buf_page, 1);
    return 0;
}


pid_t sys_fork(void) {
    task_struct *parent_thread = running_thread();
    task_struct *child_thread = (task_struct *)get_kernel_pages(1);
    if (child_thread == NULL) {
        return -1;
    }
    ASSERT(INTR_OFF == intr_get_status() && parent_thread->pgdir != NULL);

    if (copy_process(child_thread, parent_thread) == -1) {
        return -1;
    }

    ASSERT(!elem_find(&thread_ready_list, &child_thread->general_tag));
    list_append(&thread_ready_list, &child_thread->general_tag);

    ASSERT(!elem_find(&thread_all_list, &child_thread->all_list_tag));
    list_append(&thread_all_list, &child_thread->all_list_tag);

    return child_thread->pid;
}
