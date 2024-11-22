#include "init.h"
#include "print.h"
#include "debug.h"
#include "memory.h"


int main(void) {
    put_str("\nI am Kernel\n");

    put_int(0);
    put_char('\n');
    put_int(9);
    put_char('\n');
    put_int(0x00021a3f);
    put_char('\n');

    init_all();

    void *addr = get_kernel_pages(3);
    put_str("\n`get_kernel_page` start vaddr: 0x");
    put_int((uint32_t)addr);
    put_str("\n");

    asm volatile("sti");

    while (1);

    return 0;
}
