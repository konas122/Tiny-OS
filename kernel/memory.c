#include "sync.h"
#include "list.h"
#include "print.h"
#include "debug.h"
#include "global.h"
#include "stdint.h"
#include "thread.h"
#include "string.h"
#include "interrupt.h"

#include "memory.h"

#define PG_SIZE 4096

/***************************  位图地址 ***************************
 * 因为 0xc009f000 是内核主线程栈顶, 0xc009e000 是内核主线程的 pcb.
 * 一个页框大小的位图可表示 128M 内存, 位图位置安排在地址 0xc009a000,
 * 这样本系统最大支持 4 个页框的位图, 即512M */
#define MEM_BITMAP_BASE 0xc009a000
/****************************************************************/

// 0xc0000000 是内核从虚拟地址 3G 起. 0x100000 意指跨过低端 1M 内存, 使虚拟地址在逻辑上连续
#define K_HEAP_START 0xc0100000

#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22)
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12)


typedef struct pool {
    lock lock;
    bitmap pool_bitmap;
    uint32_t phy_addr_start;
    uint32_t pool_size;
} pool;


// 内存仓库 arena 元信息
typedef struct arena {
    mem_block_desc* desc;
    uint32_t cnt;
    bool large;
} arena;


pool kernel_pool, user_pool;    // 生成内核内存池和用户内存池
virtual_addr kernel_vaddr;      // 此结构是用来给内核分配虚拟地址
mem_block_desc k_block_descs[DESC_CNT]; // 内核内存块描述符数组


static void *vaddr_get(pool_flags pf, uint32_t pg_cnt) {
    int vaddr_start = 0, bit_idx_start = -1;
    uint32_t cnt = 0;
    if (pf == PF_KERNEL) {
        bit_idx_start = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1) {
            return NULL;
        }
        while (cnt < pg_cnt) {
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        }
        vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
    }
    else {
        task_struct *cur = running_thread();
        bit_idx_start = bitmap_scan(&cur->userprog_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1) {
            return NULL;
        }
        while (cnt < pg_cnt) {
            bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        }
        vaddr_start = cur->userprog_vaddr.vaddr_start + bit_idx_start * PG_SIZE;

        // (0xc0000000 - PG_SIZE) 做为用户 3 级栈已经在 start_process 被分配
        ASSERT((uint32_t)vaddr_start < (0xc0000000 - PG_SIZE));
    }
    return (void *)vaddr_start;
}


uint32_t *pde_vaddr(uint32_t vaddr) {
    // 0xfffff000 是用来访问到页表本身所在的地址
    uint32_t *pde = (uint32_t *)((0xfffff000) + PDE_IDX(vaddr) * 4);
    return pde;
}


uint32_t *pte_vaddr(uint32_t vaddr) {
    uint32_t *pte = (uint32_t *)((0xffc00000) + ((vaddr & 0xffc00000) >> 10) + PTE_IDX(vaddr) * 4);
    return pte;
}


// 在 m_pool 指向的物理内存池中分配 1 个物理页
static void *palloc(pool *m_pool) {
    int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1);
    if (bit_idx == -1) {
        return NULL;
    }
    bitmap_set(&m_pool->pool_bitmap, bit_idx, 1);
    uint32_t *page_phyaddr = (uint32_t *)((bit_idx * PG_SIZE) + m_pool->phy_addr_start);
    return (void *)page_phyaddr;
}


// 页表中添加虚拟地址 _vaddr 与物理地址 _page_phyaddr 的映射
static void page_table_map(void *_vaddr, void *_page_phyaddr) {
    uint32_t vaddr = (uint32_t)_vaddr;
    uint32_t page_phyaddr = (uint32_t)_page_phyaddr;

    uint32_t *pde = pde_vaddr(vaddr);
    uint32_t *pte = pte_vaddr(vaddr);

    /**************************  Note  ***************************
     * 执行 *pte, 会访问到空的 pde, 
     * 所以确保 pde 创建完成后才能执行 *pte, 否则会引发 page_fault.
     * 因此在 *pde 为 0 时, *pte 只能出现在下面 else 语句块中的 *pde 后面。
     * *********************************************************/

    // 判断 pde 的 P 位，若为 1, 则表示该表已存在
    if (*pde & 0x00000001) {
        if (likely( !(*pte & 0x00000001) )) {
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1); // US=1, RW=1, P=1
        }
        else {  // [[unlikely]]
            PANIC("pte repeat!!!");
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1); // US=1, RW=1, P=1
        }
    }
    // 页目录项不存在, 所以要先创建页目录再创建页表项.
    else {
        uint32_t pde_phyaddr = (uint32_t)palloc(&kernel_pool);
        *pde = (pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        // 分配到的物理页地址 pde_phyaddr 对应的物理内存清 0
        memset((void *)((uint32_t)pte & 0xfffff000), 0, PG_SIZE);

        ASSERT(!(*pte & 0x00000001));
        *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);     // US=1, RW=1, P=1
    }
}


