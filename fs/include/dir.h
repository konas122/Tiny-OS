#ifndef __FS_DIR_H__
#define __FS_DIR_H__

#include "fs.h"
#include "ide.h"
#include "inode.h"
#include "global.h"
#include "stdint.h"

#define MAX_FILE_NAME_LEN 16    // 最大文件名长度


typedef struct dir {
    struct inode* inode;   
    uint32_t dir_pos;       // 记录在目录内的偏移
    uint8_t dir_buf[512];   // 目录的数据缓存
} dir;


typedef struct dir_entry {
    char filename[MAX_FILE_NAME_LEN];   // 普通文件或目录名称
    uint32_t i_no;          // 普通文件或目录对应的 inode 编号
    file_types f_type;      // 文件类型
} dir_entry;


extern dir root_dir;

void open_root_dir(partition *part);
dir *dir_open(partition *part, uint32_t inode_no);
void dir_close(dir *dir);
bool search_dir_entry(partition *part, dir *pdir, const char *name, struct dir_entry *dir_e);
void create_dir_entry(char *filename, uint32_t inode_no, uint8_t file_type, dir_entry *p_de);
bool sync_dir_entry(dir *parent_dir, dir_entry *p_de, void *io_buf);

#endif
