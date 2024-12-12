#include "fs.h"
#include "file.h"
#include "list.h"
#include "debug.h"
#include "global.h"
#include "memory.h"
#include "string.h"
#include "interrupt.h"
#include "super_block.h"
#include "stdio_kernel.h"

#include "inode.h"


typedef struct inode_position {
    bool two_sec;       // inode 是否跨扇区
    uint32_t sec_lba;   // inode 所在的扇区号
    uint32_t off_size;  // inode 在扇区内的字节偏移量
} inode_position;


static void inode_locate(partition *part, uint32_t inode_no, inode_position *inode_pos) {
    ASSERT(inode_no < 4096);

    uint32_t inode_table_lba = part->sb->inode_table_lba;
    uint32_t inode_size = sizeof(inode);
    uint32_t off_size = inode_no * inode_size;
    uint32_t off_sec = off_size / 512;
    uint32_t off_size_in_sec = off_size % 512;

    uint32_t left_in_sec = 512 - off_size_in_sec;
    if (left_in_sec < inode_size) {
        inode_pos->two_sec = true;
    }
    else {
        inode_pos->two_sec = false;
    }
    inode_pos->sec_lba = inode_table_lba + off_sec;
    inode_pos->off_size = off_size_in_sec;
}


void inode_sync(partition *part, inode *inode_ptr, void *io_buf) {
    uint8_t inode_no = inode_ptr->i_no;
    inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);
    ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));

    inode pure_inode;
    memcpy(&pure_inode, inode_ptr, sizeof(inode));

    pure_inode.i_open_cnts = 0;
    pure_inode.write_deny = false;
    pure_inode.inode_tag.prev = pure_inode.inode_tag.next = NULL;

    char *inode_buf = (char *)io_buf;
    if (inode_pos.two_sec) {
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
        memcpy(inode_buf + inode_pos.off_size, &pure_inode, sizeof(inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    }
    else {
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
        memcpy(inode_buf + inode_pos.off_size, &pure_inode, sizeof(inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
}


inode *inode_open(partition *part, uint32_t inode_no) {
    inode *inode_found;
    list_elem *elem = part->open_inodes.head.next;

    while (elem != &part->open_inodes.tail) {
        inode_found = elem2entry(inode, inode_tag, elem);
        if (inode_found->i_no == inode_no) {
            inode_found->i_open_cnts++;
            return inode_found;
        }
        elem = elem->next;
    }

    inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);

    /**
     * 为使通过 sys_malloc 创建的新 inode 被所有任务共享,
     * 需要将 inode 置于内核空间, 故需要临时将 cur->pgdir 置为 NULL,
     * 以保证让 sys_malloc 在内核空间中分配空间
     */
    task_struct *cur = running_thread();
    uint32_t *cur_pagedir = cur->pgdir;
    cur->pgdir = NULL;
    inode_found = (inode *)sys_malloc(sizeof(inode));
    cur->pgdir = cur_pagedir;

    char *inode_buf;
    if (inode_pos.two_sec) {
        inode_buf = (char *)sys_malloc(1024);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    }
    else {
        inode_buf = (char *)sys_malloc(512);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
    memcpy(inode_found, inode_buf + inode_pos.off_size, sizeof(inode));

    list_push(&part->open_inodes, &inode_found->inode_tag);
    inode_found->i_open_cnts = 1;

    sys_free(inode_buf);
    return inode_found;
}


void inode_close(inode *inode_ptr) {
    intr_status old_status = intr_disable();
    if (--inode_ptr->i_open_cnts == 0) {
        list_remove(&inode_ptr->inode_tag);
        task_struct *cur = running_thread();
        uint32_t *cur_pagedir = cur->pgdir;
        cur->pgdir = NULL;
        sys_free(inode_ptr);
        cur->pgdir = cur_pagedir;
    }
    intr_set_status(old_status);
}


void inode_delete(partition *part, uint32_t inode_no, void *io_buf) {
    ASSERT(inode_no < 256);
    inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);
    ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));

    char *inode_buf = (char *)io_buf;
    if (inode_pos.two_sec) {
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
        memset((inode_buf + inode_pos.off_size), 0, sizeof(inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    }
    else {
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
        memset((inode_buf + inode_pos.off_size), 0, sizeof(inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
}


void inode_release(partition *part, uint32_t inode_no) {
    inode *inode_to_del = inode_open(part, inode_no);
    ASSERT(inode_to_del->i_no == inode_no);

    uint8_t block_idx = 0, block_cnt = 12;
    uint32_t block_bitmap_idx;
    uint32_t all_blocks[140] = {0};

    while (block_idx < 12) {
        all_blocks[block_idx] = inode_to_del->i_sectors[block_idx];
        block_idx++;
    }

    if (inode_to_del->i_sectors[12] != 0) {
        ide_read(part->my_disk, inode_to_del->i_sectors[12], all_blocks + 12, 1);
        block_cnt = 140;

        // 回收一级间接快占用的扇区
        block_bitmap_idx = inode_to_del->i_sectors[12] - part->sb->data_start_lba;
        ASSERT(block_bitmap_idx > 0);
        bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
        bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
    }

    block_idx = 0;
    while (block_idx < block_cnt) {
        if (all_blocks[block_idx] != 0) {
            block_bitmap_idx = all_blocks[block_idx] - part->sb->data_start_lba;
            ASSERT(block_bitmap_idx > 0);
            bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
            bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
        }
        block_idx++;
    }

    // 回收该 inode 在 inode_bitmap 中所占用 bit
    bitmap_set(&part->inode_bitmap, inode_no, 0);
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);

    void *io_buf = sys_malloc(1024);
    inode_delete(part, inode_no, io_buf);
    sys_free(io_buf);

    inode_close(inode_to_del);
}


void inode_init(uint32_t inode_no, inode *new_inode) {
    new_inode->i_no = inode_no;
    new_inode->i_size = 0;
    new_inode->i_open_cnts = 0;
    new_inode->write_deny = false;

    uint8_t sec_idx = 0;
    while (sec_idx < 13) {
        new_inode->i_sectors[sec_idx] = 0;
        ++sec_idx;
    }
}
