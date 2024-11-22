#ifndef __KERNEL_MEMORY_H__
#define __KERNEL_MEMORY_H__

#include "stdint.h"
#include "bitmap.h"

#define PG_P_1  1   // 页表项或页目录项存在属性位
#define PG_P_0  0   // 页表项或页目录项存在属性位
#define PG_RW_R 0   // R/W 属性位值, 读/执行
#define PG_RW_W 2   // R/W 属性位值, 读/写/执行
#define PG_US_S 0   // U/S 属性位值, 系统级
#define PG_US_U 4   // U/S 属性位值, 用户级


enum pool_flags {
   PF_KERNEL = 1,   // 内核内存池
   PF_USER = 2      // 用户内存池
};


typedef struct virtual_addr {
    bitmap vaddr_bitmap;
    uint32_t vaddr_start;
} virtual_addr;


void mem_init(void);
void *get_kernel_pages(uint32_t pg_cnt);
void *malloc_page(enum pool_flags pf, uint32_t pg_cnt);
uint32_t *pte_vaddr(uint32_t vaddr);
uint32_t *pde_vaddr(uint32_t vaddr);


#endif // !__KERNEL_MEMORY_H__
