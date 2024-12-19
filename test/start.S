[bits 32]
extern  main
extern  exit
section .text
global  _start

_start:
    push ebx    ; 压入 argv
    push ecx    ; 压入 argc
    call main

    push  eax
    call exit
