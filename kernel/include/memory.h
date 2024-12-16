#ifndef __KERNEL_MEMORY_H__
#define __KERNEL_MEMORY_H__

#include "list.h"
#include "stdint.h"
#include "bitmap.h"

#define PG_P_1  1   // 页表项或页目录项存在属性位
#define PG_P_0  0   // 页表项或页目录项存在属性位
#define PG_RW_R 0   // R/W 属性位值, 读/执行
#define PG_RW_W 2   // R/W 属性位值, 读/写/执行
#define PG_US_S 0   // U/S 属性位值, 系统级
#define PG_US_U 4   // U/S 属性位值, 用户级

#define DESC_CNT 7  // 内存描述符个数


typedef enum pool_flags {
   PF_KERNEL = 1,   // 内核内存池
   PF_USER = 2      // 用户内存池
} pool_flags;


typedef struct virtual_addr {
    bitmap vaddr_bitmap;
    uint32_t vaddr_start;
} virtual_addr;


typedef struct mem_block {
    list_elem free_elem;
} mem_block;


typedef struct mem_block_desc {
    uint32_t block_size;
    uint32_t blocks_per_arena;
    list free_list;
} mem_block_desc;


void mem_init(void);

void *sys_malloc(uint32_t size);
void mfree_page(pool_flags pf, void *_vaddr, uint32_t pg_cnt);
void pfree(uint32_t pg_phy_addr);
void sys_free(void *ptr);

void block_desc_init(mem_block_desc *desc_array);

void *get_kernel_pages(uint32_t pg_cnt);
void *get_user_pages(uint32_t pg_cnt);
// 将地址 vaddr 与 pf 池中的物理地址关联, 仅支持一页空间分配
void *get_a_page(pool_flags pf, uint32_t vaddr);
void *get_a_page_without_opvaddrbitmap(pool_flags pf, uint32_t vaddr);

uint32_t addr_v2p(uint32_t vaddr);

void *malloc_page(pool_flags pf, uint32_t pg_cnt);
uint32_t *pte_vaddr(uint32_t vaddr);
uint32_t *pde_vaddr(uint32_t vaddr);


#endif // !__KERNEL_MEMORY_H__
