; ============================================================================
; boot.asm - 内核引导代码
; ============================================================================
; 这个文件包含内核的第一段执行代码
; 主要任务：
;   1. 建立临时页表（支持高半核）
;   2. 启用分页
;   3. 设置栈
;   4. 跳转到 C 内核入口

[BITS 32]

; 常量定义
KERNEL_VIRTUAL_BASE equ 0x80000000                   ; 内核虚拟地址基址（2GB）
KERNEL_PAGE_NUMBER  equ (KERNEL_VIRTUAL_BASE >> 22)  ; 页目录索引 = 512

section .data
align 0x1000  ; 页表必须 4KB 对齐

; 引导页目录
; 需要映射的区域：
;   1. 0x00000000 - 0x00400000 (恒等映射，用于启用分页过渡) - 4MB
;   2. 0x80000000 - 0x80800000 (高半核映射，映射前 8MB 物理内存)
boot_page_directory:
    ; 页目录第 0 项：映射前 4MB（恒等映射）
    dd (boot_page_table1 - KERNEL_VIRTUAL_BASE) + 0x003
    
    ; 页目录第 1-511 项：未映射
    times (KERNEL_PAGE_NUMBER - 1) dd 0
    
    ; 页目录第 512-513 项：映射 0x80000000-0x80800000（高半核映射 8MB）
    dd (boot_page_table1 - KERNEL_VIRTUAL_BASE) + 0x003  ; 0-4MB
    dd (boot_page_table2 - KERNEL_VIRTUAL_BASE) + 0x003  ; 4-8MB
    
    ; 页目录第 514-1023 项：未映射
    times (1024 - KERNEL_PAGE_NUMBER - 2) dd 0

; 引导页表 1：映射物理内存 0-4MB
; 每个页表项映射一个 4KB 页
; 标志位 0x003 = Present(1) | Read/Write(1) | User/Supervisor(0)
; 注：此阶段仅需 P 和 RW 位，未设置 US/PCD/PWT/Global 等（简化设计）
boot_page_table1:
    %assign i 0
    %rep 1024
        dd (i << 12) | 0x003  ; 地址 | 标志位 (Present | Read/Write)
        %assign i i+1
    %endrep

; 引导页表 2：映射物理内存 4MB-8MB
; 确保 Multiboot 信息结构和其他引导数据在映射范围内
boot_page_table2:
    %assign i 1024
    %rep 1024
        dd (i << 12) | 0x003  ; 地址 | 标志位 (Present | Read/Write)
        %assign i i+1
    %endrep

section .text
align 4

global _start
global boot_page_directory
global boot_page_table1
global boot_page_table2
global stack_bottom
global stack_top
extern kernel_main

_start:
    ; 此时，GRUB 已经：
    ;   - 将内核加载到物理地址 0x100000
    ;   - 将 Multiboot 信息结构地址放在 ebx
    ;   - 将魔数 0x2BADB002 放在 eax
    ;   - 处于保护模式，禁用分页
    ;   - A20 线已启用
    
    ; 0. 禁用中断（防止在设置分页期间被中断打断）
    cli
    
    ; 1. 加载页目录地址到 CR3
    mov ecx, (boot_page_directory - KERNEL_VIRTUAL_BASE)
    mov cr3, ecx
    
    ; 2. 启用分页：设置 CR0 的 PG 位
    mov ecx, cr0
    or ecx, 0x80000000
    mov cr0, ecx
    
    ; 3. 跳转到高半核地址
    ; 此跳转会刷新指令流水线，确保后续指令在分页模式下正确执行
    lea ecx, [higher_half]
    jmp ecx

higher_half:
    ; 现在我们运行在高半核地址空间
    
    ; 4. 取消恒等映射（不再需要）
    mov dword [boot_page_directory], 0
    
    ; 5. 刷新 TLB (Translation Lookaside Buffer)
    mov ecx, cr3
    mov cr3, ecx
    
    ; 6. 设置栈指针
    mov esp, stack_top
    ; 注：栈已在 BSS 段 16 字节对齐
    ; 若需严格 16 字节对齐（SSE/System V ABI），可添加：
    ; and esp, 0xFFFFFFF0
    
    ; 7. 重置 EFLAGS
    push 0
    popf
    
    ; 8. 保存 Multiboot 信息
    ; ebx 包含 Multiboot 信息结构的物理地址
    ; 需要加上虚拟地址偏移
    ; 注意：我们已映射前 8MB 物理内存到高半核，确保 MBI 在可访问范围内
    add ebx, KERNEL_VIRTUAL_BASE
    push ebx  ; 传递给 kernel_main
    
    ; 9. 调用 C 内核入口
    call kernel_main
    
    ; 10. 如果 kernel_main 返回，进入死循环
    cli         ; 禁用中断
.hang:
    hlt         ; 停机
    jmp .hang

; 内核栈（16KB）
section .bss
align 16
stack_bottom:
    resb 16384  ; 16KB 栈空间
stack_top:
