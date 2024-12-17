%include "boot.inc"

SECTION loader vstart=LOADER_BASE_ADDR


; -------------------------------------
; Initialize GDT
; -------------------------------------
GDT_BASE:
    dd 0x00000000
    dd 0x00000000

CODE_DESC:
    dd 0x0000FFFF
    dd DESC_CODE_HIGH4

DATA_STACK_DESC:
    dd 0x0000FFFF
    dd DESC_DATA_HIGH4

VIDEO_DESC:
    dd 0x80000007           ; limit = (0xbffff - 0xb8000) / 4k = 0x7
    dd DESC_VIDEO_HIGH4     ; 此时 dpl 已改为 0

GDT_SIZE    equ $ - GDT_BASE
GDT_LIMIT   equ GDT_SIZE - 1

    times 60 dq 0           ; 预留 60 个段描述符的空间


    total_mem_bytes dd 0    ; 用于保存内存容量, 以字节为单位, 此位置比较好记


SELECTOR_CODE   equ (0x0001 << 3) + TI_GDT + RPL0   ; 相当于 (CODE_DESC - GDT_BASE) / 8 + TI_GDT + RPL0
SELECTOR_DATA   equ (0x0002 << 3) + TI_GDT + RPL0
SELECTOR_VIDEO  equ (0x0003 << 3) + TI_GDT + RPL0


gdt_ptr dw GDT_LIMIT
        dd GDT_BASE


; -------------------------------------
    ards_buf times 244 db 0
    ards_nr dw 0            ; 用于记录 ards 结构体数量
; -------------------------------------


loader_start:
; -------------------------------------
; 打印字符串
; -------------------------------------
    mov sp, LOADER_BASE_ADDR
    mov bp, loader_msg      ; ES:BP = 字符串地址
    mov cx, 17              ; CX = 字符串长度
    mov ax, 0x1301          ; AH = 13h,  AL = 01h
    mov bx, 0x001f          ; 页号为 0 (BH = 0) 蓝底粉红字 (BL = 1fh)
    mov dx, 0x1800          ; (DH, DL) = 坐标(行, 列)
    int 0x10                ; 10h 号中断

; -------------------------------------
; 获取 ADRS 结构体
; -------------------------------------
    xor ebx, ebx            ; 第一次调用 ebx 清零
    mov edx, 0x534d4150
    mov di, ards_buf
.e820_mem_get_loop:
    mov eax, 0x0000e820     ; eax 存放子功能号
    mov ecx, 20             ; ARDS 地址范围描述符结构大小是 20 字节
    int 0x15
    jc .e820_failed_so_try_e801     ; 若 cf 位为 1 则有错误发生, 尝试 0xe801 子功能
    add di, cx              ; 使 di 增加 20 字节指向缓冲区中新的 ARDS 结构位置
    inc word [ards_nr]      ; 记录 ARDS 数量
    cmp ebx, 0              ; 若 ebx 为 0 且 cf 不为 1, 这说明 ARDS 全部返回, 当前已是最后一个
    jnz .e820_mem_get_loop

    mov cx, [ards_nr]
    mov ebx, ards_buf
    xor edx, edx            ; edx 用于存放最大的内存容量, 在此先清 0
.find_max_mem_area:
    mov eax, [ebx]          ; base_addr_low
    add eax, [ebx + 8]      ; length_addr_low
    add ebx, 20             ; 指向缓冲区中下一个 ARDS 结构
    cmp edx, eax
    jge .next_ards
    mov edx, eax
.next_ards:
    loop .find_max_mem_area
    jmp .mem_get_ok

; -------------------------------------
; int 15h, ax = E801h 获取内存大小, 最大支持4G
; 返回后 ax cx 值一样, 以 KB 为单位, bx dx 值一样, 以 64KB 为单位
; 在 ax 和 cx 寄存器中为低 16M, 在 bx 和 dx 寄存器中为 16MB 到 4G
; -------------------------------------
.e820_failed_so_try_e801:
    mov ax, 0xe801
    int 0x15
    jc .e801_failed_so_try88    ; 若当前 e801 方法失败, 就尝试 0x88 方法

; 先算出低 15M 的内存, ax 和 cx 中是以 KB 为单位的内存数量, 将其转换为以 byte 为单位
    mov cx, 0x400           ; cx 和 ax 值一样, cx 用做乘数
    mul cx 
    shl edx, 16
    and eax, 0x0000FFFF
    or edx, eax
    add edx, 0x100000   ; ax 只是 15MB, 故要加 1MB
    mov esi, edx        ; 先把低 15MB 的内存容量存入 esi 寄存器备份

; 将 16MB 以上的内存转换为 byte 为单位, 寄存器 bx 和dx中是以 64KB 为单位的内存数量
    xor eax, eax
    mov ax, bx
    mov ecx, 0x10000    ; 0x10000 十进制为 64KB
    mul ecx             ; 32 位乘法, 默认的被乘数是 eax, 积为 64 位, 高 32 位存入 edx, 低 32 位存入 eax
    add esi, eax        ; 由于此方法只能测出 4G 以内的内存, 故 32 位 eax 足够了, edx 肯定为 0, 只加 eax 便可
    mov edx, esi
    jmp .mem_get_ok

