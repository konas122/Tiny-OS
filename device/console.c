#include "print.h"
#include "sync.h"
#include "stdint.h"
#include "thread.h"

#include "console.h"


static lock console_lock;    // 控制台锁


void console_init() {
    lock_init(&console_lock);
}


void console_acquire() {
    lock_acquire(&console_lock);
}


void console_release() {
    lock_release(&console_lock);
}


void console_put_str(char *str) {
    console_acquire();
    put_str(str);
    console_release();
}


void console_put_char(uint8_t char_asci) {
    console_acquire();
    put_char(char_asci);
    console_release();
}


// 终端中输出 16 进制整数
void console_put_int(uint32_t num) {
    console_acquire();
    put_int(num);
    console_release();
}

