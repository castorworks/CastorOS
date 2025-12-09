; ============================================================================
; boot64.asm - 内核引导代码 (x86_64)
; ============================================================================
; 这个文件包含 x86_64 内核的第一段执行代码
; 主要任务：
;   1. 验证 Multiboot2 引导
;   2. 检查 CPU 是否支持长模式
;   3. 建立临时 4 级页表（支持高半核）
;   4. 启用 PAE 和长模式
;   5. 跳转到 64 位代码
;   6. 设置栈并调用 C 内核入口

; ============================================================================
; 常量定义
; ============================================================================

; 内核虚拟地址基址 (高半核)
KERNEL_VMA              equ 0xFFFF800000000000
KERNEL_LMA              equ 0x100000            ; 物理加载地址 (1MB)

; 页表相关常量
PAGE_PRESENT            equ (1 << 0)
PAGE_WRITE              equ (1 << 1)
PAGE_SIZE_2MB           equ (1 << 7)            ; 2MB 大页

; CR0 标志
CR0_PG                  equ (1 << 31)           ; 分页启用
CR0_WP                  equ (1 << 16)           ; 写保护

; CR4 标志
CR4_PAE                 equ (1 << 5)            ; 物理地址扩展

; EFER MSR
MSR_EFER                equ 0xC0000080
EFER_LME                equ (1 << 8)            ; 长模式启用
EFER_NXE                equ (1 << 11)           ; NX 位启用

; CPUID 相关
CPUID_EXT_FUNC          equ 0x80000000
CPUID_EXT_FEATURES      equ 0x80000001
CPUID_LM_BIT            equ (1 << 29)           ; 长模式支持位

; Multiboot2 常量
MULTIBOOT2_MAGIC        equ 0xE85250D6
MULTIBOOT2_ARCH_I386    equ 0
MULTIBOOT2_HEADER_LEN   equ (multiboot2_header_end - multiboot2_header)
MULTIBOOT2_CHECKSUM     equ -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH_I386 + MULTIBOOT2_HEADER_LEN)

; Multiboot2 引导魔数 (由 bootloader 放入 EAX)
MULTIBOOT2_BOOTLOADER_MAGIC equ 0x36D76289

; ============================================================================
; Multiboot2 头部
; ============================================================================

section .multiboot
align 8

multiboot2_header:
    dd MULTIBOOT2_MAGIC                 ; 魔数
    dd MULTIBOOT2_ARCH_I386             ; 架构 (i386 保护模式)
    dd MULTIBOOT2_HEADER_LEN            ; 头部长度
    dd MULTIBOOT2_CHECKSUM              ; 校验和

    ; ---- 信息请求标签 ----
    align 8
    .tag_info_request:
        dw 1                            ; 类型: 信息请求
        dw 0                            ; 标志
        dd .tag_info_request_end - .tag_info_request
        dd 6                            ; 请求内存映射
        dd 8                            ; 请求帧缓冲信息
    .tag_info_request_end:

    ; ---- 帧缓冲标签 ----
    align 8
    .tag_framebuffer:
        dw 5                            ; 类型: 帧缓冲
        dw 0                            ; 标志
        dd 20                           ; 大小
        dd 0                            ; 宽度 (0 = 让 bootloader 选择)
        dd 0                            ; 高度 (0 = 让 bootloader 选择)
        dd 32                           ; 位深度

    ; ---- 结束标签 ----
    align 8
    .tag_end:
        dw 0                            ; 类型: 结束
        dw 0                            ; 标志
        dd 8                            ; 大小

multiboot2_header_end:

; ============================================================================
; 32 位引导代码段
; ============================================================================

[BITS 32]

section .text.boot

global _start
extern kernel_main

; 32 位入口点 (GRUB 跳转到这里)
_start:
    ; 禁用中断
    cli

    ; 保存 Multiboot2 信息
    mov edi, eax                        ; 保存魔数到 EDI
    mov esi, ebx                        ; 保存 MBI 地址到 ESI

    ; 验证 Multiboot2 魔数
    cmp edi, MULTIBOOT2_BOOTLOADER_MAGIC
    jne .no_multiboot

    ; 检查 CPU 是否支持长模式
    call check_long_mode
    test eax, eax
    jz .no_long_mode

    ; 设置临时页表
    call setup_page_tables

    ; 启用 PAE
    mov eax, cr4
    or eax, CR4_PAE
    mov cr4, eax

    ; 加载 PML4 物理地址到 CR3
    mov eax, boot_pml4 - KERNEL_VMA
    mov cr3, eax

    ; 启用长模式 (设置 EFER.LME)
    mov ecx, MSR_EFER
    rdmsr
    or eax, EFER_LME | EFER_NXE
    wrmsr

    ; 启用分页 (CR0.PG)
    mov eax, cr0
    or eax, CR0_PG | CR0_WP
    mov cr0, eax

    ; 加载 64 位 GDT
    lgdt [gdt64_ptr32 - KERNEL_VMA]

    ; 远跳转到 64 位代码段 (使用物理地址)
    jmp 0x08:(long_mode_entry - KERNEL_VMA)

