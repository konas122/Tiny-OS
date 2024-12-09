#ifndef __DEVICE_IDE_H__
#define __DEVICE_IDE_H__

#include "sync.h"
#include "stdint.h"
#include "bitmap.h"


// 分区结构
typedef struct partition {
    uint32_t start_lba;     // 起始扇区
    uint32_t sec_cnt;       // 扇区数
    struct disk* my_disk;   // 分区所属的硬盘
    list_elem part_tag;     // 用于队列中的标记
    char name[8];           // 分区名称
    struct super_block* sb; // 本分区的超级块
    bitmap block_bitmap;    // 块位图
    bitmap inode_bitmap;    // inode 位图
    list open_inodes;       // 本分区打开的 inode 队列
} partition;


// 硬盘结构
typedef struct disk {
    char name[8];                   // 本硬盘的名称
    struct ide_channel* my_channel; // 此块硬盘归属于哪个 ide 通道
    uint8_t dev_no;                 // 本硬盘是主 0 还是从 1
    partition prim_parts[4];        // 主分区顶多是 4 个
    partition logic_parts[8];       // 逻辑分区数量无限, 但总得有个支持的上限, 那就支持 8 个
} disk;


// ata 通道结构
typedef struct ide_channel {
    char name[8];           // 本 ata 通道名称 
    uint16_t port_base;     // 本通道的起始端口号
    uint8_t irq_no;         // 本通道所用的中断号
    lock lock;              // 通道锁
    bool expecting_intr;    // 表示等待硬盘的中断
    semaphore disk_done;    // 用于阻塞、唤醒驱动程序
    disk devices[2];        // 一个通道上连接两个硬盘, 一主一从
} ide_channel;


void ide_init(void);
extern uint8_t channel_cnt;
extern ide_channel channels[];
extern list partition_list;

void intr_hd_handler(uint8_t irq_no);
void ide_read(disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt);
void ide_write(disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt);

#endif
