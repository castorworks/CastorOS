; ============================================================================
; ap_trampoline.asm - 其余 CPU 的入口（x86_64）
; ============================================================================
;
; 启动 CPU 用 INIT + SIPI 把一个 CPU 叫醒（hal::Cpu::start_secondaries）。被叫醒的 CPU
; 和一台刚开机的 8086 一样：实模式，16 位，从 SIPI 指定的那一页（低于 1MB）开始执行。
; 所以这段代码不能在内核映像里原地运行：启动 CPU 把它整段拷到物理地址 AP_TRAMPOLINE_BASE，
; 把页表、栈、入口函数这些参数填进末尾的参数区，再发 SIPI。
;
; 它做的事和 boot64.asm 对启动 CPU 做的一样，只是东西都现成：
;   16 位实模式 -> 32 位保护模式 -> 打开分页进长模式 -> 64 位，跳进高半区的 C++ 代码
;
; 因为是拷到别处运行的，里面所有的地址都写成"基址 + 相对这段代码开头的偏移"（宏 AT）。

AP_TRAMPOLINE_BASE  equ 0x8000
%define AT(label)   (AP_TRAMPOLINE_BASE + (label) - ap_trampoline_start)

section .rodata

global ap_trampoline_start
global ap_trampoline_end
global ap_trampoline_params

[BITS 16]
ap_trampoline_start:
    cli
    cld
    xor ax, ax
    mov ds, ax

    lgdt [AT(ap_gdt_pointer)]
    mov eax, cr0
    or eax, 1                       ; 保护模式
    mov cr0, eax
    jmp dword 0x08:AT(ap_protected)

[BITS 32]
ap_protected:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; 和启动 CPU 一样的 CR4（PAE、SSE 的那几位）、页表、EFER（长模式使能）、CR0（分页）。
    ; 页表是为这一步专门准备的：低地址的恒等映射（这段代码正在那里运行）加上内核的高半区
    mov eax, [AT(ap_param_cr4)]
    mov cr4, eax
    mov eax, [AT(ap_param_cr3)]
    mov cr3, eax
    mov ecx, 0xC0000080             ; EFER
    rdmsr
    or eax, [AT(ap_param_efer)]
    wrmsr
    mov eax, [AT(ap_param_cr0)]
    mov cr0, eax                    ; 分页打开：长模式（兼容子模式）
    jmp 0x18:AT(ap_long_mode)

[BITS 64]
ap_long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax

    ; 换到高半区的栈，进 C++（x86_64_ap_entry，不返回）
    mov rsp, [abs AT(ap_param_stack)]
    mov rax, [abs AT(ap_param_entry)]
    xor rbp, rbp
    call rax
ap_hang:
    hlt
    jmp ap_hang

align 8
ap_gdt:
    dq 0                            ; 空
    dq 0x00CF9A000000FFFF           ; 0x08: 32 位代码
    dq 0x00CF92000000FFFF           ; 0x10: 数据
    dq 0x00AF9A000000FFFF           ; 0x18: 64 位代码
ap_gdt_pointer:
    dw 4 * 8 - 1
    dd AT(ap_gdt)

; 参数区：启动 CPU 在发 SIPI 之前填好（struct ap_trampoline_params，见 smp64.cpp）
align 8
ap_trampoline_params:
ap_param_cr3:     dd 0
ap_param_cr4:     dd 0
ap_param_cr0:     dd 0
ap_param_efer:    dd 0
ap_param_stack:   dq 0
ap_param_entry:   dq 0
ap_trampoline_end:
