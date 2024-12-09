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

#endif
