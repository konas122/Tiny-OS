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


void inode_sync(partition *part, inode *inode, void *io_buf) {
    uint8_t inode_no = inode->i_no;
    inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);
    ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));

    struct inode pure_inode;
    memcpy(&pure_inode, inode, sizeof(inode));

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

void inode_close(inode *inode) {
    intr_status old_status = intr_disable();
    if (--inode->i_open_cnts == 0) {
        list_remove(&inode->inode_tag);
        task_struct *cur = running_thread();
        uint32_t *cur_pagedir = cur->pgdir;
        cur->pgdir = NULL;
        sys_free(inode);
        cur->pgdir = cur_pagedir;
    }
    intr_set_status(old_status);
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
