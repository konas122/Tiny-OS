#include "ide.h"
#include "dir.h"
#include "list.h"
#include "file.h"
#include "inode.h"
#include "debug.h"
#include "global.h"
#include "stdint.h"
#include "string.h"
#include "memory.h"
#include "super_block.h"
#include "stdio_kernel.h"


partition *cur_part;


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
    printk("mount %s done!\n", part->name);

    return true;
}

// 格式化分区, 也就是初始化分区的元信息, 创建文件系统
static void partition_format(disk *hd, partition *part) {
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
static char* path_parse(char* pathname, char* name_store) {
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
static int search_file(const char* pathname, struct path_search_record* searched_record) {
    if (!strcmp(pathname, "/") || !strcmp(pathname, "/.") || !strcmp(pathname, "/..")) {
        searched_record->parent_dir = &root_dir;
        searched_record->file_type = FT_DIRECTORY;
        searched_record->searched_path[0] = 0;
        return 0;
    }

    uint32_t path_len = strlen(pathname);

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

        // 在所给的目录中查找文件
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
        else {  //若找不到
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
        printk("in path %s, file %s is`t exist\n",
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
        // 其余为打开文件
    }

    /**
     *  此 fd 是 pcb->fd_table 数组中的元素下标,
     *  并不是指全局 file_table 中的下标
     */
    return fd;
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
                        partition_format(hd, part);
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
