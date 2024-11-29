#include "init.h"
#include "print.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "interrupt.h"


void k_thread_a(void *);
void k_thread_b(void *);


int main(void) {
    put_str("\nI am Kernel\n");

    init_all();
    intr_enable();

    thread_start("k_thread_a", 31, k_thread_a, "A_ ");
    thread_start("k_thread_b", 31, k_thread_b, "B_ ");

    while(1) {
        // console_put_str("Main ");
    };

    return 0;
}


void k_thread_a(void* arg) {
    while(1) {
        console_put_str(arg);
    }
}


void k_thread_b(void* arg) {
    while(1) {
        console_put_str(arg);
    }
}
