#!/bin/bash

lib=./lib/
img_path=.
src_path=./test
build_dir=./out

cwd=$(pwd)
cwd=${cwd##*/}
cwd=${cwd%/}

if [[ $cwd = "tools" ]]; then
    lib=../lib
    img_path=..
    src_path=../test
    build_dir=../out
fi

BIN="prog"
SRC=$src_path/prog_no_arg
CFLAGS="-Wall -fno-builtin -Wstrict-prototypes -Wmissing-prototypes \
        -fno-stack-protector -o0 -m32 -c -g"

OBJS="${build_dir}/string.o ${build_dir}/syscall.o ${build_dir}/stdio.o"

DD_IN=$build_dir/$BIN
DD_OUT="${img_path}/hd60M.img"

gcc -I $LIB $CFLAGS -o "$build_dir/$BIN.o" $SRC".c"
nasm -f elf32 $src_path/start.S -o $build_dir/start.o
ar rcs "$build_dir/simple_crt.a" $OBJS $build_dir/start.o
ld -m elf_i386 -z noexecstack $build_dir/$BIN".o" "$build_dir/simple_crt.a" -o $build_dir/$BIN

SEC_CNT=$(ls -l $DD_IN|awk '{printf("%d", ($5+511)/512)}')

if [[ -f $DD_IN ]]; then
    dd if=$DD_IN of=$DD_OUT bs=512 count=$SEC_CNT seek=300 conv=notrunc
fi
