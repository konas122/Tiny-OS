#include "fs.h"
#include "file.h"
#include "debug.h"
#include "inode.h"
#include "global.h"
#include "stdint.h"
#include "memory.h"
#include "string.h"
#include "interrupt.h"
#include "super_block.h"
#include "stdio_kernel.h"

#include "dir.h"


dir root_dir;


void open_root_dir(partition *part) {
    root_dir.inode = inode_open(part, part->sb->root_inode_no);
    root_dir.dir_pos = 0;
}


dir *dir_open(partition *part, uint32_t inode_no) {
    dir *pdir = (dir *)sys_malloc(sizeof(dir));
    pdir->inode = inode_open(part, inode_no);
    pdir->dir_pos = 0;
    return pdir;
}


bool search_dir_entry(partition *part, dir *pdir, const char *name, dir_entry *dir_e) {
    uint32_t block_cnt = 140;   // 12 + 128 = 140

    uint32_t* all_blocks = (uint32_t*)sys_malloc(48 + 512);
    if (all_blocks == NULL) {
        printk("search_dir_entry: sys_malloc for all_blocks failed");
        return false;
    }

    uint32_t block_idx = 0;
    while (block_idx < 12) {
        all_blocks[block_idx] = pdir->inode->i_sectors[block_idx];
        block_idx++;
    }
    block_idx = 0;
    if (pdir->inode->i_sectors[12] != 0) {  // 若含有一级间接块表
        ide_read(part->my_disk, pdir->inode->i_sectors[12], all_blocks + 12, 1);
    }

    uint8_t *buf = (uint8_t *)sys_malloc(SECTOR_SIZE);
    dir_entry *p_de = (dir_entry *)buf;
    uint32_t dir_entry_size = part->sb->dir_entry_size;
    uint32_t dir_entry_cnt = SECTOR_SIZE / dir_entry_size;  // 1 扇区内可容纳的目录项个数

    while (block_idx < block_cnt) {
        if (all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        ide_read(part->my_disk, all_blocks[block_idx], buf, 1);

        uint32_t dir_entry_idx = 0;

        while (dir_entry_idx < dir_entry_cnt) {
            if (!strcmp(p_de->filename, name)) {
                memcpy(dir_e, p_de, dir_entry_size);
                sys_free(buf);
                sys_free(all_blocks);
                return true;
            }
            dir_entry_idx++;
            p_de++;
        }
        block_idx++;
        p_de = (dir_entry*)buf; // Next we will read a new sector
        memset(buf, 0, SECTOR_SIZE);
    }
    sys_free(buf);
    sys_free(all_blocks);
    return false;
}


void dir_close(dir* dir_ptr) {
    if (dir_ptr == &root_dir) {
        return;
    }
    inode_close(dir_ptr->inode);
    sys_free(dir_ptr);
}


void create_dir_entry(char* filename, uint32_t inode_no, uint8_t file_type, dir_entry* p_de) {
    ASSERT(strlen(filename) <=  MAX_FILE_NAME_LEN);

    memcpy(p_de->filename, filename, strlen(filename));
    p_de->i_no = inode_no;
    p_de->f_type = (file_types)file_type;
}


bool sync_dir_entry(dir *parent_dir, dir_entry *p_de, void *io_buf) {
    inode* dir_inode = parent_dir->inode;
    uint32_t dir_size = dir_inode->i_size;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;

    ASSERT((dir_size % dir_entry_size) == 0);

    uint32_t dir_entrys_per_sec = (512 / dir_entry_size);
    int32_t block_lba = -1;

    uint8_t block_idx = 0;
    uint32_t all_blocks[140] = {0};

    while (block_idx < 12) {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }

    dir_entry *dir_e = (dir_entry *)io_buf;
    int32_t block_bitmap_idx = -1;

    block_idx = 0;
    uint32_t block_bitmap_idx_end = UINT32_MIN;
    uint32_t block_bitmap_idx_start = UINT32_MAX;

    while (block_idx < 140) {
        block_bitmap_idx = -1;
        if (all_blocks[block_idx] == 0) {
            block_lba = block_bitmap_alloc(cur_part);
            if (block_lba == -1) {
                printk("alloc block bitmap for sync_dir_entry failed\n");
                return false;
            }

            block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
            ASSERT(block_bitmap_idx != -1);

            block_bitmap_idx_end = max(block_bitmap_idx_end, (uint32_t)block_bitmap_idx);
            block_bitmap_idx_start = min(block_bitmap_idx_start, (uint32_t)block_bitmap_idx);

            block_bitmap_idx = -1;
            if (block_idx < 12) {       // 直接块
                dir_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
            }
            else if (block_idx == 12) { // 尚未分配一级间接块表
                dir_inode->i_sectors[12] = block_lba;       // 将上面分配的块做为一级间接块表地址
                block_lba = -1;
                block_lba = block_bitmap_alloc(cur_part);   // 再分配一个块做为第 0 个间接块
                if (block_lba == -1) {
                    block_bitmap_idx = dir_inode->i_sectors[12] - cur_part->sb->data_start_lba;
                    bitmap_set(&cur_part->block_bitmap, block_bitmap_idx, 0);
                    dir_inode->i_sectors[12] = 0;
                    printk("alloc block bitmap for sync_dir_entry failed\n");
                    return false;
                }
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                ASSERT(block_bitmap_idx != -1);

                block_bitmap_idx_end = max(block_bitmap_idx_end, (uint32_t)block_bitmap_idx);
                block_bitmap_idx_start = min(block_bitmap_idx_start, (uint32_t)block_bitmap_idx);

                all_blocks[12] = block_lba;
                // 把新分配的第 0 个间接块地址写入一级间接块表
                ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
            }
            else {                  // 间接块未分配
                all_blocks[block_idx] = block_lba;
                // 把新分配的第 (block_idx-12) 个间接块地址写入一级间接块表
                ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
            }
            bitmap_sync_range(cur_part, block_bitmap_idx_start, block_bitmap_idx_end, BLOCK_BITMAP);

            // 将新目录项 p_de 写入新分配的间接块
            memset(io_buf, 0, 512);
            memcpy(io_buf, p_de, dir_entry_size);
            ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
            dir_inode->i_size += dir_entry_size;
            return true;
        }

        // 若第 block_idx 块已存在, 将其读进内存, 然后在该块中查找空目录项
        ide_read(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);

        uint8_t dir_entry_idx = 0;
        while (dir_entry_idx < dir_entrys_per_sec) {
            // FT_UNKNOWN 为 0, 无论是初始化或是删除文件后, 都会将 f_type 置为 FT_UNKNOWN
            if ((dir_e + dir_entry_idx)->f_type == FT_UNKNOWN) {
                memcpy(dir_e + dir_entry_idx, p_de, dir_entry_size);
                ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);

                dir_inode->i_size += dir_entry_size;
                return true;
            }
            dir_entry_idx++;
        }
        block_idx++;
    }
    printk("directory is full!\n");
    return false;
}


