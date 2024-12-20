#include "fs.h"
#include "list.h"
#include "pipe.h"
#include "file.h"
#include "debug.h"
#include "global.h"
#include "thread.h"
#include "memory.h"
#include "bitmap.h"
#include "stdio_kernel.h"

#include "wait_exit.h"


static void release_prog_resource(task_struct *release_thread) {
    uint32_t *pgdir_vaddr = release_thread->pgdir;

    uint32_t pde = 0;
    uint32_t pte = 0;
    uint32_t *v_pde_ptr = NULL;
    uint32_t *v_pte_ptr = NULL;
    uint16_t user_pde_nr = 768, pde_idx = 0;
    uint16_t user_pte_nr = 1024, pte_idx = 0;

    uint32_t pg_phy_addr = 0;
    uint32_t *first_pte_vaddr_in_pde = NULL;

    while (pde_idx < user_pde_nr) {
        v_pde_ptr = pgdir_vaddr + pde_idx;
        pde = *v_pde_ptr;
        if (pde & 0x00000001) {
            first_pte_vaddr_in_pde = pte_vaddr(pde_idx * 0x400000); // 一个页表表示的内存容量是 4M, 即 0x400000
            pte_idx = 0;
            while (pte_idx < user_pte_nr) {
                v_pte_ptr = first_pte_vaddr_in_pde + pte_idx;
                pte = *v_pte_ptr;
                if (pte & 0x00000001) {
                    pg_phy_addr = pte & 0xfffff000;
                    free_a_phy_page(pg_phy_addr);
                }
                pte_idx++;
            }
            pg_phy_addr = pde & 0xfffff000;
            free_a_phy_page(pg_phy_addr);
        }
        pde_idx++;
    }

    // 回收用户虚拟地址池所占的物理内存
    uint32_t bitmap_pg_cnt = (release_thread->userprog_vaddr.vaddr_bitmap.btmp_bytes_len) / PG_SIZE;
    uint8_t *user_vaddr_pool_bitmap = release_thread->userprog_vaddr.vaddr_bitmap.bits;
    mfree_page(PF_KERNEL, user_vaddr_pool_bitmap, bitmap_pg_cnt);

    // 关闭进程打开的文件
    for (uint8_t fd_idx = 3; fd_idx < MAX_FILES_OPEN_PER_PROC; fd_idx++) {
        if (release_thread->fd_table[fd_idx] != -1) {
            if (is_pipe(fd_idx)) {
                uint32_t global_fd = fd_local2global(fd_idx);
                if (--file_table[global_fd].fd_pos == 0) {
                    mfree_page(PF_KERNEL, file_table[global_fd].fd_inode, 1);
                    file_table[global_fd].fd_inode = NULL;
                }
            }
            else {
                sys_close(fd_idx);
            }
        }
    }
}


static bool find_child(list_elem* pelem, int32_t ppid) {
    task_struct *pthread = elem2entry(task_struct, all_list_tag, pelem);
    if (pthread->parent_pid == ppid) {
        return true;
    }
    return false;
}


static bool find_hanging_child(list_elem *pelem, int32_t ppid) {
    task_struct *pthread = elem2entry(task_struct, all_list_tag, pelem);
    if (pthread->parent_pid == ppid && pthread->status == TASK_HANGING) {
        return true;
    }
    return false;
}


static bool init_adopt_a_child(list_elem *pelem, int32_t pid) {
    task_struct *pthread = elem2entry(task_struct, all_list_tag, pelem);
    if (pthread->parent_pid == pid) {
        pthread->parent_pid = 1;
    }
    return false;
}


pid_t sys_wait(int32_t *status) {
    task_struct *parent_thread = running_thread();

    while (1) {
        // 优先处理已经是挂起状态的任务
        list_elem *child_elem = list_traversal(&thread_all_list, find_hanging_child, parent_thread->pid);

        if (child_elem != NULL) {
            task_struct *child_thread = elem2entry(task_struct, all_list_tag, child_elem);
            *status = child_thread->exit_status;

            uint16_t child_pid = child_thread->pid;

            thread_exit(child_thread, false);
            return child_pid;
        }

        child_elem = list_traversal(&thread_all_list, find_child, parent_thread->pid);
        if (child_elem == NULL) {
            return -1;
        }
        else {
            thread_block(TASK_WAITING);
        }
    }
}


void sys_exit(int32_t status) {
    task_struct *child_thread = running_thread();
    child_thread->exit_status = status;
    if (child_thread->parent_pid == -1) {
        PANIC("sys_exit: child_thread->parent_pid is -1\n");
    }

    list_traversal(&thread_all_list, init_adopt_a_child, child_thread->pid);

    release_prog_resource(child_thread);

    task_struct *parent_thread = pid2thread(child_thread->parent_pid);
    if (parent_thread->status == TASK_WAITING) {
        thread_unblock(parent_thread);
    }

    thread_block(TASK_HANGING);
}
