/* CastorOS x86_64 链接器脚本 */
/* 高半核设计：内核加载到虚拟地址 0xFFFF800000000000 */

ENTRY(_start)

/* 内核虚拟地址基址 */
KERNEL_VIRTUAL_BASE = 0xFFFF800000000000;

SECTIONS
{
    /* 物理地址从 1MB (0x100000) 开始 */
    /* 32 位引导代码必须使用物理地址，因为 GRUB 在 32 位模式下加载 */
    . = 0x100000;

    /* 保存内核起始地址 */
    _kernel_start = .;

    /* Multiboot 头部必须在文件前 8KB 内 */
    .multiboot ALIGN(4) : AT(ADDR(.multiboot))
    {
        *(.multiboot)
    }

    /* 32 位引导代码 (在启用分页前执行，使用物理地址) */
    .text.boot ALIGN(4K) : AT(ADDR(.text.boot))
    {
        *(.text.boot)
    }

    /* 引导页表 (PML4/PDPT/PD)：32 位代码按物理地址填写，之后一直作为
       内核 PML4 使用。放在镜像内部，保证不会和其它段重叠。 */
    .boot.pagetables ALIGN(4K) : AT(ADDR(.boot.pagetables))
    {
        *(.boot.pagetables)
    }

    /* 切换到高半核虚拟地址 */
    . = KERNEL_VIRTUAL_BASE + .;

    /* .text 段：64 位代码段 (高半核地址) */
    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VIRTUAL_BASE)
    {
        *(.text)
        *(.text.*)
    }

    /* .rodata 段：只读数据 */
    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VIRTUAL_BASE)
    {
        *(.rodata*)
        *(.lrodata .lrodata.*)      /* -mcmodel=large 的大对象 */

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
        *(.ldata .ldata.*)          /* -mcmodel=large 的大对象 */
    }

    /* .bss 段：未初始化数据 */
    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_VIRTUAL_BASE)
    {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        *(.lbss .lbss.*)            /* -mcmodel=large 的大对象（task_pool 等） */
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
    }
}
