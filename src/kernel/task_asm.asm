; ============================================================================
; task_asm.asm - 任务切换汇编代码
; ============================================================================
; 实现底层的上下文切换
; 保存当前任务的 CPU 状态，恢复下一个任务的 CPU 状态

[BITS 32]

section .text

global task_switch

%define CTX_EBX    4
%define CTX_ESI    16
%define CTX_EDI    20
%define CTX_EBP    24
%define CTX_ESP    28
%define CTX_EIP    32
%define CTX_EFLAGS 36

; void task_switch(cpu_context_t *prev, cpu_context_t *next);
task_switch:
    ; 保存当前上下文
    mov eax, [esp + 4]          ; eax = prev
    mov [eax + CTX_EBX], ebx
    mov [eax + CTX_ESI], esi
    mov [eax + CTX_EDI], edi
    mov [eax + CTX_EBP], ebp
    mov [eax + CTX_ESP], esp
    mov edx, [esp]              ; 返回地址
    mov [eax + CTX_EIP], edx
    pushfd
    pop edx
    mov [eax + CTX_EFLAGS], edx

    ; 恢复下一个上下文
    mov eax, [esp + 8]          ; eax = next
    mov ebx, [eax + CTX_EBX]
    mov esi, [eax + CTX_ESI]
    mov edi, [eax + CTX_EDI]
    mov ebp, [eax + CTX_EBP]
    mov edx, [eax + CTX_EFLAGS]
    push edx
    popfd
    mov edx, [eax + CTX_EIP]
    mov esp, [eax + CTX_ESP]
    mov [esp], edx
    ret
