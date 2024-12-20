#include "ide.h"
#include "dir.h"
#include "list.h"
#include "file.h"
#include "pipe.h"
#include "inode.h"
#include "debug.h"
#include "global.h"
#include "string.h"
#include "memory.h"
#include "console.h"
#include "ioqueue.h"
#include "keyboard.h"
#include "super_block.h"
#include "stdio_kernel.h"


typedef struct cwd_dir {
    uint32_t inode_no;
    list_elem cwd_tag;
} cwd_dir;


partition *cur_part;


static bool find_cwd_dir(list_elem *pelem, int inode_no) {
    uint32_t i_no = (uint32_t)inode_no;
    cwd_dir *pcwd = elem2entry(cwd_dir, cwd_tag, pelem);
    if (pcwd->inode_no == i_no) {
        return true;
    }
    return false;
}


// 在分区链表中找到名为 part_name 的分区, 并将其指针赋值给 cur_part
static bool mount_partition(list_elem *pelem, int arg) {
    char *part_name = (char *)arg;
    partition *part = elem2entry(partition, part_tag, pelem);

    if (strcmp(part->name, part_name) != 0) {
        return false;
    }

    cur_part = part;
    disk* hd = cur_part->my_disk;

    super_block *sb_buf = (super_block *)sys_malloc(SECTOR_SIZE);
    if (sb_buf == NULL) {
        PANIC("alloc memory failed!");
    }

    cur_part->sb = (super_block *)sys_malloc(sizeof(super_block));
    if (cur_part->sb == NULL) {
        PANIC("alloc memory failed!");
    }

    memset(sb_buf, 0, SECTOR_SIZE);
    ide_read(hd, cur_part->start_lba + 1, sb_buf, 1);
    memcpy(cur_part->sb, sb_buf, sizeof(super_block));

    // block bitmap
    cur_part->block_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->block_bitmap_sects * SECTOR_SIZE);
    if (cur_part->block_bitmap.bits == NULL) {
        PANIC("alloc memory failed!");
    }
    cur_part->block_bitmap.btmp_bytes_len = sb_buf->block_bitmap_sects * SECTOR_SIZE;
    ide_read(hd, sb_buf->block_bitmap_lba, cur_part->block_bitmap.bits, sb_buf->block_bitmap_sects);

    // inode bitmap
    cur_part->inode_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE);
    if (cur_part->inode_bitmap.bits == NULL) {
        PANIC("alloc memory failed!");
    }
    cur_part->inode_bitmap.btmp_bytes_len = sb_buf->inode_bitmap_sects * SECTOR_SIZE;
    ide_read(hd, sb_buf->inode_bitmap_lba, cur_part->inode_bitmap.bits, sb_buf->inode_bitmap_sects);

    list_init(&cur_part->open_inodes);
    list_init(&cur_part->cwd_dirs);

    printk("mount %s done!\n", part->name);
    sys_free(sb_buf);

    return true;
}

