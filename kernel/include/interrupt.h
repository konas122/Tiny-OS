#ifndef __KERNEL_INTERRUPT_H__
#define __KERNEL_INTERRUPT_H__

#include "stdint.h"

typedef void* intr_handler;

void idt_init(void);
void register_handler(uint8_t vector_no, intr_handler function);


/* 定义中断的两种状态:
 * INTR_OFF 值为 0, 表示关中断,
 * INTR_ON 值为 1, 表示开中断 */
typedef enum intr_status {  // 中断状态
    INTR_OFF,       // 中断关闭
    INTR_ON         // 中断打开
} intr_status;


intr_status intr_get_status(void);
intr_status intr_set_status (intr_status);
intr_status intr_enable (void);
intr_status intr_disable (void);

#endif
