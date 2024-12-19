#!/bin/bash

lib=lib
img_path=.
src_path=./test
build_dir=./out/lib

cwd=$(pwd)
cwd=${cwd##*/}
cwd=${cwd%/}

if [[ $cwd = "tools" ]]; then
    lib=../lib
    img_path=..
    src_path=../test
    build_dir=../out/lib
fi

src_name=prog_no_arg.c
if [[ -f $1 || -f $1".c" || -f "test/$1.c" || -f "test/$1" ]]; then
    src_name=prog_arg.c
fi

BIN="prog"
SRC=$src_path/$src_name

CLIBS="-I $lib -I $lib/user"
CFLAGS="-Wall -fno-builtin -Wstrict-prototypes -Wmissing-prototypes \
        -fno-stack-protector -o0 -m32 -c -g"

DD_IN=$build_dir/$BIN
DD_OUT="${img_path}/hd60M.img"

echo -e "Compile test/$src_name ...\n"

gcc $CLIBS $CFLAGS -o "$build_dir/$BIN.o" $SRC
ld -m elf_i386 -z noexecstack $build_dir/$BIN".o" "$build_dir/clib.a" -o $build_dir/$BIN

SEC_CNT=$(ls -l $DD_IN | cut -d ' ' -f 5)

if [[ -f $DD_IN ]]; then
    dd if=$DD_IN of=$DD_OUT bs=512 count=$SEC_CNT seek=300 conv=notrunc
fi