; -------------------------------------
; int 15h, ah = 0x88 获取内存大小, 只能获取 64M 之内
; -------------------------------------
.e801_failed_so_try88: 
    ; ax 存入的是以 kb 为单位的内存容量
    mov  ah, 0x88
    int  0x15
    jc .error_hlt
    and eax, 0x0000FFFF
        
    ; 16 位乘法, 被乘数是 ax, 积为 32 位. 积的高 16 位在 dx 中, 积的低 16 位在 ax中
    mov cx, 0x400       ; 0x400 等于 1024, 将 ax 中的内存容量换为以 byte 为单位
    mul cx
    shl edx, 16         ; 把 dx 移到高 16 位
    or edx, eax         ; 把积的低 16 位组合到 edx, 为 32 位的积
    add edx, 0x100000   ; 0x88 子功能只会返回 1MB 以上的内存, 故实际内存大小要加上 1MB

.mem_get_ok:
    mov [total_mem_bytes], edx


; -------------------------------------
; 进入保护模式
;   打开 A20
;   加载 GDT
;   将 CR0 的 PE 置为 1
; -------------------------------------
    in al, 0x92         ; 打开 A20
    or al, 0000_0010B
    out 0x92, al

    lgdt [gdt_ptr]      ; 加载 GDT

    mov eax, cr0        ; 将 CR0 的 PE 置为 1
    or eax, 0x00000001
    mov cr0, eax

    jmp dword SELECTOR_CODE:p_mode_start    ; 刷新 CPU 流水线, 避免受分支预测的影响

.error_hlt:
    hlt                 ; 停机指令 (特权指令)

; -------------------------------------
loader_msg db '2 loader in real.'
; -------------------------------------


[bits 32]
p_mode_start:
    mov ax, SELECTOR_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, LOADER_STACK_TOP
    mov ax, SELECTOR_VIDEO
    mov gs, ax

    mov eax, KERNEL_START_SECTOR            ; kernel.bin 所在的扇区号
    mov ebx, KERNEL_BIN_BASE_ADDR           ; 将 kernel.bin 写到 ebx 指定在的地址
    mov ecx, 200
    call rd_disk_m_32

    call setup_page

    sgdt [gdt_ptr]
    mov ebx, [gdt_ptr + 2]                  ; 将 gdt 描述符中视频段描述符中的段基址 + 0xc0000000
    or dword [ebx + 0x18 + 4], 0xc0000000   ; 视频段是第 3 个段描述符, 故 0x18
                                            ; 段描述符的高 4 字节的最高位是段基址的 31-24 位

    add dword [gdt_ptr + 2], 0xc0000000     ; 将 gdt 的基址加上 0xc0000000 使其成为内核所在的高地址
    add esp, 0xc0000000                     ; 将栈指针同样映射到内核地址

    ; 把页目录地址赋给 cr3
    mov eax, PAGE_DIR_TABLE_POS
    mov cr3, eax

    ; 打开 cr0 的 pg 位 (第 31 位)
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    lgdt [gdt_ptr]

    jmp SELECTOR_CODE:enter_kernel
enter_kernel:
    call kernel_init

    mov byte [gs:160], 'V'

    mov esp, 0xc009f000
    jmp KERNEL_ENTRY_POINT


; -------------------------------------
; 将 kernel.bin 中的 segment 拷贝到编译的地址
; -------------------------------------
kernel_init:
    xor eax, eax
    xor ebx, ebx    ; ebx 记录程序头表地址
    xor ecx, ecx    ; cx 记录程序头表中的 program header 数量
    xor edx, edx    ; dx 记录 program header 尺寸, 即 e_phentsize

    mov dx, [KERNEL_BIN_BASE_ADDR + 42]
    mov ebx, [KERNEL_BIN_BASE_ADDR + 28]
    add ebx, KERNEL_BIN_BASE_ADDR
    mov cx, [KERNEL_BIN_BASE_ADDR + 44]

.each_segment:
    cmp byte [ebx + 0], PT_NULL     ; 若等于, 则说明此 program header 未使用
    je .PTNULL

    ; 调用 mem_cpy(dst, src, size)
    push dword [ebx + 16]           ; 压入 p_filesz (size)
    mov eax, [ebx + 4]              ; 将 p_offset 存入 eax
    add eax, KERNEL_BIN_BASE_ADDR   ; eax 为该段的物理地址
    push eax                        ; 压入源地址
    push dword [ebx + 8]            ; 压入目的地址 p_vaddr
    call mem_cpy
    add esp, 12                     ; 清除前面压入的 3 个参数

.PTNULL:
    add ebx, edx                    ; edx 为 program header 大小, 即 e_phentsize
                                    ; 这里为的是让 ebx 指向下一个 program header
    loop .each_segment
    ret


