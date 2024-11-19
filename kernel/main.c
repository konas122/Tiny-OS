#include "init.h"
#include "print.h"

int main(void) {
    put_str("I am Kernel\n");

    put_int(0);
    put_char('\n');
    put_int(9);
    put_char('\n');
    put_int(0x00021a3f);
    put_char('\n');
    put_int(0x12345678);
    put_char('\n');
    put_int(0x00000000);
    put_char('\n');

    init_all();
    asm volatile("sti");

    while (1);

    return 0;
}