// 分配 pg_cnt 个页空间
void *malloc_page(pool_flags pf, uint32_t pg_cnt) {
    if (unlikely( pg_cnt <= 0 )) {
        pg_cnt = 1;
    }
    ASSERT(pg_cnt < 3840);  // 15MB/4KB = 3840

    void *vaddr_start = vaddr_get(pf, pg_cnt);
    if (vaddr_start == NULL) {
        return NULL;
    }

    uint32_t cnt = pg_cnt;
    uint32_t vaddr = (uint32_t)vaddr_start;

    pool *mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;

    while (cnt-- > 0) {
        void *page_phyaddr = palloc(mem_pool);
        if (page_phyaddr == NULL) {
            return NULL;
        }
        page_table_map((void *)vaddr, page_phyaddr);
        vaddr += PG_SIZE;
    }
    return vaddr_start;
}


void *get_kernel_pages(uint32_t pg_cnt) {
    lock_acquire(&kernel_pool.lock);
    void *vaddr = malloc_page(PF_KERNEL, pg_cnt);
    if (vaddr != NULL) {
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    }
    lock_release(&kernel_pool.lock);
    return vaddr;
}


void *get_user_pages(uint32_t pg_cnt) {
    lock_acquire(&user_pool.lock);
    void *vaddr = malloc_page(PF_USER, pg_cnt);
    lock_release(&user_pool.lock);
    return vaddr;
}


void *get_a_page(pool_flags pf, uint32_t vaddr) {
    pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    lock_acquire(&mem_pool->lock);

    // 先将虚拟地址对应的位图置 1
    task_struct *cur = running_thread();
    int32_t bit_idx = -1;

    // 若当前是用户进程申请用户内存, 就修改用户进程自己的虚拟地址位图
    if (cur->pgdir != NULL && pf == PF_USER) {
        bit_idx = (vaddr - cur->userprog_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx, 1);
    }
    // 如果是内核线程申请内核内存, 就修改 kernel_vaddr
    else if (cur->pgdir == NULL && pf == PF_KERNEL) {
        bit_idx = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx, 1);
    }
    else {
        PANIC("get_a_page: not allow kernel alloc userspace or user alloc kernelspace by `get_a_page`");
    }
    void *page_phyaddr = palloc(mem_pool);
    if (page_phyaddr == NULL) {
        return NULL;
    }
    page_table_map((void *)vaddr, page_phyaddr);
    lock_release(&mem_pool->lock);
    return (void *)vaddr;
}


uint32_t addr_v2p(uint32_t vaddr) {
    uint32_t *pte = pte_vaddr(vaddr);
    return ((*pte & 0xfffff000) + (vaddr & 0x00000fff));
}


// 返回 arena 中第 idx 个内存块的地址
static mem_block* arena2block(arena *a, uint32_t idx) {
    return (mem_block*)((uint32_t)a + sizeof(arena) + idx * a->desc->block_size);
}


static arena* block2arena(mem_block* b) {
    return (arena *)((uint32_t)b & 0xfffff000);
}


void *sys_malloc(uint32_t size) {
    pool_flags pf;
    pool *mem_pool;
    uint32_t pool_size;
    mem_block_desc *desc;
    task_struct *cur = running_thread();

    if (cur->pgdir == NULL) {
        pf = PF_KERNEL;
        pool_size = kernel_pool.pool_size;
        mem_pool = &kernel_pool;
        desc = k_block_descs;
    }
    else {
        pf = PF_USER;
        pool_size = user_pool.pool_size;
        mem_pool = &user_pool;
        desc = cur->u_block_desc;
    }

    if (!(size > 0 && size < pool_size)) {
        return NULL;
    }
    arena *a;
    mem_block *b;
    lock_acquire(&mem_pool->lock);

    if (size > 1024) {
        uint32_t page_cnt = DIV_ROUND_UP(size + sizeof(arena), PG_SIZE);
        a = (arena *)malloc_page(pf, page_cnt);

        if (a != NULL) {
            memset(a, 0, page_cnt * PG_SIZE);
            a->desc = NULL;
            a->cnt = page_cnt;
            a->large = true;
            lock_release(&mem_pool->lock);
            return (void *)(a + 1);
        }
        else {
            lock_release(&mem_pool->lock);
            return NULL;
        }
    }
    else {
        uint8_t desc_idx;
        for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++) {
            if (size <= desc[desc_idx].block_size) {
                break;
            }
        }

        if (list_empty(&desc[desc_idx].free_list)) {
            a = (arena *)malloc_page(pf, 1);
            if (a == NULL) {
                lock_release(&mem_pool->lock);
                return NULL;
            }
            memset(a, 0, PG_SIZE);

            a->desc = &desc[desc_idx];
            a->large = false;
            a->cnt = desc[desc_idx].blocks_per_arena;

            intr_status old_status = intr_disable();

            // 开始将 arena 拆分成内存块, 并添加到内存块描述符的 free_list 中
            for (uint32_t block_idx = 0; block_idx < desc[desc_idx].blocks_per_arena; block_idx++) {
                b = arena2block(a, block_idx);
                ASSERT(!elem_find(&a->desc->free_list, &b->free_elem));
                list_append(&a->desc->free_list, &b->free_elem);
            }
            intr_set_status(old_status);
        }

        b = elem2entry(mem_block, free_elem, list_pop(&(desc[desc_idx].free_list)));
        memset(b, 0, desc[desc_idx].block_size);
        a = block2arena(b);
        a->cnt--;
        lock_release(&mem_pool->lock);
        return (void *)b;
    }
    return NULL;
}


void pfree(uint32_t pg_phy_addr) {
    pool *mem_pool;
    uint32_t bit_idx = 0;

    if (pg_phy_addr >= user_pool.phy_addr_start) {
        mem_pool = &user_pool;
        bit_idx = (pg_phy_addr - user_pool.phy_addr_start) / PG_SIZE;
    }
    else {
        mem_pool = &kernel_pool;
        bit_idx = (pg_phy_addr - kernel_pool.phy_addr_start) / PG_SIZE;
    }
    bitmap_set(&mem_pool->pool_bitmap, bit_idx, 0);
}


static void page_table_unmap(uint32_t vaddr) {
    uint32_t *pte = pte_vaddr(vaddr);
    *pte &= ~PG_P_1;
    asm volatile ("invlpg %0"::"m" (vaddr):"memory");   // 更新 tlb
}


static void vaddr_remove(pool_flags pf, void *_vaddr, uint32_t pg_cnt) {
    uint32_t bit_idx_start = 0, vaddr = (uint32_t)_vaddr, cnt = 0;
    if (pf == PF_KERNEL) {
        bit_idx_start = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        while (cnt < pg_cnt) {
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 0);
        }
    }
    else {
        task_struct *cur = running_thread();
        bit_idx_start = (vaddr - cur->userprog_vaddr.vaddr_start) / PG_SIZE;
        while (cnt < pg_cnt) {
            bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 0);
        }
    }
}


void mfree_page(pool_flags pf, void* _vaddr, uint32_t pg_cnt) {
    uint32_t pg_phy_addr;
    uint32_t vaddr = (uint32_t)_vaddr, page_cnt = 0;
    pg_phy_addr = addr_v2p(vaddr);

    ASSERT(pg_cnt >= 1 && vaddr % PG_SIZE == 0);
    // 确保待释放的物理内存在低端 1M+1k 大小的页目录 +1k 大小的页表地址范围外
    ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= 0x102000);

    if (pg_phy_addr >= user_pool.phy_addr_start) {
        vaddr -= PG_SIZE;
        while (page_cnt < pg_cnt) {
            vaddr += PG_SIZE;
            pg_phy_addr = addr_v2p(vaddr);

            ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= user_pool.phy_addr_start);

            pfree(pg_phy_addr);
            page_table_unmap(vaddr);
            page_cnt++;
        }
        vaddr_remove(pf, _vaddr, pg_cnt);
    }
    else {
        vaddr -= PG_SIZE;	      
        while (page_cnt < pg_cnt) {
            vaddr += PG_SIZE;
            pg_phy_addr = addr_v2p(vaddr);

            ASSERT((pg_phy_addr % PG_SIZE) == 0 && \
                pg_phy_addr >= kernel_pool.phy_addr_start && \
                pg_phy_addr < user_pool.phy_addr_start
            );

            pfree(pg_phy_addr);
            page_table_unmap(vaddr);
            page_cnt++;
        }
        vaddr_remove(pf, _vaddr, pg_cnt);
    }
}


