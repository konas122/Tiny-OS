#ifndef __LIB_USER_UNISTD_H__
#define __LIB_USER_UNISTD_H__

#define NULL 0

typedef enum oflags {
    O_RDONLY,   // 只读
    O_WRONLY,   // 只写
    O_RDWR,     // 读写
    O_CREAT = 4 // 创建
} oflags;


typedef enum whence {
    SEEK_SET = 1,
    SEEK_CUR,
    SEEK_END
} whence;

#endif
