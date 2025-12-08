; ============================================================================
; irq_asm.asm - 硬件中断请求汇编存根 (i686)
; ============================================================================
; 为 16 个 IRQ 创建汇编入口点

[BITS 32]

section .text

; 导出 IRQ 符号
global irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
global irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15

; 导入 C 处理函数
extern irq_handler
extern interrupt_enter
extern interrupt_exit

; IRQ 宏
; IRQ 不会压入错误码，需要手动压入 0
%macro IRQ 2
irq%1:
    cli                     ; 禁用中断
    push 0                  ; 压入虚拟错误码
    push %2                 ; 压入中断号（32 + IRQ号）
    jmp irq_common_stub
%endmacro

; 定义所有 16 个 IRQ 处理程序
IRQ  0, 32    ; IRQ 0: 定时器
IRQ  1, 33    ; IRQ 1: 键盘
IRQ  2, 34    ; IRQ 2: 级联
IRQ  3, 35    ; IRQ 3: COM2/COM4
IRQ  4, 36    ; IRQ 4: COM1/COM3
IRQ  5, 37    ; IRQ 5: LPT2
IRQ  6, 38    ; IRQ 6: 软盘
IRQ  7, 39    ; IRQ 7: LPT1
IRQ  8, 40    ; IRQ 8: 实时时钟
IRQ  9, 41    ; IRQ 9: 自由
IRQ 10, 42    ; IRQ 10: 自由
IRQ 11, 43    ; IRQ 11: 自由
IRQ 12, 44    ; IRQ 12: PS/2 鼠标
IRQ 13, 45    ; IRQ 13: 协处理器
IRQ 14, 46    ; IRQ 14: 主 IDE
IRQ 15, 47    ; IRQ 15: 副 IDE

; 通用 IRQ 处理存根
; 与 ISR 类似，但调用 irq_handler
irq_common_stub:
    ; 保存所有通用寄存器
    pusha
    
    ; 保存数据段选择子
    xor eax, eax        ; 清零 EAX
    mov ax, ds          ; 加载 DS 到 AX (16位)
    push eax            ; 压入完整的 EAX (32位)
    
    ; 切换到内核数据段
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; 标记进入中断上下文
    call interrupt_enter
    
    ; 调用 C 处理函数
    push esp
    call irq_handler
    add esp, 4
    
    ; 标记离开中断上下文
    call interrupt_exit
    
    ; 恢复数据段选择子
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; 恢复所有通用寄存器
    popa
    
    ; 清理错误码和中断号
    add esp, 8
    
    ; 中断返回（IRET 会根据 CS.RPL 自动处理特权级切换并恢复 IF）
    iret
