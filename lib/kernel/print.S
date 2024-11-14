TI_GDT  equ 0
RPL0    equ 0
SELECTOR_VIDEO  equ (0x0003<<3) + TI_GDT + RPL0

section .data
put_int_buffer    dq    0   ; 定义 8 bytes 缓冲区用于数字到字符的转换

[bits 32]
section .text

global put_int
global put_str
global put_char


; -------------------------------------
put_int:
    pushad
    mov ebp, esp
    mov eax, [ebp + 4 * 9]  ; call 返回地址占 4 bytes + pushad 的 8 个 4 bytes
    mov edx, eax
    mov edi, 7              ; 指定在 put_int_buffer 中的初始偏移量
    mov ecx, 8              ; 32 位数字中, 十六进制数字的位数是 8 个
    mov ebx, put_int_buffer

.16based_4bits:
    and edx, 0x0000000F
    cmp edx, 9
    jg .is_A2F
    add edx, '0'
    jmp .store
.is_A2F:
    sub edx, 10
    add edx, 'A'

.store:
    mov [ebx+edi], dl
    dec edi
    shr eax, 4
    mov edx, eax
    loop .16based_4bits

; 把高位连续的字符去掉, 比如把字符 000123 变成 123
.ready_to_print:
    inc edi             ; 此时 edi 若退减为 -1(0xffffffff), 加 1 使其为 0
.skip_prefix_0:
    cmp edi, 8          ; 若已经比较第 9 个字符了, 表示待打印的字符串为全 0 
    je .full0

; 找出连续的 0 字符, edi 做为非 0 的最高位字符的偏移
.go_on_skip:
    mov cl, [put_int_buffer+edi]
    inc edi
    cmp cl, '0'
    je .skip_prefix_0   ; 继续判断下一位字符是否为字符 '0'
    dec edi             ; edi 在上面的 inc 操作中指向了下一个字符, 若当前字符不为 '0', 要恢复 edi 指向当前字符		       
    jmp .put_each_num

.full0:
    mov cl, '0'                     ; 输入的数字为全 0 时, 则只打印 0
.put_each_num:
    push ecx
    call put_char
    add esp, 4
    inc edi                         ; 使 edi 指向下一个字符
    mov cl, [put_int_buffer+edi]    ; 获取下一个字符到cl寄存器
    cmp edi, 8
    jl .put_each_num

    popad
    ret


; -------------------------------------
put_str:
    ; 本函数中只用到了 ebx 和 ecx, 只备份这两个寄存器
    push ebx
    push ecx
    xor ecx, ecx
    mov ebx, [esp + 12]
.go_on:
    mov cl, [ebx]
    cmp cl, 0
    jz .str_over
    push ecx    ; 为 put_char 函数传递参数
    call put_char
    add esp, 4
    inc ebx
    jmp .go_on
.str_over:
    pop ecx
    pop ebx
    ret


; -------------------------------------
put_char:
    pushad      ; 备份 32 位寄存器
    mov ax, SELECTOR_VIDEO
    mov gs, ax

    ; 获取当前光标位置
    ; 先获得高 8 位
    mov dx, 0x03d4  ; 索引寄存器
    mov al, 0x0e    ; 用于提供光标位置的高 8 位
    out dx, al
    mov dx, 0x03d5  ; 通过读写数据端口 0x3d5 来获得或设置光标位置 
    in al, dx       ; 得到了光标位置的高 8 位
    mov ah, al

    ; 再获取低 8 位
    mov dx, 0x03d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x03d5 
    in al, dx

    ; 将光标存入 bx
    mov bx, ax	  

    ; 下面这行是在栈中获取待打印的字符
    mov ecx, [esp + 36] ; pushad 压入 32 bytes, 加上主调函数的返回地址 4 字节, 故 esp+36 字节

    cmp cl, 0xd         ; CR 是 0x0d, LF 是 0x0a, BS(Backspace) 是 0x8
    jz .is_carriage_return
    cmp cl, 0xa
    jz .is_line_feed
    cmp cl, 0x8
    jz .is_backspace
    jmp .put_other

; Backspace 本质上只要将光标移向前一个显存位置即可. 后面再输入的字符自然会覆盖此处的字符
.is_backspace:
    dec bx
    shl bx, 1

    mov byte [gs:bx], 0x20
    inc bx
    mov byte [gs:bx], 0x07
    shr bx, 1
    jmp .set_cursor

.put_other:
    shl bx, 1
    mov byte [gs:bx], cl
    inc bx
    mov byte [gs:bx], 0x07
    shr bx, 1
    inc bx
    cmp bx, 2000    ; 若光标值小于 2000, 表示未写到显存的最后, 则去设置新的光标值
    jl .set_cursor

.is_line_feed:
.is_carriage_return:
    xor dx, dx      ; dx 是被除数的高 16 位, 清 0
    mov ax, bx      ; ax 是被除数的低 16 位
    mov si, 80      ; 由于是效仿 linux, linux 中 \n 便表示下一行的行首, 所以这里也把 \n 和 \r 都处理为 linux 中 \n
    div si
    sub bx, dx      ; 光标值减去除 80 的余数便是取整
.is_carriage_return_end:
    add bx, 80
    cmp bx, 2000
.is_line_feed_end:
    jl .set_cursor

.roll_screen:
    cld             ; 清除方向位, 也就是把 eflags 寄存器中的 DF 设为 0
    mov ecx, 960    ; 一共有 2000-80=1920 个字符要搬运, 共 1920*2=3840 字节. 一次搬 4 字节, 共 3840/4=960 次
    mov esi, 0xb80a0    ; 第 1 行行首
    mov edi, 0xb8000    ; 第 0 行行首
    rep movsd

    ; 将最后一行填充为空白
    mov ebx, 3840       ; 最后一行首字符的第一个字节偏移 = 1920 * 2
    mov ecx, 80         ; 一行是 80 字符(160字节), 每次清空 1 字符(2字节), 一行需要移动 80 次
.cls:
    mov word [gs:ebx], 0x720    ; 0x0720 是黑底白字的空格键
    add ebx, 2
    loop .cls
    mov bx, 1920

; 将光标设为 bx 值
.set_cursor:
    ; 设置高 8 位
    mov dx, 0x03d4      ; 索引寄存器
    mov al, 0xe0        ; 用于光标位置的高 8 位
    out dx, al
    mov dx, 0x03d5
    mov al, bh
    out dx, al

    ; 设置低 8 位
    mov dx, 0x03d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x03d5 
    mov al, bl
    out dx, al

.put_char_done:
    popad
    ret
