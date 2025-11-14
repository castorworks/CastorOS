; ============================================================================
; syscall_asm.asm - 系统调用汇编入口
; ============================================================================

[BITS 32]

section .text

global syscall_handler
extern syscall_dispatcher

; 系统调用处理程序
; 调用约定：
;   EAX = 系统调用号
;   EBX = 参数 1
;   ECX = 参数 2
;   EDX = 参数 3
;   ESI = 参数 4
;   EDI = 参数 5
;   返回值放在 EAX
syscall_handler:
    ; 保存所有寄存器
    push ebp
    push edi
    push esi
    push edx
    push ecx
    push ebx
    push eax
    
    ; 保存并切换段寄存器到内核数据段
    xor eax, eax
    mov ax, ds
    push eax            ; 保存原 DS（用于恢复 ds/es/fs/gs）
    mov ax, 0x10        ; 内核数据段选择子
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; 从已保存的寄存器构造参数并调用 C 处理函数
    ; 将当前 ESP 固定到 EBP，避免 push 修改 ESP 导致偏移错位
    mov ebp, esp
    
    ; 传递栈帧指针作为第一个参数（用于 fork 等需要访问寄存器的系统调用）
    push ebp                ; stack_frame pointer
    push dword [ebp + 24]   ; p5 = edi
    push dword [ebp + 20]   ; p4 = esi
    push dword [ebp + 16]   ; p3 = edx
    push dword [ebp + 12]   ; p2 = ecx
    push dword [ebp + 8]    ; p1 = ebx
    push dword [ebp + 4]    ; syscall_num = eax
    
    call syscall_dispatcher
    add esp, 28             ; 清理参数（7 * 4 = 28 字节）
    
    ; 将返回值写入保存的 eax 位置（使用固定基址 EBP）
    mov [ebp + 4], eax
    
    ; 恢复段寄存器
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; 恢复寄存器（eax 将从栈中弹出为返回值）
    pop eax         ; eax = 返回值
    pop ebx
    pop ecx
    pop edx
    pop esi
    pop edi
    pop ebp
    
    ; 返回用户态（IRET）
    iret
