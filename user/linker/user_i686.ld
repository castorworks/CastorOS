/* 用户程序链接脚本 - i686 (x86 32-bit) */
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    /* 用户空间起始地址 */
    . = 0x10000000;
    
    /* 代码段 */
    .text ALIGN(0x1000) : {
        *(.text.startup)
        *(.text*)
    }
    
    /* 只读数据段 */
    .rodata ALIGN(0x1000) : {
        *(.rodata*)

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
    
    /* 数据段 */
    .data ALIGN(0x1000) : {
        *(.data*)
    }
    
    /* BSS 段（未初始化数据） */
    .bss ALIGN(0x1000) : {
        *(.bss*)
        *(COMMON)
    }
}