; -------------------------------------
; memcpy(dst, src, size)
; -------------------------------------
mem_cpy:
    cld
    push ebp
    mov ebp, esp

    push ecx        ; rep 指令用到了 ecx, 然而 ecx 对外层段的循环有用, 故先入栈备份
    mov edi, [ebp + 8]
    mov esi, [ebp + 12]
    mov ecx, [ebp + 16]
    rep movsb       ; 逐字节拷贝

    pop ecx
    pop ebp
    ret


; -------------------------------------
; 创建页目录和页表
; -------------------------------------
setup_page:
    mov ecx, 4096
    mov esi, 0

.clear_page_dir:
    mov byte [PAGE_DIR_TABLE_POS + esi], 0
    inc esi
    loop .clear_page_dir

; Create Page Directory Entry
.create_pde:
    mov eax, PAGE_DIR_TABLE_POS
    add eax, 0x1000                     ; eax 为第一个 PTE 的位置
    mov ebx, eax                        ; 备份 eax
    or eax, PG_US_U | PG_RW_W | PG_P    ; 页目录项的属性 RW=1, P=1 和 US=1, 表示用户属性, 所有特权级别都可以访问

    ; 保证了分页前段机制下的 "线性地址" 和分页后 "虚拟地址" 对应的 "物理地址" 一致 (低端的 1MB)
    mov [PAGE_DIR_TABLE_POS + 0x0], eax
    ; 0xc00 表示第 768 个页表占用的目录项, 0xc00 以上的目录项用于内核空间,
    ; 从而页表的 0xc0000000-0xffffffff 共计 1G 属于内核, 0x0-0xbfffffff 共计 3G 属于用户进程
    mov [PAGE_DIR_TABLE_POS + 0xc00], eax

    sub eax, 0x1000
    mov [PAGE_DIR_TABLE_POS + 4092], eax    ; 使最后一个目录项指向页目录表自己的地址

; Create Page Table Entry
    mov ecx, 256    ; 低端内存 (1M) / 每页大小 (4k) = 256
    mov esi, 0
    mov edx, PG_US_U | PG_RW_W | PG_P
.create_pte:
    mov [ebx + esi * 4], edx
    add edx, 4096
    inc esi
    loop .create_pte

; 创建内核其它页表的 PDE
    mov eax, PAGE_DIR_TABLE_POS
    add eax, 0x2000 ; eax 为第二个 PTE 的位置
    or eax, PG_US_U | PG_RW_W | PG_P
    mov ebx, PAGE_DIR_TABLE_POS
    mov ecx, 254    ; 范围为 769-1022 的所有目录项数量 (0xc00/4=768)
    mov esi, 769
.create_kernel_pde:
    mov [ebx + esi * 4], eax
    inc esi
    add eax, 0x1000
    loop .create_kernel_pde
    ret


; -------------------------------------
; 读取磁盘
; -------------------------------------
rd_disk_m_32:
    mov esi, eax        ; 备份 eax
    mov di, cx          ; 备份扇区数到 di

; 设置要读取的扇区数
    mov dx, 0x1f2
    mov al, cl
    out dx, al          ; 读取的扇区数

    mov eax, esi        ; 恢复 ax

; 将 LBA 地址存入 0x1f3-0x1f6
    ; LBA 地址 7-0 位写入端口 0x1f3
    mov dx, 0x1f3                       
    out dx, al                          
    ; LBA 地址 15-8 位写入端口 0x1f4
    mov cl, 8
    shr eax, cl
    mov dx, 0x1f4
    out dx, al
    ; LBA 地址 23-16 位写入端口 0x1f5
    shr eax, cl
    mov dx, 0x1f5
    out dx, al

    shr eax, cl
    and al, 0x0f        ; LBA 第 24-27 位
    or al, 0xe0         ; 设置 7-4 位为 1110, 表示 LBA 模式
    mov dx, 0x1f6
    out dx, al

; 向 0x1f7 端口写入读命令, 0x20 
    mov dx, 0x1f7
    mov al, 0x20
    out dx, al

; 检测硬盘状态
.not_ready:             ; 测试 0x1f7 端口 (status寄存器) 的 BSY 位
    nop
    in al, dx
    and al, 0x88        ; 第 4 位为 1 表示硬盘控制器已准备好数据传输, 第 7 位为 1 表示硬盘忙
    cmp al, 0x08
    jnz .not_ready      ; 若未准备好, 继续等。

; 从 0x1f0 端口读数据
    mov ax,  di         ; 以下从硬盘端口读数据用 insw 指令更快捷, 不过尽可能多的演示命令使用, 
                        ; 在此先用这种方法, 在后面内容会用到 insw 和 outsw 等

    mov dx, 256         ; di 为要读取的扇区数, 一个扇区有 512 字节, 每次读入一个字, 共需 di * 512 / 2 次, 所以 di * 256
    mul dx
    mov cx, ax
    mov dx, 0x1f0
.go_on_read:
    in ax, dx
    mov [ebx], ax
    add ebx, 2
    loop .go_on_read
    ret
