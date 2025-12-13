; ============================================================================
; boot64.asm - 内核引导代码 (x86_64)
; ============================================================================
; 32 位和 64 位混合代码，使用绝对寻址避免 RIP-relative 问题
; ============================================================================

; 强制使用绝对寻址 (不使用 RIP-relative)
default abs

; ============================================================================
; 常量定义
; ============================================================================

KERNEL_VMA              equ 0xFFFF800000000000

PAGE_PRESENT            equ (1 << 0)
PAGE_WRITE              equ (1 << 1)
PAGE_WRITE_THROUGH      equ (1 << 3)
PAGE_CACHE_DISABLE      equ (1 << 4)
PAGE_SIZE_2MB           equ (1 << 7)

CR0_PG                  equ (1 << 31)
CR0_WP                  equ (1 << 16)
CR4_PAE                 equ (1 << 5)

MSR_EFER                equ 0xC0000080
EFER_LME                equ (1 << 8)
EFER_NXE                equ (1 << 11)

CPUID_EXT_FUNC          equ 0x80000000
CPUID_EXT_FEATURES      equ 0x80000001
CPUID_LM_BIT            equ (1 << 29)

MULTIBOOT_MAGIC         equ 0x1BADB002
MULTIBOOT_PAGE_ALIGN    equ (1 << 0)
MULTIBOOT_MEMORY_INFO   equ (1 << 1)
MULTIBOOT_VIDEO_MODE    equ (1 << 2)
MULTIBOOT_FLAGS         equ (MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO | MULTIBOOT_VIDEO_MODE)
MULTIBOOT_CHECKSUM      equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)
MULTIBOOT_BOOTLOADER_MAGIC equ 0x2BADB002

BOOT_PML4_PHYS          equ 0x200000
BOOT_PDPT_PHYS          equ 0x201000
BOOT_PD_PHYS            equ 0x202000

; ============================================================================
; Multiboot1 头部
; ============================================================================

section .multiboot
align 4

multiboot_header:
    dd MULTIBOOT_MAGIC
    dd MULTIBOOT_FLAGS
    dd MULTIBOOT_CHECKSUM
    dd 0, 0, 0, 0, 0       ; a.out kludge (unused)
    dd 0                   ; mode_type: 0 = linear graphics mode (1 = EGA text)
    dd 0                   ; width: 0 = let GRUB choose based on gfxpayload
    dd 0                   ; height: 0 = let GRUB choose
    dd 32                  ; depth: 32bpp preferred

; ============================================================================
; 32 位引导代码
; ============================================================================

[BITS 32]

section .text.boot

global _start
extern kernel_main

_start:
    cli
    mov edi, eax
    mov esi, ebx

    cmp edi, MULTIBOOT_BOOTLOADER_MAGIC
    jne near no_multiboot

    call check_long_mode
    test eax, eax
    jz near no_long_mode

    call setup_page_tables

    mov eax, cr4
    or eax, CR4_PAE
    mov cr4, eax

    mov eax, BOOT_PML4_PHYS
    mov cr3, eax

    mov ecx, MSR_EFER
    rdmsr
    or eax, EFER_LME | EFER_NXE
    wrmsr

    mov eax, cr0
    or eax, CR0_PG | CR0_WP
    mov cr0, eax

    ; 使用绝对地址加载 GDT
    mov eax, gdt64_ptr_low
    lgdt [eax]
    
    ; 使用间接远跳转
    jmp dword 0x08:long_mode_entry_low

no_multiboot:
    mov dword [0xB8000], 0x4F524F45
    mov dword [0xB8004], 0x4F3A4F52
    mov dword [0xB8008], 0x4F424F4D
    jmp halt32

no_long_mode:
    mov dword [0xB8000], 0x4F524F45
    mov dword [0xB8004], 0x4F3A4F52
    mov dword [0xB8008], 0x4F4D4F4C

halt32:
    hlt
    jmp halt32

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

setup_page_tables:
    ; 清零页表
    mov edi, BOOT_PML4_PHYS
    mov ecx, 4096 * 3 / 4
    xor eax, eax
    rep stosd

    ; PML4[0] -> PDPT (使用寄存器间接寻址避免 RIP-relative)
    mov edi, BOOT_PML4_PHYS
    mov eax, BOOT_PDPT_PHYS + PAGE_PRESENT + PAGE_WRITE
    mov [edi], eax
    
    ; PML4[256] -> PDPT
    mov edi, BOOT_PML4_PHYS + 256 * 8
    mov [edi], eax
    
    ; PDPT[0] -> PD
    mov edi, BOOT_PDPT_PHYS
    mov eax, BOOT_PD_PHYS + PAGE_PRESENT + PAGE_WRITE
    mov [edi], eax

    ; PD: 映射前 1GB
    ; 第一个 2MB 页包含 VGA 内存 (0xB8000)，需要禁用缓存
    mov edi, BOOT_PD_PHYS
    ; 第一个 2MB 页：禁用缓存 (PCD=1, PWT=1) 用于 VGA 内存映射 I/O
    mov eax, PAGE_PRESENT | PAGE_WRITE | PAGE_SIZE_2MB | PAGE_CACHE_DISABLE | PAGE_WRITE_THROUGH
    mov [edi], eax
    mov dword [edi + 4], 0
    add edi, 8
    
    ; 剩余的 511 个 2MB 页：正常缓存
    mov eax, 0x200000 | PAGE_PRESENT | PAGE_WRITE | PAGE_SIZE_2MB
    mov ecx, 511
.fill_pd:
    mov [edi], eax
    mov dword [edi + 4], 0
    add eax, 0x200000
    add edi, 8
    loop .fill_pd
    ret

; 32 位模式 GDT
align 16
gdt64_low:
    dq 0
    dq 0x00209A0000000000
    dq 0x0000920000000000
gdt64_low_end:

gdt64_ptr_low:
    dw gdt64_low_end - gdt64_low - 1
    dd gdt64_low

; ============================================================================
; 64 位入口 (物理地址，在 .text.boot 段)
; ============================================================================

[BITS 64]

long_mode_entry_low:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 调试: 在 VGA 显示 "64" 表示进入了 64 位模式
    mov dword [0xB8000], 0x2F362F36  ; "66" in green
    mov dword [0xB8004], 0x2F342F34  ; "44" in green

    ; 跳转到高半核
    mov rax, long_mode_entry_high
    jmp rax

; ============================================================================
; 64 位代码 (高半核地址)
; ============================================================================

section .text

long_mode_entry_high:
    ; 调试: 显示 "HI" 表示进入高半核
    mov rax, 0xB8008 + KERNEL_VMA
    mov dword [rax], 0x2F482F48      ; "HH"
    mov dword [rax + 4], 0x2F492F49  ; "II"

    ; 加载高半核 GDT
    mov rax, gdt64_ptr
    lgdt [rax]

    ; 取消恒等映射
    mov rax, KERNEL_VMA + BOOT_PML4_PHYS
    mov qword [rax], 0

    ; 刷新 TLB
    mov rax, cr3
    mov cr3, rax

    ; 设置栈
    mov rax, stack_top
    mov rsp, rax

    ; 调试: 显示 "OK" 表示栈设置完成
    mov rax, 0xB8010 + KERNEL_VMA
    mov dword [rax], 0x2F4F2F4F      ; "OO"
    mov dword [rax + 4], 0x2F4B2F4B  ; "KK"

    ; kernel_main 参数
    xor rdi, rdi
    mov edi, esi
    mov rax, KERNEL_VMA
    add rdi, rax

    ; 调用内核
    mov rax, kernel_main
    call rax

    cli
.hang:
    hlt
    jmp .hang

; ============================================================================
; 64 位 GDT (高半核)
; ============================================================================

section .rodata
align 16

gdt64:
    dq 0
    dq 0x00209A0000000000
    dq 0x0000920000000000
    dq 0x0020FA0000000000
    dq 0x0000F20000000000
gdt64_end:

gdt64_ptr:
    dw gdt64_end - gdt64 - 1
    dq gdt64

; ============================================================================
; 数据
; ============================================================================

section .data

global boot_pml4
global boot_page_directory
boot_pml4:
boot_page_directory:
    dq KERNEL_VMA + BOOT_PML4_PHYS

global boot_pdpt
boot_pdpt:
    dq KERNEL_VMA + BOOT_PDPT_PHYS

global boot_pd
boot_pd:
    dq KERNEL_VMA + BOOT_PD_PHYS

; ============================================================================
; 栈
; ============================================================================

section .bss
align 16

global stack_bottom
global stack_top

stack_bottom:
    resb 32768
stack_top:
