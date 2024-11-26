[bits 32]
section .text

global switch_to

; void switch_to(task_struct *cur, task_struct *next)
switch_to:
    push esi
    push edi
    push ebx
    push ebp

    mov eax, [esp + 20] ; 获得参数 cur
    mov [eax], esp      ; 将 esp 保存到 cur->self_kstack 中

    mov eax, [esp + 24] ; 获得参数 next
    mov esp, [eax]      ; 将 next->self_kstack 的值存入 esp
    
    pop ebp
    pop ebx
    pop edi
    pop esi
    ret


;   |             |  high
;   ---------------
;   |     next    |
;   ---------------
;   |     cur     |
;   ---------------
;   | return addr |
;   ---------------
;   |     esi     |
;   ---------------
;   |     edi     |
;   ---------------
;   |     ebx     |
;   ---------------
;   |     ebp     |
;   ---------------   <-- esp
;   |             |   low
