; ============================================================================
; isr_asm.asm - 中断服务例程汇编存根
; ============================================================================
; 为每个 CPU 异常创建汇编入口点
; 保存寄存器状态，调用 C 处理函数，然后恢复状态

[BITS 32]

section .text

; 导出 ISR 符号
global isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
global isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
global isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
global isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31

; 导入 C 处理函数
extern isr_handler

; ISR 宏：无错误码
; 某些异常不会压入错误码，需要手动压入 0 以保持栈平衡
%macro ISR_NOERRCODE 1
isr%1:
    cli                     ; 禁用中断
    push 0                  ; 压入虚拟错误码
    push %1                 ; 压入中断号
    jmp isr_common_stub     ; 跳转到通用处理
%endmacro

; ISR 宏：有错误码
; 某些异常会自动压入错误码，无需手动压入
%macro ISR_ERRCODE 1
isr%1:
    cli                     ; 禁用中断
    push %1                 ; 压入中断号
    jmp isr_common_stub     ; 跳转到通用处理
%endmacro

; 定义所有 32 个异常处理程序
ISR_NOERRCODE 0     ; 除零错误
ISR_NOERRCODE 1     ; 调试异常
ISR_NOERRCODE 2     ; 非屏蔽中断
ISR_NOERRCODE 3     ; 断点
ISR_NOERRCODE 4     ; 溢出
ISR_NOERRCODE 5     ; 边界检查
ISR_NOERRCODE 6     ; 无效操作码
ISR_NOERRCODE 7     ; 设备不可用
ISR_ERRCODE   8     ; 双重故障（有错误码）
ISR_NOERRCODE 9     ; 协处理器段超限
ISR_ERRCODE   10    ; 无效 TSS（有错误码）
ISR_ERRCODE   11    ; 段不存在（有错误码）
ISR_ERRCODE   12    ; 栈段错误（有错误码）
ISR_ERRCODE   13    ; 一般保护错误（有错误码）
ISR_ERRCODE   14    ; 页错误（有错误码）
ISR_NOERRCODE 15    ; 保留
ISR_NOERRCODE 16    ; 浮点异常
ISR_ERRCODE   17    ; 对齐检查（有错误码）
ISR_NOERRCODE 18    ; 机器检查
ISR_NOERRCODE 19    ; SIMD 浮点异常
ISR_NOERRCODE 20    ; 保留
ISR_NOERRCODE 21    ; 保留
ISR_NOERRCODE 22    ; 保留
ISR_NOERRCODE 23    ; 保留
ISR_NOERRCODE 24    ; 保留
ISR_NOERRCODE 25    ; 保留
ISR_NOERRCODE 26    ; 保留
ISR_NOERRCODE 27    ; 保留
ISR_NOERRCODE 28    ; 保留
ISR_NOERRCODE 29    ; 保留
ISR_ERRCODE   30    ; 安全异常（有错误码）
ISR_NOERRCODE 31    ; 保留

; 通用 ISR 处理存根
; 保存所有寄存器，调用 C 处理函数，然后恢复寄存器
isr_common_stub:
    ; 保存所有通用寄存器
    pusha                   ; 压入 EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
    
    ; 保存数据段选择子
    xor eax, eax        ; 清零 EAX
    mov ax, ds          ; 加载 DS 到 AX (16位)
    push eax            ; 压入完整的 EAX (32位)
    
    ; 切换到内核数据段
    mov ax, 0x10            ; 内核数据段选择子
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; 调用 C 处理函数
    push esp                ; 传递栈指针（指向 registers_t 结构）
    call isr_handler
    add esp, 4              ; 清理参数
    
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
    
    ; 恢复 CS, EIP, EFLAGS（Ring 0）
    ; 或 CS, EIP, EFLAGS, ESP, SS（Ring 3→Ring 0）
    ; CPU 会根据栈上的 CS.RPL 自动判断
    sti                     ; 重新启用中断
    iret                    ; 中断返回
