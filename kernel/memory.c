#include "print.h"
#include "debug.h"
#include "global.h"
#include "stdint.h"
#include "memory.h"
#include "string.h"

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
    bitmap pool_bitmap;
    uint32_t phy_addr_start;
    uint32_t pool_size;
} pool;

pool kernel_pool, user_pool;    // 生成内核内存池和用户内存池
virtual_addr kernel_vaddr;      // 此结构是用来给内核分配虚拟地址


static void *vaddr_get(enum pool_flags pf, uint32_t pg_cnt) {
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
        // TODO
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
        memset((void *)((int)pte & 0xfffff000), 0, PG_SIZE);

        ASSERT(!(*pte & 0x00000001));
        *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);     // US=1, RW=1, P=1
    }
}


// 分配 pg_cnt 个页空间
void *malloc_page(enum pool_flags pf, uint32_t pg_cnt) {
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


void* get_kernel_pages(uint32_t pg_cnt) {
    void *vaddr = malloc_page(PF_KERNEL, pg_cnt);
    if (vaddr != NULL) {
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    }
    return vaddr;
}


static void mem_pool_init(uint32_t all_mem) {
    put_str("\nmem_pool_init start\n");
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

    // 下面初始化内核虚拟地址的位图,按实际物理内存大小生成数组
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length;
    kernel_vaddr.vaddr_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length + ubm_length);
    kernel_vaddr.vaddr_start = K_HEAP_START;
    bitmap_init(&kernel_vaddr.vaddr_bitmap);
    put_str("    mem_pool_init done\n");
}


void mem_init() {
    put_str("mem_init start\n");
    uint32_t mem_bytes_total = (*(uint32_t*)(0xb00));
    mem_pool_init(mem_bytes_total);     // 初始化内存池
    put_str("mem_init done\n");
}
