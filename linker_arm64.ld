/* CastorOS ARM64 链接器脚本 */
/* 引导代码链接在物理地址；内核其余部分链接在高半区（TTBR1），加载地址仍是物理地址 */
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

    /* 内核起始地址。和其他链接符号一样给的是高半区虚拟地址
     * （高半区的 C++ 代码用 PC 相对寻址引用符号，够不到物理地址），
     * 需要物理地址的地方用 VIRT_TO_PHYS 转换 */
    _kernel_start = . + KERNEL_VIRTUAL_BASE;

    /* 引导代码 (在启用 MMU 前执行，使用物理地址) */
    .text.boot ALIGN(4K) :
    {
        *(.text.boot)
    }

    /* 之后的段运行在高半区：VMA = 物理地址 + KERNEL_VIRTUAL_BASE，LMA 保持物理地址 */
    . = ALIGN(4K);
    . += KERNEL_VIRTUAL_BASE;

    /* .text 段：代码段 */
    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VIRTUAL_BASE)
    {
        *(.text)
        *(.text.*)
    }

    /* .rodata 段：只读数据 */
    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VIRTUAL_BASE)
    {
        *(.rodata)
        *(.rodata.*)

        /* C++ 全局构造函数表 */
        . = ALIGN(8);
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
        __ctors_start = .;
        KEEP(*(SORT_BY_NAME(.ctors.*)))
        KEEP(*(.ctors))
        __ctors_end = .;
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