// 格式化分区, 也就是初始化分区的元信息, 创建文件系统
static void partition_format(partition *part) {
    uint32_t boot_sector_sects = 1;
    uint32_t super_block_sects = 1;
    uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);
    uint32_t inode_table_sects = DIV_ROUND_UP(((sizeof(inode) * MAX_FILES_PER_PART)), SECTOR_SIZE);

    uint32_t used_sects = boot_sector_sects + super_block_sects + inode_bitmap_sects + inode_table_sects;
    uint32_t free_sects = part->sec_cnt - used_sects;

    // 简单处理块位图占据的扇区数
    uint32_t block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);
    uint32_t block_bitmap_bit_len = free_sects - block_bitmap_sects;    // 可用块的数量
    block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR);

    // 超级块初始化
    super_block sb;
    sb.magic = 0x19780506;
    sb.sec_cnt = part->sec_cnt;
    sb.inode_cnt = MAX_FILES_PER_PART;
    sb.part_lba_base = part->start_lba;

    // 第 0 块是引导块, 第 1 块是超级块
    sb.block_bitmap_lba = sb.part_lba_base + 2;
    sb.block_bitmap_sects = block_bitmap_sects;

    sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects;
    sb.inode_bitmap_sects = inode_bitmap_sects;

    sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
    sb.inode_table_sects = inode_table_sects;

    sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;
    sb.root_inode_no = 0;
    sb.dir_entry_size = sizeof(dir_entry);

    printk("%s info:\n", part->name);
    printk("    magic: 0x%x\n    part_lba_base: 0x%x\n    all_sectors: 0x%x\n    inode_cnt: 0x%x\n    block_bitmap_lba: 0x%x\n    block_bitmap_sectors: 0x%x\n    inode_bitmap_lba: 0x%x\n    inode_bitmap_sectors: 0x%x\n    inode_table_lba: 0x%x\n    inode_table_sectors: 0x%x\n    data_start_lba: 0x%x\n", sb.magic, sb.part_lba_base, sb.sec_cnt, sb.inode_cnt, sb.block_bitmap_lba, sb.block_bitmap_sects, sb.inode_bitmap_lba, sb.inode_bitmap_sects, sb.inode_table_lba, sb.inode_table_sects, sb.data_start_lba);

    disk *hd = part->my_disk;

    /*************************************
     * 1 将 super_block 写入本分区的 1 扇区
     *************************************/
    ide_write(hd, part->start_lba + 1, &sb, 1);
    printk("    super_block_lba: 0x%x\n", part->start_lba + 1);

    // 找出数据量最大的元信息, 用其尺寸做存储缓冲区
    uint32_t buf_size = (sb.block_bitmap_sects >= sb.inode_bitmap_sects ? sb.block_bitmap_sects : sb.inode_bitmap_sects);
    buf_size = (buf_size >= sb.inode_table_sects ? buf_size : sb.inode_table_sects) * SECTOR_SIZE;
    uint8_t* buf = (uint8_t*)sys_malloc(buf_size);  // 申请的内存由内存管理系统清 0 后返回


    /**************************************************
     * 2 初始化 block_bitmap 并写入 sb.block_bitmap_lba
     **************************************************/
    // 初始化块位图 block_bitmap
    buf[0] |= 0x01;     // 第 0 个块预留给根目录, 位图中先占位
    uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;
    uint8_t  block_bitmap_last_bit  = block_bitmap_bit_len % 8;
    // last_size 是位图所在最后一个扇区中, 不足一扇区的其余部分
    uint32_t last_size = SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE);

    // 将位图最后一字节到其所在的扇区的结束全置为 1, 即超出实际块数的部分直接置为已占用
    memset(&buf[block_bitmap_last_byte], 0xff, last_size);

    // 将上一步中覆盖的最后一字节内的有效位重新置 0
    uint8_t bit_idx = 0;
    while (bit_idx <= block_bitmap_last_bit) {
        buf[block_bitmap_last_byte] &= ~(1 << bit_idx++);
    }
    ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects);


    /**************************************************
     * 3 初始化 inode_bitmap 并写入 sb.inode_bitmap_lba
     **************************************************/
    memset(buf, 0, buf_size);
    buf[0] |= 0x1;  // 第 0 个 inode 分给根目录

    /**
     * 由于 inode_table 中共 4096 个 inode, inode_bitmap 正好占用 1 扇区,
     * 即 inode_bitmap_sects 等于 1, 所以位图中的位全都代表 inode_table 中的 inode,
     * 无须再像 block_bitmap 那样单独处理最后一扇区的剩余部分,
     * inode_bitmap 所在的扇区中没有多余的无效位.
     */
    ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects);


    /***********************************************
     * 4 初始化 inode_table 并写入sb.inode_table_lba
     ***********************************************/
    // 准备写 inode_table 中的第 0 项, 即根目录所在的 inode
    memset(buf, 0, buf_size);
    inode *i = (inode *)buf;
    i->i_size = sb.dir_entry_size * 2;      // . 和 ..
    i->i_no = 0;    // 根目录占 inode_table 中第 0 个 inode
    i->i_sectors[0] = sb.data_start_lba;
    ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects);


    /***************************************
     * 5 初始化根目录并写入 sb.data_start_lba
     ***************************************/
    // 写入根目录的两个目录项 . 和 ..
    memset(buf, 0, buf_size);
    dir_entry *p_de = (dir_entry *)buf;

    // 初始化当前目录 "."
    memcpy(p_de->filename, ".", 1);
    p_de->i_no = 0;
    p_de->f_type = FT_DIRECTORY;
    p_de++;

    // 初始化当前目录父目录 ".."
    memcpy(p_de->filename, "..", 2);
    p_de->i_no = 0; // 根目录的父目录依然是根目录自己
    p_de->f_type = FT_DIRECTORY;

    // sb.data_start_lba 已经分配给了根目录, 里面是根目录的目录项
    ide_write(hd, sb.data_start_lba, buf, 1);

    printk("    root_dir_lba: 0x%x\n", sb.data_start_lba);
    printk("%s format done\n", part->name);
    sys_free(buf);
}


