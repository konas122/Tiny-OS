#include "fs.h"
#include "thread.h"    
#include "string.h"
#include "global.h"
#include "memory.h"
#include "stdio_kernel.h"

#include "exec.h"


extern void intr_exit(void);

typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;


// 32 位 elf 头
typedef struct Elf32_Ehdr {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
} Elf32_Ehdr;


// 程序头表 Program header, 即段描述头
typedef struct Elf32_Phdr {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
} Elf32_Phdr;


// 段类型
typedef enum segment_type {
    PT_NULL,    // 忽略
    PT_LOAD,    // 可加载程序段
    PT_DYNAMIC, // 动态加载信息 
    PT_INTERP,  // 动态加载器名称
    PT_NOTE,    // 一些辅助信息
    PT_SHLIB,   // 保留
    PT_PHDR     // 程序头表
} segment_type;


// 将文件描述符 fd 指向的文件中, 偏移为 offset, 大小为 filesz 的段加载到虚拟地址为 vaddr 的内存
static bool segment_load(int32_t fd, uint32_t offset, uint32_t filesz, uint32_t vaddr) {
    uint32_t occupy_pages = 0;
    uint32_t vaddr_first_page = vaddr & 0xfffff000;                 // vaddr 地址所在的页框
    uint32_t size_in_first_page = PG_SIZE - (vaddr & 0x00000fff);   // 加载到内存后, 文件在第一个页框中占用的字节大小

    // 若一个页框容不下该段
    if (filesz > size_in_first_page) {
        uint32_t left_size = filesz - size_in_first_page;
        occupy_pages = DIV_ROUND_UP(left_size, PG_SIZE) + 1;    // 1 是指 vaddr_first_page
    }
    else {
        occupy_pages = 1;
    }

    // 为进程分配内存
    uint32_t vaddr_page = vaddr_first_page;
    for (uint32_t page_idx = 0; page_idx < occupy_pages; ++page_idx) {
        uint32_t *pde = pde_vaddr(vaddr_page);
        uint32_t *pte = pte_vaddr(vaddr_page);

        if (!(*pde & 0x00000001) || !(*pte & 0x00000001)) {
            if (get_a_page(PF_USER, vaddr_page) == NULL) {
                return false;
            }
        }
        vaddr_page += PG_SIZE;
    }

    sys_lseek(fd, offset, SEEK_SET);
    sys_read(fd, (void *)vaddr, filesz);
    return true;
}


// 从文件系统上加载用户程序 pathname
static int32_t load(const char *pathname) {
    int32_t ret = -1;
    Elf32_Ehdr elf_header;
    Elf32_Phdr prog_header;
    memset(&elf_header, 0, sizeof(Elf32_Ehdr));

    int32_t fd = sys_open(pathname, O_RDONLY);
    if (fd == -1) {
        return -1;
    }

    if (sys_read(fd, &elf_header, sizeof(Elf32_Ehdr)) != sizeof(Elf32_Ehdr)) {
        ret = -1;
        goto done;
    }

    // 校验 ELF 头
    if (memcmp(elf_header.e_ident, "\177ELF\1\1\1", 7) \
        || elf_header.e_type != 2 \
        || elf_header.e_machine != 3 \
        || elf_header.e_version != 1 \
        || elf_header.e_phnum > 1024 \
        || elf_header.e_phentsize != sizeof(Elf32_Phdr))
    {
        ret = -1;
        goto done;
    }

    Elf32_Off prog_header_offset = elf_header.e_phoff; 
    Elf32_Half prog_header_size = elf_header.e_phentsize;

    // 遍历所有程序头

    for (uint32_t prog_idx = 0; prog_idx < elf_header.e_phnum; ++prog_idx) {
        memset(&prog_header, 0, prog_header_size);
        
        // 将文件的指针定位到程序头
        sys_lseek(fd, prog_header_offset, SEEK_SET);

        // 只获取程序头
        if (sys_read(fd, &prog_header, prog_header_size) != prog_header_size) {
            ret = -1;
            goto done;
        }

        // 如果是可加载段就调用 segment_load 加载到内存
        if (PT_LOAD == prog_header.p_type) {
            if (!segment_load(fd, prog_header.p_offset, prog_header.p_filesz, prog_header.p_vaddr)) {
                ret = -1;
                goto done;
            }
        }

        // 更新下一个程序头的偏移
        prog_header_offset += elf_header.e_phentsize;
    }
    ret = elf_header.e_entry;

done:
    sys_close(fd);
    return ret;
}


int32_t sys_execv(const char* path, const char* argv[]) {
    uint32_t argc = 0;
    while (argv[argc]) {
        argc++;
    }

    int32_t entry_point = load(path);
    if (entry_point == -1) {
        return -1;
    }

    task_struct *cur = running_thread();

    // 修改进程名
    memcpy(cur->name, path, TASK_NAME_LEN);
    cur->name[TASK_NAME_LEN - 1] = 0;

    intr_stack *intr_0_stack = (intr_stack *)((uint32_t)cur + PG_SIZE - sizeof(intr_stack));

    // 参数传递给用户进程
    intr_0_stack->ebx = (int32_t)argv;
    intr_0_stack->ecx = argc;
    intr_0_stack->eip = (void *)entry_point;
    intr_0_stack->esp = (void *)0xc0000000;     // 使新用户进程的栈地址为最高用户空间地址

    asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(intr_0_stack) : "memory");
    return 0;
}
