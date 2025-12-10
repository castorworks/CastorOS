/* CastorOS ARM64 链接器脚本 */
/* 简化版本：所有代码在物理地址空间 */
/* Requirements: 4.1 */

ENTRY(_start)

/* 内核虚拟地址基址 (TTBR1 区域) */
KERNEL_VIRTUAL_BASE = 0xFFFF000000000000;

/* 物理内存基址 (QEMU virt machine) */
PHYS_MEM_BASE = 0x40000000;

/* 内核物理加载地址 (1MB offset from RAM start) */
KERNEL_PHYS_BASE = 0x40100000;

SECTIONS
{
    /* 物理地址从 0x40100000 开始 (QEMU virt machine RAM + 1MB) */
    . = KERNEL_PHYS_BASE;

    /* 保存内核起始地址 */
    _kernel_start = .;

    /* 引导代码 (在启用 MMU 前执行，使用物理地址) */
    .text.boot ALIGN(4K) :
    {
        *(.text.boot)
    }

    /* .text 段：代码段 */
    .text ALIGN(4K) :
    {
        *(.text)
        *(.text.*)
    }

    /* .rodata 段：只读数据 */
    .rodata ALIGN(4K) :
    {
        *(.rodata)
        *(.rodata.*)
    }

    /* .data 段：已初始化数据 */
    .data ALIGN(4K) :
    {
        *(.data)
        *(.data.*)
    }

    /* .bss 段：未初始化数据 */
    .bss ALIGN(4K) :
    {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        *(.bootstrap_stack)
        _bss_end = .;
    }

    /* 保存内核结束地址 - 必须在 BSS 段之后 */
    . = ALIGN(4K);
    _kernel_end = .;

    /* 丢弃不需要的段 */
    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note.gnu.build-id)
        *(.note.GNU-stack)
        *(.ARM.attributes)
    }
}