// 将最上层路径名称解析出来
char* path_parse(char* pathname, char* name_store) {
    if (pathname[0] == '/') {
        while(*(++pathname) == '/');
    }

    while (*pathname != '/' && *pathname != 0) {
        *name_store++ = *pathname++;
    }

    if (pathname[0] == 0) {   // 若路径字符串为空则返回 NULL
        return NULL;
    }
    return pathname;
}


// 返回路径深度
int32_t path_depth_cnt(char* pathname) {
    ASSERT(pathname != NULL);
    char* p = pathname;
    char name[MAX_FILE_NAME_LEN];
    uint32_t depth = 0;

    p = path_parse(p, name);
    while (name[0]) {
        depth++;
        memset(name, 0, MAX_FILE_NAME_LEN);
        if (p) {    // 如果 p != NULL, 继续分析路径
            p  = path_parse(p, name);
        }
    }
    return depth;
}


// 搜索文件 pathname, 若找到则返回其 inode 号, 否则返回 -1
static int search_file(const char* pathname, path_search_record* searched_record) {
    if (!strcmp(pathname, "/") || !strcmp(pathname, "/.") || !strcmp(pathname, "/..")) {
        searched_record->parent_dir = &root_dir;
        searched_record->file_type = FT_DIRECTORY;
        searched_record->searched_path[0] = 0;
        return 0;
    }

#ifndef NDEBUG
    uint32_t path_len = strlen(pathname);
#endif

    ASSERT(pathname[0] == '/' && path_len > 1 && path_len < MAX_PATH_LEN);

    char* sub_path = (char*)pathname;
    dir* parent_dir = &root_dir;
    dir_entry dir_e;

    char name[MAX_FILE_NAME_LEN] = {0};

    searched_record->parent_dir = parent_dir;
    searched_record->file_type = FT_UNKNOWN;
    uint32_t parent_inode_no = 0;

    sub_path = path_parse(sub_path, name);
    while (name[0]) {
        ASSERT(strlen(searched_record->searched_path) < 512);

        strcat(searched_record->searched_path, "/");
        strcat(searched_record->searched_path, name);

        if (search_dir_entry(cur_part, parent_dir, name, &dir_e)) {
            memset(name, 0, MAX_FILE_NAME_LEN);
            // 若 sub_path != NULL, 也就是未结束时继续拆分路径
            if (sub_path) {
                sub_path = path_parse(sub_path, name);
            }

            if (FT_DIRECTORY == dir_e.f_type) {     // 如果被打开的是目录
                parent_inode_no = parent_dir->inode->i_no;
                dir_close(parent_dir);
                parent_dir = dir_open(cur_part, dir_e.i_no);    // 更新父目录
                searched_record->parent_dir = parent_dir;
                continue;
            }
            else if (FT_REGULAR == dir_e.f_type) {  // 若是普通文件
                searched_record->file_type = FT_REGULAR;
                return dir_e.i_no;
            }
        }
        else {
            /**
             * 找不到目录项时, 要留着 parent_dir 不要关闭,
             * 若是创建新文件的话需要在 parent_dir 中创建
             */
            return -1;
        }
    }

    // 执行到此, 必然是遍历了完整路径并且查找的文件或目录只有同名目录存在
    dir_close(searched_record->parent_dir);

    // 保存被查找目录的直接父目录
    searched_record->parent_dir = dir_open(cur_part, parent_inode_no);
    searched_record->file_type = FT_DIRECTORY;
    return dir_e.i_no;
}


