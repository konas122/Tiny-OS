file out/kernel.bin

target remote localhost:1234
set disassembly-flavor intel

b test/main.c:2

b kernel/main.c:4
c
