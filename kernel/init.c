#include "tss.h"
#include "ide.h"
#include "print.h"
#include "timer.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"
#include "interrupt.h"
#include "syscall_init.h"

#include "init.h"


void init_all() {
    put_str("init_all\n");
    idt_init();     // 初始化中断
    mem_init();	    // 初始化内存管理系统
    thread_init();  // 初始化线程相关结构
    timer_init();   // 初始化 PIT
    console_init();
    keyboard_init();
    tss_init();
    syscall_init();
    intr_enable();  // 后面的 ide_init 需要打开中断
    ide_init();     // 初始化硬盘
}
