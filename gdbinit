file out/kernel.bin

target remote localhost:1234
set disassembly-flavor intel

b kernel/main.c:2
c