.no_multiboot:
    mov dword [0xB8000], 0x4F524F45     ; "ER"
    mov dword [0xB8004], 0x4F3A4F52     ; "R:"
    mov dword [0xB8008], 0x4F424F4D     ; "MB"
    jmp .halt

.no_long_mode:
    mov dword [0xB8000], 0x4F524F45     ; "ER"
    mov dword [0xB8004], 0x4F3A4F52     ; "R:"
    mov dword [0xB8008], 0x4F4D4F4C     ; "LM"

.halt:
    hlt
    jmp .halt

; ============================================================================
; 检查 CPU 是否支持长模式
; ============================================================================
check_long_mode:
    mov eax, CPUID_EXT_FUNC
    cpuid
    cmp eax, CPUID_EXT_FEATURES
    jb .no_lm

    mov eax, CPUID_EXT_FEATURES
    cpuid
    test edx, CPUID_LM_BIT
    jz .no_lm

    mov eax, 1
    ret

.no_lm:
    xor eax, eax
    ret

; ============================================================================
; 设置临时 4 级页表
; 映射:
;   1. 恒等映射: 0x00000000 - 0x40000000 (1GB)
;   2. 高半核映射: 0xFFFF800000000000 - 0xFFFF800040000000 (1GB)
; ============================================================================
setup_page_tables:
    ; 清零页表
    mov edi, boot_pml4 - KERNEL_VMA
    mov ecx, 4096 * 4 / 4
    xor eax, eax
    rep stosd

    ; PML4[0] -> PDPT (恒等映射)
    mov eax, (boot_pdpt - KERNEL_VMA) + PAGE_PRESENT + PAGE_WRITE
    mov [boot_pml4 - KERNEL_VMA], eax

    ; PML4[256] -> PDPT (高半核映射)
    mov [boot_pml4 - KERNEL_VMA + 256 * 8], eax

    ; PDPT[0] -> PD
    mov eax, (boot_pd - KERNEL_VMA) + PAGE_PRESENT + PAGE_WRITE
    mov [boot_pdpt - KERNEL_VMA], eax

    ; PD: 映射前 1GB (512 个 2MB 页)
    mov edi, boot_pd - KERNEL_VMA
    mov eax, PAGE_PRESENT | PAGE_WRITE | PAGE_SIZE_2MB
    mov ecx, 512
.fill_pd:
    mov [edi], eax
    mov dword [edi + 4], 0              ; 高 32 位为 0
    add eax, 0x200000
    add edi, 8
    loop .fill_pd

    ret


; ============================================================================
; 64 位代码段
; ============================================================================

[BITS 64]

section .text

long_mode_entry:
    ; 加载数据段选择子
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 跳转到高半核地址
    mov rax, .higher_half
    jmp rax

.higher_half:
    ; 现在运行在高半核地址空间

    ; 加载高半核 GDT (使用绝对地址)
    mov rax, gdt64_ptr
    lgdt [rax]

    ; 取消恒等映射 (清除 PML4[0])
    mov rax, boot_pml4
    mov qword [rax], 0

    ; 刷新 TLB
    mov rax, cr3
    mov cr3, rax

    ; 设置栈指针 (使用绝对地址)
    mov rax, stack_top
    mov rsp, rax

    ; 准备调用 kernel_main
    ; RDI = Multiboot2 信息结构地址 (转换为高半核地址)
    xor rdi, rdi
    mov edi, esi
    mov rax, KERNEL_VMA
    add rdi, rax

    ; 调用 C 内核入口 (使用绝对地址)
    mov rax, kernel_main
    call rax

    ; 如果返回，进入死循环
    cli
.hang:
    hlt
    jmp .hang

; ============================================================================
; 64 位 GDT
; ============================================================================

section .rodata
align 16

gdt64:
    dq 0                                ; 空描述符
    dq 0x00209A0000000000               ; 64-bit 代码段 (ring 0)
    dq 0x0000920000000000               ; 64-bit 数据段 (ring 0)
    dq 0x0020FA0000000000               ; 64-bit 代码段 (ring 3)
    dq 0x0000F20000000000               ; 64-bit 数据段 (ring 3)
gdt64_end:

; GDT 指针 (高半核地址)
gdt64_ptr:
    dw gdt64_end - gdt64 - 1
    dq gdt64

; GDT 指针 (物理地址，用于 32 位模式)
section .data
align 8
gdt64_ptr32:
    dw gdt64_end - gdt64 - 1
    dq gdt64 - KERNEL_VMA

; ============================================================================
; 页表 (4KB 对齐)
; ============================================================================

section .bss
align 4096

global boot_pml4
global boot_page_directory  ; Alias for compatibility with vmm.c
boot_pml4:
boot_page_directory:        ; boot_page_directory points to PML4 on x86_64
    resb 4096

global boot_pdpt
boot_pdpt:
    resb 4096

global boot_pd
boot_pd:
    resb 4096

boot_pt:
    resb 4096

; ============================================================================
; 内核栈 (32KB)
; ============================================================================

align 16
global stack_bottom
global stack_top

stack_bottom:
    resb 32768
stack_top:
