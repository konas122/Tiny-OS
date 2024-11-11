%include "boot.inc"

SECTION MBR vstart=0x7c00
    mov ax, cs
    mov ds, ax
    mov ss, ax
    mov fs, ax
    mov es, ax
    mov sp, 0x7c00
    
    mov ax, 0xb800
    mov gs, ax

; -------------------------------------
; Clear Screen
; INT: 0x10     NUM: 0x06
; -------------------------------------
    mov ax, 0x600
    mov bx, 0x700
    mov cx, 0           ; Left Up: (0, 0)
    mov dx, 0x184f      ; Right Down: (80, 25)
    int 0x10

    mov byte [gs:0x00], '1'
    mov byte [gs:0x01], 0xA4

    mov byte [gs:0x02], ' '
    mov byte [gs:0x03], 0xA4

    mov byte [gs:0x04], 'M'
    mov byte [gs:0x05], 0xA4

    mov byte [gs:0x06], 'B'
    mov byte [gs:0x07], 0xA4

    mov byte [gs:0x08], 'R'
    mov byte [gs:0x09], 0xA4

; -------------------------------------
    mov eax, LOADER_START_SECTOR    ; 起始扇区 LBA 地址
    mov bx, LOADER_BASE_ADDR        ; 将数据写入的内存地址
    mov cx, 4                       ; 待读入的扇区数
    call rd_disk_m_16               ; 加载 loader.bin

    jmp LOADER_BASE_ADDR + 0x300    ; loader.bin 前 0x300 字节存放了 GDT 等数据,
                                    ; 运行所用的指令存放在上述的数据后面, 因此跳转地址要加上 0x300

; -------------------------------------
; 读取硬盘 n 个扇区
; eax: LBA 地址
; bx: 将数据写入的内存地址
; cx: 读入的扇区数
; -------------------------------------
rd_disk_m_16:
    mov esi, eax        ; 备份 eax
    mov di, cx          ; 备份 cx

; -------------------------------------
; 设置待读取的扇区数
; -------------------------------------
    mov dx, 0x1f2
    mov al, cl
    out dx, al

    mov eax, esi        ; 恢复 eax

; -------------------------------------
; 设置 LBA 地址
; -------------------------------------
    mov dx, 0x1f3       ; 写入 LBA 0-7 地址
    out dx, al
    mov dx, 0x1f4       ; 写入 LBA 8-15 地址
    mov cl, 8
    shr eax, cl
    out dx, al
    mov dx, 0x1f5       ; 写入 LBA 16-23 地址
    shr eax, cl
    out dx, al
    mov dx, 0x1f6       ; 写入 LBA 24-27 地址
    and al, 0x0f
    or al, 0xe0         ; 设置 4-7 位为 1110, 表示 LBA 模式
    out dx, al

; -------------------------------------
; 给磁盘写入读命令
; -------------------------------------
    mov dx, 0x1f7
    mov al, 0x20
    out dx, al

; -------------------------------------
; 检测硬盘状态
; -------------------------------------
.not_ready:
    nop
    in al, dx
    and al, 0x88        ; 第三位为 1 表示硬盘控制器已准备好数据传输
                        ; 第七位为 1 表示硬盘忙
    cmp al, 0x08
    jnz .not_ready

; -------------------------------------
; 从硬盘读取数据
; -------------------------------------
    mov ax, di          ; 设置读取的扇区数
    mov dx, 256
    mul dx
    mov cx, ax          ; di 为待读取的扇区数, 一个扇区有 512 字节, 每次读入一个字, 总共需要读取 di * 512 / 2 次
    mov dx, 0x1f0
.go_on_read:
    in ax, dx
    mov [bx], ax
    add bx, 2
    loop .go_on_read

    ret

    times 510-($-$$) db 0
    db 0x55, 0xaa
