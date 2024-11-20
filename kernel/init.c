#include "init.h"
#include "print.h"
#include "timer.h"
#include "interrupt.h"


void init_all() {
   put_str("init_all\n");
   idt_init();      // 初始化中断
   timer_init();    //  初始化 PIT
}