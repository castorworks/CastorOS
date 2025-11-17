; ============================================================================
; enter_usermode(entry_point, user_stack)
; ============================================================================

[BITS 32]

section .text
global enter_usermode

enter_usermode:
    cli

    mov eax, [esp + 4]      ; entry_point
    mov ebx, [esp + 8]      ; user_stack

    ; ----------------------------
    ; 构造用户态堆栈（IRET帧）
    ; ----------------------------
    push 0x23               ; SS = user data segment
    push ebx                ; ESP = user stack top

    pushfd                  ; EFLAGS
    pop ecx
    or ecx, (1 << 9)        ; IF=1
    and ecx, ~(1 << 27)     ; 清除 VM
    and ecx, ~(1 << 16)     ; 清除 RF
    push ecx

    push 0x1B               ; CS = user code segment
    push eax                ; EIP = entry point

    ; ----------------------------
    ; 设置段寄存器（用户数据段）
    ; ----------------------------
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; ----------------------------
    ; 切换到用户态
    ; ----------------------------
    nop
    iret
