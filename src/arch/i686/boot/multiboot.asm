; ============================================================================
; multiboot.asm - Multiboot 头部（支持图形模式）(i686)
; ============================================================================
; 这个文件定义了符合 Multiboot 规范的内核头部
; 必须在内核文件的前 8KB 内
;
; 图形模式支持说明：
; - 设置 MULTIBOOT_VIDEO_MODE 标志请求图形模式信息
; - 将 width/height 设为 0 让 GRUB 根据 gfxmode 配置选择分辨率
; - 这样可以通过修改 grub.cfg 来切换不同分辨率

section .multiboot
align 4

; Multiboot 常量定义
MULTIBOOT_MAGIC        equ 0x1BADB002  ; Multiboot 魔数
MULTIBOOT_PAGE_ALIGN   equ 1 << 0      ; 页对齐标志
MULTIBOOT_MEMORY_INFO  equ 1 << 1      ; 请求内存信息
MULTIBOOT_VIDEO_MODE   equ 1 << 2      ; 请求视频模式信息
MULTIBOOT_FLAGS        equ MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO | MULTIBOOT_VIDEO_MODE
MULTIBOOT_CHECKSUM     equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

; Multiboot 头部结构
multiboot_header:
    dd MULTIBOOT_MAGIC              ; 魔数
    dd MULTIBOOT_FLAGS              ; 标志
    dd MULTIBOOT_CHECKSUM           ; 校验和
    ; 以下字段仅在 flags[16] 置位时使用（aout kludge，我们不使用）
    dd 0                            ; header_addr
    dd 0                            ; load_addr
    dd 0                            ; load_end_addr
    dd 0                            ; bss_end_addr
    dd 0                            ; entry_addr
    ; 视频模式字段（flags[2] 置位时使用）
    dd 0                            ; mode_type: 0 = 线性图形模式，1 = 文本模式
    dd 0                            ; width: 0 = 让 GRUB 根据 gfxpayload 选择
    dd 0                            ; height: 0 = 让 GRUB 根据 gfxpayload 选择
    dd 32                           ; depth: 首选位深度（32bpp）
