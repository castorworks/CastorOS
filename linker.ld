/* CastorOS 链接器脚本 */
/* 高半核设计：内核加载到虚拟地址 0x80000000 */

ENTRY(_start)

SECTIONS
{
    /* 内核虚拟地址起始于 2GB (0x80000000) */
    /* 物理地址从 1MB (0x100000) 开始 */
    . = 0x80100000;

    /* 保存内核起始地址 */
    _kernel_start = .;

    /* .text 段：代码段 */
    .text ALIGN(4K) : AT(ADDR(.text) - 0x80000000)
    {
        *(.multiboot)
        *(.text)
    }

    /* .rodata 段：只读数据 */
    .rodata ALIGN(4K) : AT(ADDR(.rodata) - 0x80000000)
    {
        *(.rodata*)
    }

    /* .data 段：已初始化数据 */
    .data ALIGN(4K) : AT(ADDR(.data) - 0x80000000)
    {
        *(.data)
    }

    /* .bss 段：未初始化数据 */
    .bss ALIGN(4K) : AT(ADDR(.bss) - 0x80000000)
    {
        *(COMMON)
        *(.bss)
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
