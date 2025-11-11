; ============================================================================
; multiboot.asm - Multiboot 头部
; ============================================================================
; 这个文件定义了符合 Multiboot 规范的内核头部
; 必须在内核文件的前 8KB 内

section .multiboot
align 4

; Multiboot 常量定义
MULTIBOOT_MAGIC        equ 0x1BADB002  ; Multiboot 魔数
MULTIBOOT_PAGE_ALIGN   equ 1 << 0      ; 页对齐标志
MULTIBOOT_MEMORY_INFO  equ 1 << 1      ; 请求内存信息
MULTIBOOT_FLAGS        equ MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO
MULTIBOOT_CHECKSUM     equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

; Multiboot 头部结构
multiboot_header:
    dd MULTIBOOT_MAGIC              ; 魔数
    dd MULTIBOOT_FLAGS              ; 标志
    dd MULTIBOOT_CHECKSUM           ; 校验和