void sys_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    pool_flags PF;
    pool *mem_pool;

    task_struct *cur = running_thread();
    if (cur->pgdir == NULL) {
        PF = PF_KERNEL;
        mem_pool = &kernel_pool;
        ASSERT((uint32_t)ptr > K_HEAP_START);
    }
    else {
        PF = PF_USER;
        mem_pool = &user_pool;
    }
    lock_acquire(&mem_pool->lock);
    mem_block *b = ptr;
    arena *a = block2arena(b);
    ASSERT(a->large == 0 || a->large == 1);
    if (a->desc == NULL && a->large == true) {
        mfree_page(PF, a, a->cnt);
    }
    else {
        list_append(&a->desc->free_list, &b->free_elem);
        if (++a->cnt == a->desc->blocks_per_arena) {
            uint32_t block_idx;
            for (block_idx = 0; block_idx < a->desc->blocks_per_arena; block_idx++) {
                mem_block*  b = arena2block(a, block_idx);
                ASSERT(elem_find(&a->desc->free_list, &b->free_elem));
                list_remove(&b->free_elem);
            }
            mfree_page(PF, a, 1); 
        } 
    }
    lock_release(&mem_pool->lock);
}


static void mem_pool_init(uint32_t all_mem) {
    put_str("    mem_pool_init start\n");
    uint32_t page_table_size = PG_SIZE * 256;   // 页表大小 = 第 0 和第 768 个页目录项指向同一个页表 +
                                                // 第 769-1022 个页目录项共指向 254 个页表, 共 256 个页框
    uint32_t used_mem = page_table_size + 0x100000;
    uint32_t free_mem = all_mem - used_mem;
    uint16_t all_free_pages = free_mem / PG_SIZE;

    uint32_t kernel_free_pages = all_free_pages / 2;
    uint16_t user_free_pages = all_free_pages - kernel_free_pages;

    uint32_t kbm_length = kernel_free_pages / 8;
    uint32_t ubm_length = user_free_pages / 8;

    uint32_t kp_start = used_mem;
    uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE;

    kernel_pool.phy_addr_start = kp_start;
    user_pool.phy_addr_start = up_start;

    kernel_pool.pool_size = kernel_free_pages * PG_SIZE;
    user_pool.pool_size = user_free_pages * PG_SIZE;

    kernel_pool.pool_bitmap.btmp_bytes_len = kbm_length;
    user_pool.pool_bitmap.btmp_bytes_len = ubm_length;

    /*********    内核内存池和用户内存池位图   ***********
     *   位图是全局的数据，长度不固定.
     *   全局或静态的数组需要在编译时知道其长度,
     *   而我们需要根据总内存大小算出需要多少字节.
     *   所以改为指定一块内存来生成位图.
     *   ************************************************/
    // 内核使用的最高地址是 0xc009f000, 这是主线程的栈地址. (内核的大小预计为 70K 左右)
    // 32M 内存占用的位图是 2K. 内核内存池的位图先定在 MEM_BITMAP_BASE(0xc009a000) 处.
    kernel_pool.pool_bitmap.bits = (void *)MEM_BITMAP_BASE;
    // 用户内存池的位图紧跟在内核内存池位图之后
    user_pool.pool_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length);

    put_str("    kernel_pool_bitmap_start: 0x");
    put_int((int)kernel_pool.pool_bitmap.bits);
    put_str("\n");
    put_str("    kernel_pool_phy_addr_start: 0x");
    put_int(kernel_pool.phy_addr_start);
    put_str("\n");
    put_str("    user_pool_bitmap_start: 0x");
    put_int((int)user_pool.pool_bitmap.bits);
    put_str("\n");
    put_str("    user_pool_phy_addr_start: 0x");
    put_int(user_pool.phy_addr_start);
    put_str("\n");

    // 将位图置 0
    bitmap_init(&kernel_pool.pool_bitmap);
    bitmap_init(&user_pool.pool_bitmap);

    lock_init(&kernel_pool.lock);
    lock_init(&user_pool.lock);

    // 下面初始化内核虚拟地址的位图,按实际物理内存大小生成数组
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length;
    kernel_vaddr.vaddr_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length + ubm_length);
    kernel_vaddr.vaddr_start = K_HEAP_START;
    bitmap_init(&kernel_vaddr.vaddr_bitmap);
    put_str("    mem_pool_init done\n");
}


// 为 malloc 做准备
void block_desc_init(mem_block_desc* desc_array) {
    uint16_t desc_idx, block_size = 16;

    for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++) {
        desc_array[desc_idx].block_size = block_size;

        // 初始化 arena 中的内存块数量
        desc_array[desc_idx].blocks_per_arena = (PG_SIZE - sizeof(arena)) / block_size;
        list_init(&desc_array[desc_idx].free_list);

        block_size *= 2;  // 更新为下一个规格内存块
    }
}


void mem_init() {
    put_str("\nmem_init start\n");
    uint32_t mem_bytes_total = (*(uint32_t*)(0xb00));
    mem_pool_init(mem_bytes_total);     // 初始化内存池
    block_desc_init(k_block_descs);
    put_str("mem_init done\n");
}
