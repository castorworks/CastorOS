/* CastorOS x86_64 链接器脚本 */
/* 高半核设计：内核加载到虚拟地址 0xFFFF800000000000 */

ENTRY(_start)

/* 内核虚拟地址基址 */
KERNEL_VIRTUAL_BASE = 0xFFFF800000000000;

SECTIONS
{
    /* 内核虚拟地址起始于高半核区域 */
    /* 物理地址从 1MB (0x100000) 开始 */
    . = KERNEL_VIRTUAL_BASE + 0x100000;

    /* 保存内核起始地址 */
    _kernel_start = .;

    /* Multiboot2 头部必须在文件前 32KB 内 */
    /* 使用 8 字节对齐 (Multiboot2 要求) */
    .multiboot ALIGN(8) : AT(ADDR(.multiboot) - KERNEL_VIRTUAL_BASE)
    {
        *(.multiboot)
    }

    /* 32 位引导代码 (在启用分页前执行) */
    .text.boot ALIGN(4K) : AT(ADDR(.text.boot) - KERNEL_VIRTUAL_BASE)
    {
        *(.text.boot)
    }

    /* .text 段：64 位代码段 */
    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VIRTUAL_BASE)
    {
        *(.text)
        *(.text.*)
    }

    /* .rodata 段：只读数据 */
    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VIRTUAL_BASE)
    {
        *(.rodata*)
    }

    /* .data 段：已初始化数据 */
    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VIRTUAL_BASE)
    {
        *(.data)
        *(.data.*)
    }

    /* .bss 段：未初始化数据 */
    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_VIRTUAL_BASE)
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
        *(.bootstrap_stack)
    }

    /* 保存内核结束地址 */
    _kernel_end = .;

    /* 丢弃不需要的段 */
    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note.gnu.build-id)
    }
}
