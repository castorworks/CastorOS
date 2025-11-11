; ============================================================================
; gdt_asm.asm - GDT 加载汇编代码
; ============================================================================
; 这个文件包含加载 GDT 并刷新段寄存器的汇编代码
; 最小汇编原则：仅包含 CPU 指令必须的汇编代码

[BITS 32]

section .text

global gdt_flush

; void gdt_flush(uint32_t gdt_ptr);
; 加载新的 GDT 并刷新段寄存器
gdt_flush:
    mov eax, [esp + 4]      ; 获取 GDT 指针参数
    lgdt [eax]              ; 加载 GDT
    
    ; 重新加载段寄存器
    ; 0x10 是内核数据段选择子（GDT 表项 2）
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; 远跳转以加载代码段选择子
    ; 0x08 是内核代码段选择子（GDT 表项 1）
    jmp 0x08:.flush
    
.flush:
    ret
