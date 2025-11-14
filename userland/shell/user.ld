/* 用户程序链接脚本 */
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    /* 用户空间起始地址 */
    . = 0x40000000;
    
    /* 代码段 */
    .text ALIGN(0x1000) : {
        *(.text.startup)
        *(.text*)
    }
    
    /* 只读数据段 */
    .rodata ALIGN(0x1000) : {
        *(.rodata*)
    }
    
    /* 数据段 */
    .data ALIGN(0x1000) : {
        *(.data)
    }
    
    /* BSS 段（未初始化数据） */
    .bss ALIGN(0x1000) : {
        *(.bss)
        *(COMMON)
    }
}

