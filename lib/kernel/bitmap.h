#ifndef __LIB_KERNEL_BITMAP_H__
#define __LIB_KERNEL_BITMAP_H__


#include "global.h"

#define BITMAP_MASK 1


typedef struct bitmap {
    uint32_t btmp_bytes_len;
    uint8_t *bits;
} bitmap;


void bitmap_init(bitmap *btmp);
bool bitmap_scan_test(const bitmap *btmp, uint32_t bit_idx);
int bitmap_scan(bitmap *btmp, uint32_t cnt);
void bitmap_set(bitmap *btmp, uint32_t bit_idx, int8_t value);


#endif // !__LIB_KERNEL_BITMAP_H__
