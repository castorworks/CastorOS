; ============================================================================
; syscall_asm.asm - 系统调用汇编入口（最终正确版本）
; ============================================================================

[BITS 32]

section .text

global syscall_handler
extern syscall_dispatcher

syscall_handler:
    ;
    ; 保存通用寄存器（与 C 端frame布局匹配）
    ;
    push ebp
    push edi
    push esi
    push edx
    push ecx
    push ebx
    push eax          ; frame[1] = eax (syscall_num)

    ;
    ; 保存原 DS
    ;
    xor eax, eax
    mov ax, ds
    push eax          ; frame[0] = DS     <-- frame 起点

    ;
    ; 切换到内核数据段
    ;
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ;
    ; 固定 EBP 指向 frame[0]
    ;
    mov ebp, esp

    ;
    ; push 参数：syscall_dispatcher(num,p1,p2,p3,p4,p5,frame)
    ;
    push ebp                ; frame
    push dword [ebp + 24]   ; p5 = edi
    push dword [ebp + 20]   ; p4 = esi
    push dword [ebp + 16]   ; p3 = edx
    push dword [ebp + 12]   ; p2 = ecx
    push dword [ebp + 8]    ; p1 = ebx
    push dword [ebp + 4]    ; syscall_num = eax

    call syscall_dispatcher
    add esp, 28             ; 7 参数 * 4 字节

    ;
    ; 返回值存回 frame[1]
    ;
    mov [ebp + 4], eax      ; frame[1] = return value

    ;
    ; 恢复段寄存器
    ;
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ;
    ; 恢复寄存器，注意：eax 位置已经被替换为返回值
    ;
    pop eax
    pop ebx
    pop ecx
    pop edx
    pop esi
    pop edi
    pop ebp

    iret
