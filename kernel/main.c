#include "init.h"
#include "print.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "process.h"
#include "interrupt.h"


void k_thread_a(void *);
void k_thread_b(void *);
void u_prog_a(void);
void u_prog_b(void);
int test_var_a = 0, test_var_b = 0;


int main(void) {
    put_str("\nI am Kernel\n");

    init_all();
    intr_enable();

    thread_start("k_thread_a", 31, k_thread_a, "A_ ");
    thread_start("k_thread_b", 31, k_thread_b, "B_ ");

    process_execute(u_prog_a, "user_prog_a");
    process_execute(u_prog_b, "user_prog_b");

    while(1) {
        // console_put_str("Main ");
    };

    return 0;
}


void k_thread_a(void* arg) {
    while(1) {
        console_put_str(arg);
        console_put_str(": 0x");
        console_put_int(test_var_a);
        console_put_str("\n");
    }
}


void k_thread_b(void* arg) {
    while(1) {
        console_put_str(arg);
        console_put_str(": 0x");
        console_put_int(test_var_b);
        console_put_str("\n");
    }
}


void u_prog_a(void) {
    while(1) {
        test_var_a++;
    }
}


void u_prog_b(void) {
    while(1) {
        test_var_b++;
    }
}