// 打开或创建文件成功后, 返回文件描述符, 否则返回 -1
int32_t sys_open(const char* pathname, uint8_t flags) {
    if (pathname[strlen(pathname) - 1] == '/') {
        printk("Can't open a directory %s\n",pathname);
        return -1;
    }

    ASSERT(flags <= 7);
    int32_t fd = -1;

    path_search_record searched_record;
    memset(&searched_record, 0, sizeof(path_search_record));

    uint32_t pathname_depth = path_depth_cnt((char*)pathname);

    int inode_no = search_file(pathname, &searched_record);
    bool found = inode_no != -1 ? true : false;

    if (searched_record.file_type == FT_DIRECTORY) {
        printk("Can't open a direcotry with open(), use opendir() to instead\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }

    uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);

    // 说明并没有访问到全部的路径, 某个中间目录是不存在的
    if (pathname_depth != path_searched_depth) {
        printk("cannot access %s: Not a directory, subpath %s is't exist\n",
               pathname, searched_record.searched_path);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    // 若是在最后一个路径上没找到, 并且并不是要创建文件, 直接返回 -1
    if (!found && !(flags & O_CREAT)) {
        printk("in path %s, file %s is't exist\n",
               searched_record.searched_path,
               (strrchr(searched_record.searched_path, '/') + 1));
        dir_close(searched_record.parent_dir);
        return -1;
    }
    // 若要创建的文件已存在
    else if (found && flags & O_CREAT) {
        printk("%s has already exist!\n", pathname);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    switch (flags & O_CREAT) {
    case O_CREAT:
        printk("creating file\n");
        fd = file_create(searched_record.parent_dir, (strrchr(pathname, '/') + 1), flags);
        dir_close(searched_record.parent_dir);
        break;

    default:    // O_RDONLY, O_WRONLY, O_RDWR
        fd = file_open(inode_no, flags);
    }
    /**
     *  此 fd 是 pcb->fd_table 数组中的元素下标,
     *  并不是指全局 file_table 中的下标
     */
    return fd;
}


// 将文件描述符转化为文件表的下标
uint32_t fd_local2global(uint32_t local_fd) {
    task_struct *cur = running_thread();
    int32_t global_fd = cur->fd_table[local_fd];
    ASSERT(global_fd >= 0 && global_fd < MAX_FILE_OPEN);
    return (uint32_t)global_fd;
}


int32_t sys_close(int32_t fd) {
    int32_t ret = -1;
    if (fd > 2) {
        uint32_t _fd = fd_local2global(fd);

        if (is_pipe(fd)) {
            if (--file_table[_fd].fd_pos == 0) {
                mfree_page(PF_KERNEL, file_table[_fd].fd_inode, 1);
                file_table[_fd].fd_inode = NULL;
            }
        }
        else {
            ret = file_close(&file_table[_fd]);
        }

        task_struct *cur = running_thread();
        cur->fd_table[fd] = -1;
    }
    return ret;
}


int32_t sys_write(int32_t fd, const void* buf, uint32_t count) {
    if (fd < 0) {
        printk("sys_write: fd error\n");
        return -1;
    }
    if (fd == stdout_no) {
        char tmp_buf[1024] = {0};
        memcpy(tmp_buf, buf, count);
        console_put_str(tmp_buf);
        return count;
    }
    else if (is_pipe(fd)) {
        return pipe_write(fd, buf, count);
    }

    uint32_t _fd = fd_local2global(fd);
    file* wr_file = &file_table[_fd];
    if (wr_file->fd_flag & O_WRONLY || wr_file->fd_flag & O_RDWR) {
        uint32_t bytes_written  = file_write(wr_file, buf, count);
        return bytes_written;
    }
    else {
        console_put_str("sys_write: not allowed to write file without flag O_RDWR or O_WRONLY\n");
        return -1;
    }
}


int32_t sys_read(int32_t fd, void *buf, uint32_t count) {
    ASSERT(buf != NULL);
    int32_t ret = -1;
    if (fd < 0  || fd == stdout_no || fd == stderr_no) {
        printk("sys_read: fd error\n");
        return -1;
    }
    else if (fd == stdin_no) {
        // 标准输入有可能被重定向为管道缓冲区
        if (is_pipe(fd)) {
            ret = pipe_read(fd, buf, count);
        }
        else {
            char *buffer = (char *)buf;
            uint32_t bytes_read = 0;
            while (bytes_read < count) {
                *buffer = ioq_getchar(&kbd_buf);
                bytes_read++;
                buffer++;
            }
            ret = (bytes_read == 0 ? -1 : (int32_t)bytes_read);
        }
    }
    else if (is_pipe(fd)) {
        ret = pipe_read(fd, buf, count);
    }
    else {
        uint32_t _fd = fd_local2global(fd);
        ret = file_read(&file_table[_fd], buf, count);
    }
    return ret;
}


int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence) {
    if (fd < 0) {
        printk("sys_lseek: fd error\n");
        return -1;
    }

    ASSERT(whence > 0 && whence < 4);
    uint32_t _fd = fd_local2global(fd);
    file *pf = &file_table[_fd];
    int32_t new_pos = 0;
    int32_t file_size = (int32_t)pf->fd_inode->i_size;

    switch (whence) {
    case SEEK_SET:
        new_pos = offset;
        break;
    case SEEK_CUR:
        new_pos = (int32_t)pf->fd_pos + offset;
        break;
    case SEEK_END:
        new_pos = file_size + offset;
    default:
        break;
    }

    if (new_pos < 0 || new_pos > (file_size - 1)) {
        return -1;
    }
    pf->fd_pos = new_pos;
    return pf->fd_pos;
}


int32_t sys_unlink(const char* pathname) {
    ASSERT(strlen(pathname) < MAX_PATH_LEN);

    // 先检查待删除的文件是否存在
    path_search_record searched_record;
    memset(&searched_record, 0, sizeof(path_search_record));
    int inode_no = search_file(pathname, &searched_record);

    ASSERT(inode_no != 0);
    if (inode_no == -1) {
        printk("file %s not found!\n", pathname);
        dir_close(searched_record.parent_dir);
        return -1;
    }
    if (searched_record.file_type == FT_DIRECTORY) {
        printk("can't delete a direcotry with unlink(), use rmdir() to instead\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }

    // 检查是否在已打开文件列表(文件表)中
    uint32_t file_idx = 0;
    while (file_idx < MAX_FILE_OPEN) {
        if (file_table[file_idx].fd_inode != NULL && (uint32_t)inode_no == file_table[file_idx].fd_inode->i_no) {
            break;
        }
        file_idx++;
    }
    if (file_idx < MAX_FILE_OPEN) {
        dir_close(searched_record.parent_dir);
        printk("file %s is in use, not allow to delete!\n", pathname);
        return -1;
    }
    ASSERT(file_idx == MAX_FILE_OPEN);

    // 为 delete_dir_entry 申请缓冲区
    void* io_buf = sys_malloc(SECTOR_SIZE + SECTOR_SIZE);
    if (io_buf == NULL) {
        dir_close(searched_record.parent_dir);
        printk("sys_unlink: malloc for io_buf failed\n");
        return -1;
    }

    dir* parent_dir = searched_record.parent_dir;
    delete_dir_entry(cur_part, parent_dir, inode_no, io_buf);
    inode_release(cur_part, inode_no);
    sys_free(io_buf);
    dir_close(searched_record.parent_dir);

    return 0;
}


int32_t sys_mkdir(const char *pathname) {
    uint8_t rollback_step = 0;
    void *io_buf = sys_malloc(SECTOR_SIZE * 2);
    if (io_buf == NULL) {
        printk("sys_mkdir: sys_malloc for io_buf failed\n");
        return -1;
    }

    path_search_record searched_record;
    memset(&searched_record, 0, sizeof(path_search_record));
    int inode_no = -1;
    inode_no = search_file(pathname, &searched_record);
    if (inode_no != -1) {
        printk("sys_mkdir: file or directory %s exist!\n", pathname);
        rollback_step = 1;
        goto rollback;
    }
    else {
        uint32_t pathname_depth = path_depth_cnt((char*)pathname);
        uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);

        // 先判断是否把 pathname 的各层目录都访问到了, 即是否在某个中间目录就失败了
        if (pathname_depth != path_searched_depth) {    // 说明并没有访问到全部的路径, 某个中间目录是不存在的
            printk("sys_mkdir: can't access %s, subpath %s is not exist\n", pathname, searched_record.searched_path);
            rollback_step = 1;
            goto rollback;
        }
    }

    dir *parent_dir = searched_record.parent_dir;
    char *dirname = strrchr(searched_record.searched_path, '/') + 1;

    inode_no = inode_bitmap_alloc(cur_part);
    if (inode_no == -1) {
        printk("sys_mkdir: allocate inode failed\n");
        rollback_step = 1;
        goto rollback;
    }

    inode new_dir_inode;
    inode_init(inode_no, &new_dir_inode);

    int32_t block_lba = -1;
    uint32_t block_bitmap_idx = 0;
    block_lba = block_bitmap_alloc(cur_part);
    if (block_lba == -1) {
        printk("sys_mkdir: block_bitmap_alloc for create directory failed\n");
        rollback_step = 2;
        goto rollback;
    }

    new_dir_inode.i_sectors[0] = block_lba;
    block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
    ASSERT(block_bitmap_idx != 0);
    bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

    // 将当前目录的目录项 '.' 和 '..' 写入目录
    memset(io_buf, 0, SECTOR_SIZE * 2);
    dir_entry *p_de = (dir_entry *)io_buf;

    memcpy(p_de->filename, ".", 1);
    p_de->i_no = inode_no ;
    p_de->f_type = FT_DIRECTORY;
    p_de++;
    memcpy(p_de->filename, "..", 2);
    p_de->i_no = parent_dir->inode->i_no;
    p_de->f_type = FT_DIRECTORY;
    ide_write(cur_part->my_disk, new_dir_inode.i_sectors[0], io_buf, 1);

    new_dir_inode.i_size = 2 * cur_part->sb->dir_entry_size;

    // 在父目录中添加自己的目录项
    dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(dir_entry));
    create_dir_entry(dirname, inode_no, FT_DIRECTORY, &new_dir_entry);
    memset(io_buf, 0, SECTOR_SIZE * 2);
    if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {
        printk("sys_mkdir: sync_dir_entry to disk failed!\n");
        rollback_step = 2;
        goto rollback;
    }

    // 父目录的 inode 同步到硬盘
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(cur_part, parent_dir->inode, io_buf);

    // 将新创建目录的 inode 同步到硬盘
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(cur_part, &new_dir_inode, io_buf);

    // 将 inode 位图同步到硬盘
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);

    sys_free(io_buf);

    // 关闭所创建目录的父目录
    dir_close(searched_record.parent_dir);

    return 0;

rollback:
    switch (rollback_step) {
    case 2:
        bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
        __attribute__((fallthrough));
    case 1:
        dir_close(searched_record.parent_dir);
        break;
    }
    sys_free(io_buf);
    return -1;
}


dir *sys_opendir(const char *name) {
    ASSERT(strlen(name) < MAX_PATH_LEN);
    if (name[0] == '/' && (name[1] == 0 || name[0] == '.')) {
        return &root_dir;
    }

    path_search_record searched_record;
    memset(&searched_record, 0, sizeof(path_search_record));
    int inode_no = search_file(name, &searched_record);
    dir *ret = NULL;
    if (inode_no == -1) {
        printk("In %s, sub path %s not exist\n", name, searched_record.searched_path);
    }
    else {
        if (searched_record.file_type == FT_REGULAR) {
            printk("%s is regular file!\n", name);
        }
        else if (searched_record.file_type == FT_DIRECTORY) {
            ret = dir_open(cur_part, inode_no);
        }
    }
    dir_close(searched_record.parent_dir);
    return ret;
}


int32_t sys_closedir(dir *dir_ptr) {
    int32_t ret = -1;
    if (dir_ptr != NULL) {
        dir_close(dir_ptr);
        ret = 0;
    }
    return ret;
}


dir_entry *sys_readdir(dir *dir_ptr) {
    ASSERT(dir_ptr != NULL);
    return dir_read(dir_ptr);
}


void sys_rewinddir(dir *dir_ptr) {
    dir_ptr->dir_pos = 0;
}


int32_t sys_rmdir(const char* pathname) {
    path_search_record searched_record;
    memset(&searched_record, 0, sizeof(path_search_record));

    int inode_no = search_file(pathname, &searched_record);
    ASSERT(inode_no != 0);

    int retval = -1;
    if (inode_no == -1) {
        printk("In %s, sub path %s not exist\n", pathname, searched_record.searched_path);
    }
    else {
        if (searched_record.file_type == FT_REGULAR) {
            printk("%s is regular file!\n", pathname);
        }
        else {
            dir *dir = dir_open(cur_part, inode_no);
            list_elem *cwd_tag = list_traversal(&cur_part->cwd_dirs, find_cwd_dir, inode_no);
            if (cwd_tag != NULL) {
                printk("dir %s is occupied!\n", pathname);
            }
            else if (!dir_is_empty(dir)) {
                printk("dir %s is not empty!\n", pathname);
            }
            else {
                if (!dir_remove(searched_record.parent_dir, dir)) {
                    retval = 0;
                }
            }
            dir_close(dir);
        }
    }
    dir_close(searched_record.parent_dir);
    return retval;
}


static uint32_t get_parent_dir_inode_nr(uint32_t child_inode_nr, void *io_buf) {
    inode *child_dir_inode = inode_open(cur_part, child_inode_nr);
    uint32_t block_lba = child_dir_inode->i_sectors[0];
    ASSERT(block_lba >= cur_part->sb->data_start_lba);
    inode_close(child_dir_inode);
    ide_read(cur_part->my_disk, block_lba, io_buf, 1);
    dir_entry *dir_e = (dir_entry *)io_buf;

    // 第 0 个目录项是 ".", 第 1 个目录项是 ".."
    ASSERT(dir_e[1].i_no < 4096 && dir_e[1].f_type == FT_DIRECTORY);
    return dir_e[1].i_no;
}


static int get_child_dir_name(uint32_t p_inode_nr, uint32_t c_inode_nr, char *path, void *io_buf) {
    inode *parent_dir_inode = inode_open(cur_part, p_inode_nr);

    uint8_t block_idx = 0;
    uint32_t all_blocks[140] = {0}, block_cnt = 12;
    for (block_idx = 0; block_idx < 12; block_idx++) {
        all_blocks[block_idx] = parent_dir_inode->i_sectors[block_idx];
    }
    if (parent_dir_inode->i_sectors[12]) {
        ide_read(cur_part->my_disk, parent_dir_inode->i_sectors[12], all_blocks + 12, 1);
        block_cnt = 140;
    }
    inode_close(parent_dir_inode);

    dir_entry *dir_e = (dir_entry *)io_buf;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
    uint32_t dir_entry_per_sec = (512 / dir_entry_size);

    for (block_idx = 0; block_idx < block_cnt; ++block_idx) {
        if (all_blocks[block_idx]) {
            ide_read(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);

            for (uint8_t dir_e_idx = 0; dir_e_idx < dir_entry_per_sec; dir_e_idx++) {
                dir_entry *cur = dir_e + dir_e_idx;
                if (cur->i_no == c_inode_nr) {
                    strcat(path, "/");
                    strcat(path, cur->filename);
                    return 0;
                }
            }
        }
    }
    return -1;
}


char *sys_getcwd(char *buf, uint32_t size) {
    ASSERT(buf != NULL);
    void *io_buf = sys_malloc(SECTOR_SIZE);
    if (io_buf == NULL) {
        return NULL;
    }

    task_struct *cur_thread = running_thread();
    int32_t parent_inode_nr = 0;
    int32_t child_inode_nr = cur_thread->cwd_inode_nr;
    ASSERT(child_inode_nr >= 0 &&child_inode_nr < 4096);

    if (child_inode_nr == 0) {
        buf[0] = '/';
        buf[1] = 0;
        sys_free(io_buf);
        return buf;
    }

    memset(buf, 0, size);
    char full_path_reverse[MAX_PATH_LEN] = {0};

    while (child_inode_nr) {
        parent_inode_nr = get_parent_dir_inode_nr(child_inode_nr, io_buf);
        if (get_child_dir_name(parent_inode_nr, child_inode_nr, full_path_reverse, io_buf) == -1) {
            sys_free(io_buf);
            return NULL;
        }
        child_inode_nr = parent_inode_nr;
    }
    ASSERT(strlen(full_path_reverse) <= size);

    char *last_slash;
    while ((last_slash = strrchr(full_path_reverse, '/'))) {
        uint16_t len = strlen(buf);
        strcpy(buf + len, last_slash);
        *last_slash = 0;
    }
    sys_free(io_buf);
    return buf;
}


int32_t sys_chdir(const char *path) {
    int32_t ret = -1;
    path_search_record searched_record;
    memset(&searched_record, 0, sizeof(path_search_record));
    int inode_no = search_file(path, &searched_record);
    if (inode_no != -1) {
        if (searched_record.file_type == FT_DIRECTORY) {
            task_struct *cur = running_thread();
            uint32_t prev_inode = cur->cwd_inode_nr;
            cur->cwd_inode_nr = inode_no;
            ret = 0;

            if (prev_inode != 0) {
                list_elem *prev_cwd_tag = list_traversal(&cur_part->cwd_dirs, find_cwd_dir, prev_inode);
                if (prev_cwd_tag != NULL) {
                    list_remove(prev_cwd_tag);
                    cwd_dir *prev_cwd = elem2entry(cwd_dir, cwd_tag, prev_cwd_tag);
                    sys_free(prev_cwd);
                }
            }
            cwd_dir *new_cwd = (cwd_dir *)sys_malloc(sizeof(cwd_dir));
            if (new_cwd == NULL) {
                PANIC("alloc memory failed!");
            }

            new_cwd->inode_no = inode_no;
            list_push(&cur_part->cwd_dirs, &new_cwd->cwd_tag);
        }
        else {
            printk("sys_chdir: %s is regular file or other!\n", path);
        }
    }
    dir_close(searched_record.parent_dir);
    return ret;
}


void add_cwd(uint32_t inode_no) {
    if (inode_no <= 0) {
        return;
    }

    cwd_dir *new_cwd = (cwd_dir *)sys_malloc(sizeof(cwd_dir));
    if (new_cwd == NULL) {
        PANIC("alloc memory failed!");
    }

    new_cwd->inode_no = inode_no;
    list_push(&cur_part->cwd_dirs, &new_cwd->cwd_tag);
}


int32_t sys_stat(const char *path, stat *buf) {
    if (!strcmp(path, "/") || !strcmp(path, "/.") || !strcmp(path, "/..")) {
        buf->st_filetype = FT_DIRECTORY;
        buf->st_ino = 0;
        buf->st_size = root_dir.inode->i_size;
        return 0;
    }
    int32_t ret = -1;
    path_search_record searched_record;
    memset(&searched_record, 0, sizeof(path_search_record));
    int inode_no = search_file(path, &searched_record);
    if (inode_no != -1) {
        inode* obj_inode = inode_open(cur_part, inode_no);
        buf->st_size = obj_inode->i_size;
        inode_close(obj_inode);
        buf->st_filetype = searched_record.file_type;
        buf->st_ino = inode_no;
        ret = 0;
    }
    else {
        printk("sys_stat: %s not found\n", path);
    }
    dir_close(searched_record.parent_dir);
    return ret;
}


void fs_init() {
    uint8_t channel_no = 0, dev_no, part_idx = 0;

    // sb_buf 用来存储从硬盘上读入的超级块
    super_block* sb_buf = (super_block*)sys_malloc(SECTOR_SIZE);
    if (sb_buf == NULL) {
        PANIC("alloc memory failed!");
    }

    printk("\nsearching filesystem...\n");
    while (channel_no < channel_cnt) {
        dev_no = 0;
        while(dev_no < 2) {
            if (dev_no == 0) {  // 跨过裸盘 hd60M.img
                dev_no++;
                continue;
            }
            disk* hd = &channels[channel_no].devices[dev_no];
            partition* part = hd->prim_parts;
            while(part_idx < 12) {      // 4 个主分区和 8 个逻辑
                if (part_idx == 4) {    // 开始处理逻辑分区
                    part = hd->logic_parts;
                }

                if (part->sec_cnt != 0) {   // 如果分区存在
                    memset(sb_buf, 0, SECTOR_SIZE);

                    ide_read(hd, part->start_lba + 1, sb_buf, 1); // 读出分区的超级块

                    if (sb_buf->magic == 0x19780506) {
                        printk("%s has filesystem\n", part->name);
                    }
                    else {  // 其它文件系统不支持,一律按无文件系统处理
                        printk("formatting %s's partition %s......\n", hd->name, part->name);
                        partition_format(part);
                    }
                }
                part_idx++;
                part++; // 下一分区
            }
            dev_no++;   // 下一磁盘
        }
        channel_no++;   // 下一通道
    }
    sys_free(sb_buf);

    char default_part[8] = "sdb1";  // 确定默认操作的分区
    list_traversal(&partition_list, mount_partition, (int)default_part);    // 挂载分区
    open_root_dir(cur_part);

    uint32_t fd_idx = 0;
    while (fd_idx < MAX_FILE_OPEN) {
        file_table[fd_idx++].fd_inode = NULL;
    }
}
