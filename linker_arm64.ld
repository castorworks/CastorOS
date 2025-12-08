/* CastorOS ARM64 链接器脚本 */
/* 高半核设计：内核加载到虚拟地址 0xFFFF000000000000 */

ENTRY(_start)

SECTIONS
{
    /* 内核虚拟地址起始于高半核区域 */
    /* 物理地址从 0x40000000 开始 (QEMU virt machine) */
    . = 0xFFFF000040000000;

    /* 保存内核起始地址 */
    _kernel_start = .;

    /* .text 段：代码段 */
    .text ALIGN(4K) : AT(ADDR(.text) - 0xFFFF000000000000)
    {
        KEEP(*(.text.boot))
        *(.text)
        *(.text.*)
    }

    /* .rodata 段：只读数据 */
    .rodata ALIGN(4K) : AT(ADDR(.rodata) - 0xFFFF000000000000)
    {
        *(.rodata*)
    }

    /* .data 段：已初始化数据 */
    .data ALIGN(4K) : AT(ADDR(.data) - 0xFFFF000000000000)
    {
        *(.data)
        *(.data.*)
    }

    /* .bss 段：未初始化数据 */
    .bss ALIGN(4K) : AT(ADDR(.bss) - 0xFFFF000000000000)
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
        *(.note.GNU-stack)
    }
}
