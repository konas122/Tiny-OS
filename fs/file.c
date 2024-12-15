#include "fs.h"
#include "dir.h"
#include "inode.h"
#include "debug.h"
#include "string.h"
#include "thread.h"
#include "global.h"
#include "memory.h"
#include "interrupt.h"
#include "super_block.h"
#include "stdio_kernel.h"

#include "file.h"

#define DEFAULT_SECS 1


file file_table[MAX_FILE_OPEN];     // 文件表


int32_t get_free_slot_in_global(void) {
    uint32_t fd_idx = 3;
    while (fd_idx < MAX_FILE_OPEN) {
        if (file_table[fd_idx].fd_inode == NULL) {
            break;
        }
        fd_idx++;
    }
    if (fd_idx == MAX_FILE_OPEN) {
        printk("exceed max open files\n");
        return -1;
    }
    return fd_idx;
}


int32_t pcb_fd_install(int32_t global_fd_idx) {
    task_struct *cur = running_thread();
    uint8_t local_fd_idx = 3;
    while (local_fd_idx < MAX_FILES_OPEN_PER_PROC) {
        if (cur->fd_table[local_fd_idx] == -1) {
            cur->fd_table[local_fd_idx] = global_fd_idx;
            break;
        }
        local_fd_idx++;
    }
    if (local_fd_idx == MAX_FILES_OPEN_PER_PROC) {
        printk("exceed max open files_per_proc\n");
        return -1;
    }
    return local_fd_idx;
}


int32_t inode_bitmap_alloc(partition* part) {
    int32_t bit_idx = bitmap_scan(&part->inode_bitmap, 1);
    if (bit_idx == -1) {
        return -1;
    }
    bitmap_set(&part->inode_bitmap, bit_idx, 1);
    return bit_idx;
}


int32_t block_bitmap_alloc(partition* part) {
    int32_t bit_idx = bitmap_scan(&part->block_bitmap, 1);
    if (bit_idx == -1) {
        return -1;
    }
    bitmap_set(&part->block_bitmap, bit_idx, 1);
    return (part->sb->data_start_lba + bit_idx);
}


void bitmap_sync(partition *part, uint32_t bit_idx, uint8_t btmp_type) {
    uint32_t off_sec = bit_idx / 4096;
    uint32_t off_size = off_sec * BLOCK_SIZE;
    uint32_t sec_lba;
    uint8_t *bitmap_off;

    switch (btmp_type) {
    case INODE_BITMAP:
        sec_lba = part->sb->inode_bitmap_lba + off_sec;
        bitmap_off = part->inode_bitmap.bits + off_size;
        break;

    case BLOCK_BITMAP: 
        sec_lba = part->sb->block_bitmap_lba + off_sec;
        bitmap_off = part->block_bitmap.bits + off_size;
        break;
    }
    ide_write(part->my_disk, sec_lba, bitmap_off, 1);
}


void bitmap_sync_range(partition *part, uint32_t bit_idx_start, uint32_t bit_idx_end, uint8_t btmp_type) {
    uint32_t off_sec_start = bit_idx_start / 4096;
    uint32_t off_sec_end = bit_idx_end/ 4096;

    uint32_t off_size = off_sec_start * BLOCK_SIZE;

    uint32_t sec_lba;
    uint8_t *bitmap_off;
    uint32_t sec_cnt = off_sec_end - off_sec_start + 1;

    switch (btmp_type) {
    case INODE_BITMAP:
        sec_lba = part->sb->inode_bitmap_lba + off_sec_start;
        bitmap_off = part->inode_bitmap.bits + off_size;
        break;
    
    case BLOCK_BITMAP:
        sec_lba = part->sb->block_bitmap_lba + off_sec_start;
        bitmap_off = part->block_bitmap.bits + off_size;
        break;
    }
    ide_write(part->my_disk, sec_lba, bitmap_off, sec_cnt);
}


