#ifndef __LIB_KERNEL_PRINT_H__
#define __LIB_KERNEL_PRINT_H__

#include "stdint.h"

void put_int(uint32_t num);     // 以 16 进制打印
void put_str(char *message);
void put_char(uint8_t char_asci);

#endif
