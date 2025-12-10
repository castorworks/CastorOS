/* 用户程序链接脚本 - ARM64 (AArch64) */
OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS
{
    /* 用户空间起始地址 (64-bit user space) */
    . = 0x10000000;
    
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
