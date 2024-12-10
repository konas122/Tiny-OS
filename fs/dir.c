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


void dir_close(dir* dir) {
    if (dir == &root_dir) {
        return;
    }
    inode_close(dir->inode);
    sys_free(dir);
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
    while (block_idx < 140) {
        block_bitmap_idx = -1;
        if (all_blocks[block_idx] == 0) {
            block_lba = block_bitmap_alloc(cur_part);
            if (block_lba == -1) {
                printk("alloc block bitmap for sync_dir_entry failed\n");
                return false;
            }

            // 每分配一个块就同步一次 block_bitmap
            block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
            ASSERT(block_bitmap_idx != -1);
            bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

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
                // 每分配一个块就同步一次 block_bitmap
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                ASSERT(block_bitmap_idx != -1);
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

                all_blocks[12] = block_lba;
                // 把新分配的第 0 个间接块地址写入一级间接块表
                ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
            }
            else {                  // 间接块未分配
                all_blocks[block_idx] = block_lba;
                // 把新分配的第 (block_idx-12) 个间接块地址写入一级间接块表
                ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
            }

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
