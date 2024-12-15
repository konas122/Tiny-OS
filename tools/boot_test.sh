#!/bin/sh

src_path=../boot
work_path=..

if [ -e './boot' -o -e './out' ]; then
    src_path=boot
    work_path=.
fi

if [ $work_path = . ]; then
    if [ ! -d out ]; then
        mkdir out;
    fi
else
    if [ ! -d ../out ]; then
        mkdir ../out;
    fi
fi

nasm -I ${src_path}/include/ -o ${work_path}/out/mbr.bin ${src_path}/mbr.S && \
    dd if=${work_path}/out/mbr.bin of=${work_path}/hd60M.img bs=512 count=1 conv=notrunc

nasm -I ${src_path}/include/ -o ${work_path}/out/loader.bin ${src_path}/loader.S && \
    dd if=${work_path}/out/loader.bin of=${work_path}/hd60M.img bs=512 count=4 seek=2 conv=notrunc

gcc -g -m32 -c -o ${work_path}/out/main.o ${work_path}/test/boot_test_main.c && \
    ld ${work_path}/out/main.o -Ttext 0xc0001500 -m elf_i386 -e main -o ${work_path}/out/kernel.bin && \
    dd if=${work_path}/out/kernel.bin of=${work_path}/hd60M.img bs=512 count=200 seek=9 conv=notrunc