int32_t file_create(dir *parent_dir, char *filename, uint8_t flag) {
    void *io_buf = sys_malloc(1024);
    if (io_buf == NULL) {
        printk("in file_creat: sys_malloc for io_buf failed\n");
        return -1;
    }

    uint8_t rollback_step = 0;  // 用于操作失败时回滚各资源状态

    int32_t inode_no = inode_bitmap_alloc(cur_part);
    if (inode_no == -1) {
        printk("in file_creat: allocate inode failed\n");
        return -1;
    }

    inode *new_file_inode = (inode *)sys_malloc(sizeof(inode));
    if (new_file_inode == NULL) {
        printk("file_create: sys_malloc for inode failded\n");
        rollback_step = 1;
        goto rollback;
    }
    inode_init(inode_no, new_file_inode);

    int fd_idx = get_free_slot_in_global();
    if (fd_idx == -1) {
        printk("exceed max open files\n");
        rollback_step = 2;
        goto rollback;
    }

    file_table[fd_idx].fd_inode = new_file_inode;
    file_table[fd_idx].fd_pos = 0;
    file_table[fd_idx].fd_flag = flag;
    file_table[fd_idx].fd_inode->write_deny = false;

    dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(dir_entry));
    create_dir_entry(filename, inode_no, FT_REGULAR, &new_dir_entry);

    // 同步内存数据到硬盘
    if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {
        printk("sync dir_entry to disk failed\n");
        rollback_step = 3;
        goto rollback;
    }

    memset(io_buf, 0, 1024);
    inode_sync(cur_part, parent_dir->inode, io_buf);

    memset(io_buf, 0, 1024);
    inode_sync(cur_part, new_file_inode, io_buf);

    bitmap_sync(cur_part, inode_no, INODE_BITMAP);

    list_push(&cur_part->open_inodes, &new_file_inode->inode_tag);
    new_file_inode->i_open_cnts = 1;

    sys_free(io_buf);
    return pcb_fd_install(fd_idx);

rollback:
    switch (rollback_step) {
    case 3:
        memset(&file_table[fd_idx], 0, sizeof(file));
        __attribute__((fallthrough));
    case 2:
        sys_free(new_file_inode);
        __attribute__((fallthrough));
    case 1:
        bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
        __attribute__((fallthrough));
    default:
        break;
    }
    sys_free(io_buf);
    return -1;
}


int32_t file_open(uint32_t inode_no, uint8_t flag) {
    int fd_idx = get_free_slot_in_global();
    if (fd_idx == -1) {
        printk("exceed max open files\n");
        return -1;
    }
    file_table[fd_idx].fd_inode = inode_open(cur_part, inode_no);
    file_table[fd_idx].fd_pos = 0;
    file_table[fd_idx].fd_flag = flag;
    bool *write_deny = &file_table[fd_idx].fd_inode->write_deny;

    if (flag & O_WRONLY || flag & O_RDWR) {
        intr_status old_status = intr_disable();
        if (!(*write_deny)) {
            *write_deny = true;     // 避免多个进程同时写此文件
            intr_set_status(old_status);
        }
        else {
            intr_set_status(old_status);
            printk("file can`t be write now, try again later\n");
            return -1;
        }
    }
    return pcb_fd_install(fd_idx);
}


int32_t file_close(file* file) {
    if (file == NULL) {
        return -1;
    }
    file->fd_inode->write_deny = false;
    inode_close(file->fd_inode);
    file->fd_inode = NULL;
    return 0;
}


