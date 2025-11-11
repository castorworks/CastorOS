; ============================================================================
; idt_asm.asm - IDT 加载汇编代码
; ============================================================================
; 包含加载 IDT 的汇编代码
; 最小汇编原则：仅包含 LIDT 指令

[BITS 32]

section .text

global idt_flush

; void idt_flush(uint32_t idt_ptr);
; 加载新的 IDT
idt_flush:
    mov eax, [esp + 4]      ; 获取 IDT 指针参数
    lidt [eax]              ; 加载 IDT
    ret
