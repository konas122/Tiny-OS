#include "io.h"
#include "print.h"
#include "debug.h"
#include "thread.h"
#include "interrupt.h"

#include "timer.h"


#define IRQ0_FREQUENCY      100
#define INPUT_FREQUENCY     1193180
#define COUNTER0_VALUE      INPUT_FREQUENCY / IRQ0_FREQUENCY
#define CONTRER0_PORT       0x40
#define COUNTER0_NO         0
#define COUNTER_MODE        2
#define READ_WRITE_LATCH    3
#define PIT_CONTROL_PORT    0x43

#define mil_seconds_per_intr (1000 / IRQ0_FREQUENCY)

uint32_t ticks;     // ticks 是内核自中断开启以来总共的 ticks


static void intr_timer_handler(void) {
    task_struct *cur_thread = running_thread();
    ASSERT(cur_thread->stack_magic == 0x19780506);

    cur_thread->elapsed_ticks++;
    ticks++;

    if (cur_thread->ticks == 0) {
        schedule();
    }
    else {
        cur_thread->ticks--;
    }
}


// 把操作的计数器 counter_no, 读写锁属性 rwl 和计数器模式 counter_mode 写入模式控制寄存器并赋予初始值 counter_value
static void frequency_set(
    uint8_t counter_port,
    uint8_t counter_no,
    uint8_t rwl,
    uint8_t counter_mode,
    uint16_t counter_value)
{
    // 往控制字寄存器端口 0x43 中写入控制字
    outb(PIT_CONTROL_PORT, (uint8_t)(counter_no << 6 | rwl << 4 | counter_mode << 1));
    // 先写入 counter_value 的低 8 位
    outb(counter_port, (uint8_t)counter_value);
    // 再写入 counter_value 的高 8 位
    outb(counter_port, (uint8_t)counter_value >> 8);
}


static void ticks_to_sleep(uint32_t sleep_ticks) {
    uint32_t start_tick = ticks;
    while (ticks - start_tick < sleep_ticks) {
        thread_yield();
    }
}


void mtime_sleep(uint32_t m_seconds) {
    uint32_t sleep_ticks = DIV_ROUND_UP(m_seconds, mil_seconds_per_intr);
    ASSERT(sleep_ticks > 0);
    ticks_to_sleep(sleep_ticks); 
}


// 初始化 PIT 8253
void timer_init() {
    put_str("\ntimer_init start\n");
    // 设置 8253 的定时周期, 也就是发中断的周期
    frequency_set(CONTRER0_PORT, COUNTER0_NO, READ_WRITE_LATCH, COUNTER_MODE, COUNTER0_VALUE);
    register_handler(0x20, intr_timer_handler);
    put_str("timer_init done\n");
}