int32_t file_write(file *file, const void * buf, uint32_t count) {
    if ((file->fd_inode->i_size + count) > (BLOCK_SIZE * 140)) {
        printk("exceed max file_size 71680 bytes, write file failed\n");
        return -1;
    }
    uint8_t *io_buf = sys_malloc(BLOCK_SIZE);
    if (io_buf == NULL) {
        printk("file_write: sys_malloc for io_buf failed\n");
        return -1;
    }
    uint32_t* all_blocks = (uint32_t*)sys_malloc(BLOCK_SIZE + 48);  // 用来记录文件所有的块地址
    if (all_blocks == NULL) {
        printk("file_write: sys_malloc for all_blocks failed\n");
        return -1;
    }

    const uint8_t* src = buf;       // 用 src 指向 buf 中待写入的数据 
    uint32_t bytes_written = 0;     // 用来记录已写入数据大小
    uint32_t size_left = count;     // 用来记录未写入数据大小
    int32_t block_lba = -1;         // 块地址
    uint32_t block_bitmap_idx = 0;  // 用来记录 block 对应于 block_bitmap 中的索引, 做为参数传给 bitmap_sync
    uint32_t sec_idx;               // 用来索引扇区
    uint32_t sec_lba;               // 扇区地址
    uint32_t sec_off_bytes;         // 扇区内字节偏移量
    uint32_t sec_left_bytes;        // 扇区内剩余字节量
    uint32_t chunk_size;            // 每次写入硬盘的数据块大小
    int32_t indirect_block_table;   // 用来获取一级间接表地址
    uint32_t block_idx;             // 块索引

    uint32_t block_bitmap_idx_end = UINT32_MIN;
    uint32_t block_bitmap_idx_start = UINT32_MAX;

    if (file->fd_inode->i_sectors[0] == 0) {    // 文件第一次写
        block_lba = block_bitmap_alloc(cur_part);
        if (block_lba == -1) {
            printk("file_write: block_bitmap_alloc failed\n");
            return -1;
        }
        file->fd_inode->i_sectors[0] = block_lba;
        block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
        ASSERT(block_bitmap_idx != 0);

        block_bitmap_idx_end = max(block_bitmap_idx_end, block_bitmap_idx);
        block_bitmap_idx_start = min(block_bitmap_idx_start, block_bitmap_idx);
    }

    uint32_t file_has_used_blocks = file->fd_inode->i_size / BLOCK_SIZE + 1;
    uint32_t file_will_use_blocks = (file->fd_inode->i_size + count) / BLOCK_SIZE + 1;
    ASSERT(file_will_use_blocks <= 140);
    uint32_t add_blocks = file_will_use_blocks - file_has_used_blocks;

    if (add_blocks == 0) {
        if (file_has_used_blocks <= 12) {
            block_idx = file_has_used_blocks - 1;
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
        }
        else {
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    }
    else {
        if (file_will_use_blocks <= 12) {
            // 先将有剩余空间的可继续用的扇区地址收集到 all_blocks
            block_idx = file_has_used_blocks - 1;
            ASSERT(file->fd_inode->i_sectors[block_idx] != 0);
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];

            block_idx = file_has_used_blocks;
            while (block_idx < file_will_use_blocks) {
                block_lba = block_bitmap_alloc(cur_part);
                if (block_lba == -1) {
                    printk("file_write: block_bitmap_alloc for situation 1 failed\n");
                    return -1;
                }
                ASSERT(file->fd_inode->i_sectors[block_idx] == 0);
                file->fd_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;

                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;

                block_bitmap_idx_end = max(block_bitmap_idx_end, block_bitmap_idx);
                block_bitmap_idx_start = min(block_bitmap_idx_start, block_bitmap_idx);

                block_idx++;
            }
        }
        // 旧数据在 12 个直接块内, 新数据将使用间接块
        else if (file_has_used_blocks <= 12 && file_will_use_blocks > 12) {
            // 先将有剩余空间的可继续用的扇区地址收集到 all_blocks
            block_idx = file_has_used_blocks - 1;   // 指向旧数据所在的最后一个扇区
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];

            // 创建一级间接块表
            block_lba = block_bitmap_alloc(cur_part);
            if (block_lba == -1) {
                printk("file_write: block_bitmap_alloc for situation 2 failed\n");
                return -1;
            }
            ASSERT(file->fd_inode->i_sectors[12] == 0);

            // 分配一级间接块索引表
            indirect_block_table = file->fd_inode->i_sectors[12] = block_lba;

            block_idx = file_has_used_blocks;   // 第一个未使用的块, 即本文件最后一个已经使用的直接块的下一块
            while (block_idx < file_will_use_blocks) {
                block_lba = block_bitmap_alloc(cur_part);
                if (block_lba == -1) {
                    printk("file_write: block_bitmap_alloc for situation 2 failed\n");
                    return -1;
                }

                if (block_idx < 12) {           // 新创建的 0-11 块直接存入 all_blocks 数组
                    ASSERT(file->fd_inode->i_sectors[block_idx] == 0);  // 确保尚未分配扇区地址
                    file->fd_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
                }
                else {  // 间接块只写入到 all_block 数组中, 待全部分配完成后一次性同步到硬盘
                    all_blocks[block_idx] = block_lba;
                }
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;

                block_bitmap_idx_end = max(block_bitmap_idx_end, block_bitmap_idx);
                block_bitmap_idx_start = min(block_bitmap_idx_start, block_bitmap_idx);

                block_idx++;   // 下一个新扇区
            }
            ide_write(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1); // 同步一级间接块表到硬盘
        }
        else if (file_has_used_blocks > 12) {
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            indirect_block_table = file->fd_inode->i_sectors[12];   // 获取一级间接表地址

            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1); // 获取所有间接块地址

            block_idx = file_has_used_blocks;	  // 第一个未使用的间接块, 即已经使用的间接块的下一块
            while (block_idx < file_will_use_blocks) {
                block_lba = block_bitmap_alloc(cur_part);
                if (block_lba == -1) {
                    printk("file_write: block_bitmap_alloc for situation 3 failed\n");
                    return -1;
                }
                all_blocks[block_idx++] = block_lba;

                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;

                block_bitmap_idx_end = max(block_bitmap_idx_end, block_bitmap_idx);
                block_bitmap_idx_start = min(block_bitmap_idx_start, block_bitmap_idx);
            }
            ide_write(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1); // 同步一级间接块表到硬盘
        }
    }

    bitmap_sync_range(cur_part, block_bitmap_idx_start, block_bitmap_idx_end, BLOCK_BITMAP);

    bool first_write_block = true;
    // 块地址已经收集到 all_blocks 中, 下面开始写数据
    file->fd_pos = file->fd_inode->i_size - 1;
    while (bytes_written < count) {
        memset(io_buf, 0, BLOCK_SIZE);
        sec_idx = file->fd_inode->i_size / BLOCK_SIZE;
        sec_lba = all_blocks[sec_idx];
        sec_off_bytes = file->fd_inode->i_size % BLOCK_SIZE;
        sec_left_bytes = BLOCK_SIZE - sec_off_bytes;

        // 判断此次写入硬盘的数据大小
        chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;
        if (first_write_block) {
            ide_read(cur_part->my_disk, sec_lba, io_buf, 1);
            first_write_block = false;
        }
        memcpy(io_buf + sec_off_bytes, src, chunk_size);
        ide_write(cur_part->my_disk, sec_lba, io_buf, 1);
        printk("file write at lba 0x%x\n", sec_lba);    //调试,完成后去掉

        src += chunk_size;
        file->fd_inode->i_size += chunk_size;   // 更新文件大小
        file->fd_pos += chunk_size;
        bytes_written += chunk_size;
        size_left -= chunk_size;
    }
    inode_sync(cur_part, file->fd_inode, io_buf);
    sys_free(all_blocks);
    sys_free(io_buf);

    return bytes_written;
}


