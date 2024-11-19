#ifndef __KERNEL_INTERRUPT_H__
#define __KERNEL_INTERRUPT_H__

#include "stdint.h"

typedef void* intr_handler;

void idt_init(void);

#endif