bool delete_dir_entry(partition *part, dir *pdir, uint32_t inode_no, void *io_buf) {
    inode *dir_inode = pdir->inode;
    uint32_t block_idx = 0, all_blocks[140] = {0};

    while (block_idx < 12) {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }
    if (dir_inode->i_sectors[12]) {
        ide_read(part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
    }

    uint32_t dir_entry_size = part->sb->dir_entry_size;
    uint32_t dir_entry_per_sec = (SECTOR_SIZE / dir_entry_size);

    dir_entry *dir_e = (dir_entry *)io_buf;
    dir_entry *dir_entry_found = NULL;

    uint8_t dir_entry_idx, dir_entry_cnt;
    bool is_dir_first_block = false;

    block_idx = 0;
    while (block_idx < 140) {
        is_dir_first_block = false;
        if (all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        dir_entry_idx = dir_entry_cnt = 0;
        memset(io_buf, 0, SECTOR_SIZE);
        ide_read(part->my_disk, all_blocks[block_idx], io_buf, 1);

        while (dir_entry_idx < dir_entry_per_sec) {
            dir_entry *cur_entry = dir_e + dir_entry_idx;
            if (cur_entry->f_type != FT_UNKNOWN) {
                if (!strcmp(cur_entry->filename, ".")) {
                    is_dir_first_block = true;
                }
                else if (strcmp(cur_entry->filename, ".") && strcmp(cur_entry->filename, "..")) {
                    dir_entry_cnt++;
                    if (cur_entry->i_no == inode_no) {
                        ASSERT(dir_entry_found == NULL);
                        dir_entry_found = cur_entry;
                    }
                }
            }
            dir_entry_idx++;
        }

        if (dir_entry_found == NULL) {
            block_idx++;
            continue;
        }

        uint32_t block_bitmap_idx_end = UINT32_MIN;
        uint32_t block_bitmap_idx_start = UINT32_MAX;

        // 在此扇区中找到目录项后, 清除该目录项并判断是否回收扇区, 随后退出循环直接返回
        ASSERT(dir_entry_cnt >= 1);
        if (dir_entry_cnt == 1 && !is_dir_first_block) {
            // 在 block_bitmap 中回收该块
            uint32_t block_bitmap_idx = all_blocks[block_idx] - part->sb->data_start_lba;
            bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);

            block_bitmap_idx_end = max(block_bitmap_idx_end, block_bitmap_idx);
            block_bitmap_idx_start = min(block_bitmap_idx_start, block_bitmap_idx);

            if (block_idx < 12) {
                dir_inode->i_sectors[block_idx] = 0;
            }
            else {
                uint32_t indirect_blocks = 0;
                uint32_t indirect_block_idx = 12;
                while ((all_blocks[indirect_block_idx] != 0) && (indirect_block_idx < 140) && (indirect_blocks >= 2)) {
                    indirect_block_idx++;
                    indirect_blocks++;
                }

                ASSERT(indirect_blocks >= 1);

                if (indirect_blocks > 1) {
                    all_blocks[block_idx] = 0;
                    ide_write(part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
                }
                else {  // 回收间接索引表所在的块
                    block_bitmap_idx = dir_inode->i_sectors[12] - part->sb->data_start_lba;
                    bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);

                    block_bitmap_idx_end = max(block_bitmap_idx_end, block_bitmap_idx);
                    block_bitmap_idx_start = min(block_bitmap_idx_start, block_bitmap_idx);

                    // 将间接索引表地址清 0
                    dir_inode->i_sectors[12] = 0;
                }
            }
            bitmap_sync_range(cur_part, block_bitmap_idx_start, block_bitmap_idx_end, BLOCK_BITMAP);
        }
        else {  // 仅将该目录项清空
            memset(dir_entry_found, 0, dir_entry_size);
            ide_write(part->my_disk, all_blocks[block_idx], io_buf, 1);
        }

        // 更新 inode 信息并同步到硬盘
        ASSERT(dir_inode->i_size >= dir_entry_size);
        dir_inode->i_size -= dir_entry_size;
        memset(io_buf, 0, SECTOR_SIZE * 2);
        inode_sync(part, dir_inode, io_buf);

        return true;
    }

    return false;
}


dir_entry *dir_read(dir *dir_ptr) {
    dir_entry *dir_e = (dir_entry *)dir_ptr->dir_buf;
    inode *dir_inode = dir_ptr->inode;

    uint32_t all_blocks[140] = {0};
    uint32_t block_cnt = 12, block_idx = 0, dir_entry_idx = 0;
    while (block_idx < 12) {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }
    if (dir_inode->i_sectors[12] != 0) {
        ide_read(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
        block_cnt = 140;
    }
    block_idx = 0;

    uint32_t cur_dir_entry_pos = 0;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
    uint32_t dir_entry_per_sec = SECTOR_SIZE / dir_entry_size;

    while (block_idx < block_cnt) {
        if (dir_ptr->dir_pos >= dir_inode->i_size) {
            return NULL;
        }
        if (all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        memset(dir_e, 0, SECTOR_SIZE);
        ide_read(cur_part->my_disk, all_blocks[block_idx], dir_e, 1);

        for (dir_entry_idx = 0; dir_entry_idx < dir_entry_per_sec; ++dir_entry_idx) {
            dir_entry *cur = dir_e + dir_entry_idx;
            if (cur->f_type) {  // f_type 不为 FT_UNKNOWN
                if (cur_dir_entry_pos < dir_ptr->dir_pos) {
                    cur_dir_entry_pos += dir_entry_size;
                    continue;
                }
                ASSERT(cur_dir_entry_pos == dir_ptr->dir_pos);
                dir_ptr->dir_pos += dir_entry_size;
                return cur;
            }
        }
        block_idx++;
    }
    return NULL;
}


bool dir_is_empty(dir *dir_ptr) {
    inode *dir_inode = dir_ptr->inode;
    return (dir_inode->i_size == cur_part->sb->dir_entry_size * 2);
}


int32_t dir_remove(dir* parent_dir, dir* child_dir) {
    inode *child_dir_inode = child_dir->inode;

    // 空目录只在 inode->i_sectors[0] 中有扇区, 其它扇区都应该为空
    int32_t block_idx = 1;
    while (block_idx < 13) {
        ASSERT(child_dir_inode->i_sectors[block_idx] == 0);
        block_idx++;
    }

    void *io_buf = sys_malloc(SECTOR_SIZE * 2);
    if (io_buf == NULL) {
        printk("dir_remove: malloc for io_buf failed\n");
        return -1;
    }

    // 在父目录 parent_dir 中删除子目录 child_dir 对应的目录项
    delete_dir_entry(cur_part, parent_dir, child_dir_inode->i_no, io_buf);

    // 回收 inode 中 i_secotrs 中所占用的扇区, 并同步 inode_bitmap 和 block_bitmap
    inode_release(cur_part, child_dir_inode->i_no);
    sys_free(io_buf);
    return 0;
}