int32_t file_read(file* file, void* buf, uint32_t count) {
    uint8_t *buf_dst = (uint8_t *)buf;
    uint32_t size = count, size_left = size;

    if ((file->fd_pos + count) > file->fd_inode->i_size) {
        size = file->fd_inode->i_size - file->fd_pos;
        size_left = size;
        if (size == 0) {
            return -1;
        }
    }

    uint8_t *io_buf = sys_malloc(BLOCK_SIZE);
    if (io_buf == NULL) {
        printk("file_read: sys_malloc for io_buf failed\n");
    }
    uint32_t* all_blocks = (uint32_t*)sys_malloc(BLOCK_SIZE + 48);
    if (all_blocks == NULL) {
        printk("file_read: sys_malloc for all_blocks failed\n");
        return -1;
    }

    uint32_t block_read_start_idx = file->fd_pos / BLOCK_SIZE;
    uint32_t block_read_end_idx = (file->fd_pos + size) / BLOCK_SIZE;
    uint32_t read_blocks = block_read_start_idx - block_read_end_idx;
    ASSERT(block_read_start_idx < 139 && block_read_end_idx < 139);

    uint32_t block_idx;
    int32_t indirect_block_table;

    if (read_blocks == 0) {
        ASSERT(block_read_end_idx == block_read_start_idx);
        if (block_read_end_idx < 12) {
            block_idx = block_read_end_idx;
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
        }
        else {
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    }
    else {
        if (block_read_end_idx < 12) {
            block_idx = block_read_start_idx;
            while (block_idx <= block_read_end_idx) {
                all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
                block_idx++;
            }
        }
        else if (block_read_start_idx < 12 && block_read_end_idx >= 12) {
            block_idx = block_read_start_idx;
            while (block_idx < 12) {
                all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
                block_idx++;
            }
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
        else {
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    }

    uint32_t sec_idx, sec_lba, sec_off_bytes, sec_left_bytes, chunk_size;
    uint32_t bytes_read = 0;

    while (bytes_read < size) {
        sec_idx = file->fd_pos / BLOCK_SIZE;
        sec_lba = all_blocks[sec_idx];
        sec_off_bytes = file->fd_pos % BLOCK_SIZE;
        sec_left_bytes = BLOCK_SIZE - sec_off_bytes;
        chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;

        memset(io_buf, 0, BLOCK_SIZE);
        ide_read(cur_part->my_disk, sec_lba, io_buf, 1);
        memcpy(buf_dst, io_buf + sec_off_bytes, chunk_size);

        buf_dst += chunk_size;
        file->fd_pos += chunk_size;
        bytes_read += chunk_size;
        size_left -= chunk_size;
    }
    sys_free(all_blocks);
    sys_free(io_buf);

    return bytes_read;
}
