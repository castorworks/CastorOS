; ============================================================================
; gdt_asm.asm - GDT 和 TSS 汇编支持函数
; ============================================================================
[BITS 32]

section .text
global gdt_flush
global tss_load

; void gdt_flush(uint32_t gdt_ptr);
; 参数: 指向 gdt_ptr（limit:uint16 + base:uint32）的地址
gdt_flush:
    mov eax, [esp + 4]      ; 获取参数（C 传入 &gdt_pointer）
    lgdt [eax]              ; lgdt (m16/32)
    
    ; 重新加载数据段寄存器为内核数据段选择子（0x10）
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 远跳转加载内核代码段选择子（0x08）
    jmp 0x08:.flush
.flush:
    ret

; void tss_load(uint16_t selector);
; 参数: tss selector 在栈上（[esp+4]）
tss_load:
    mov ax, [esp + 4]
    ltr ax
    ret
