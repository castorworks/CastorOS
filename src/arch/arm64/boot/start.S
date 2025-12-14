/*
 * start.S - ARM64 内核引导代码
 *
 * 实现 UEFI/U-Boot 入口点
 * 验证 Exception Level (EL1)
 * 设置初始栈
 *
 * Requirements: 4.1
 *
 * ARM64 引导流程:
 *   1. 从 UEFI/U-Boot 进入 _start (应在 EL1)
 *   2. 验证当前 Exception Level
 *   3. 设置初始栈指针
 *   4. 清零 BSS 段
 *   5. 设置初始页表并启用 MMU
 *   6. 跳转到 kernel_main
 *
 * 内存布局 (QEMU virt machine):
 *   - 物理内存从 0x40000000 开始
 *   - 内核加载到 0x40100000 (物理)
 *   - 内核虚拟地址 0xFFFF000000100000
 */

/* ============================================================================
 * 常量定义
 * ========================================================================== */

/* 内核虚拟地址基址 (TTBR1 区域) */
.equ KERNEL_VMA,            0xFFFF000000000000

/* 物理内存基址 (QEMU virt machine) */
.equ PHYS_MEM_BASE,         0x40000000

/* 内核物理加载地址 */
.equ KERNEL_PHYS_BASE,      0x40100000

/* 页大小 */
.equ PAGE_SIZE,             4096
.equ PAGE_SHIFT,            12

/* 栈大小 (32KB) */
.equ STACK_SIZE,            32768

/* Exception Level 掩码 */
.equ CURRENTEL_EL_MASK,     0x0C
.equ CURRENTEL_EL1,         0x04
.equ CURRENTEL_EL2,         0x08
.equ CURRENTEL_EL3,         0x0C

/* SCTLR_EL1 位定义 */
.equ SCTLR_M,               (1 << 0)    /* MMU enable */
.equ SCTLR_A,               (1 << 1)    /* Alignment check */
.equ SCTLR_C,               (1 << 2)    /* Data cache enable */
.equ SCTLR_SA,              (1 << 3)    /* Stack alignment check */
.equ SCTLR_I,               (1 << 12)   /* Instruction cache enable */
.equ SCTLR_WXN,             (1 << 19)   /* Write permission implies XN */
.equ SCTLR_EE,              (1 << 25)   /* Exception endianness */

/* TCR_EL1 配置 */
.equ TCR_T0SZ,              (16 << 0)   /* TTBR0 region size: 48-bit */
.equ TCR_T1SZ,              (16 << 16)  /* TTBR1 region size: 48-bit */
.equ TCR_TG0_4KB,           (0 << 14)   /* TTBR0 granule: 4KB */
.equ TCR_TG1_4KB,           (2 << 30)   /* TTBR1 granule: 4KB */
.equ TCR_SH0_INNER,         (3 << 12)   /* TTBR0 inner shareable */
.equ TCR_SH1_INNER,         (3 << 28)   /* TTBR1 inner shareable */
.equ TCR_ORGN0_WB_WA,       (1 << 10)   /* TTBR0 outer write-back write-allocate */
.equ TCR_IRGN0_WB_WA,       (1 << 8)    /* TTBR0 inner write-back write-allocate */
.equ TCR_ORGN1_WB_WA,       (1 << 26)   /* TTBR1 outer write-back write-allocate */
.equ TCR_IRGN1_WB_WA,       (1 << 24)   /* TTBR1 inner write-back write-allocate */
.equ TCR_IPS_48BIT,         (5 << 32)   /* 48-bit physical address */

/* MAIR_EL1 属性定义 */
.equ MAIR_DEVICE_nGnRnE,    0x00        /* Device-nGnRnE */
.equ MAIR_NORMAL_NC,        0x44        /* Normal Non-Cacheable */
.equ MAIR_NORMAL_WT,        0xBB        /* Normal Write-Through */
.equ MAIR_NORMAL_WB,        0xFF        /* Normal Write-Back */

/* 页表描述符标志 */
.equ DESC_VALID,            (1 << 0)
.equ DESC_TABLE,            (1 << 1)
.equ DESC_BLOCK,            (0 << 1)
.equ DESC_PAGE,             (1 << 1)
.equ DESC_AF,               (1 << 10)   /* Access Flag */
.equ DESC_SH_INNER,         (3 << 8)    /* Inner Shareable */
.equ DESC_AP_RW_EL1,        (0 << 6)    /* EL1 read-write */
.equ DESC_ATTR_IDX_WB,      (3 << 2)    /* MAIR index 3 (Write-Back) */
.equ DESC_ATTR_IDX_DEV,     (0 << 2)    /* MAIR index 0 (Device) */

/* 块描述符标志 (用于 1GB/2MB 映射) */
.equ BLOCK_NORMAL,          (DESC_VALID | DESC_BLOCK | DESC_AF | DESC_SH_INNER | DESC_AP_RW_EL1 | DESC_ATTR_IDX_WB)
.equ BLOCK_DEVICE,          (DESC_VALID | DESC_BLOCK | DESC_AF | DESC_AP_RW_EL1 | DESC_ATTR_IDX_DEV)

/* 表描述符标志 */
.equ TABLE_DESC,            (DESC_VALID | DESC_TABLE)

/* ============================================================================
 * 代码段
 * ========================================================================== */

.section .text.boot
.global _start

/*
 * _start - 内核入口点
 *
 * 从 UEFI/U-Boot 进入，预期在 EL1 或 EL2
 * 
 * 输入:
 *   x0 - DTB 物理地址 (由 bootloader 传递)
 *   x1-x3 - 保留
 */
_start:
    /* 保存 DTB 地址 (x0 contains DTB address from bootloader/QEMU) */
    mov     x19, x0
    
    /* Also save to x21 as backup (x19-x28 are callee-saved) */
    mov     x21, x0

    /* 禁用中断 */
    msr     daifset, #0xf

    /* 检查当前 Exception Level */
    mrs     x0, CurrentEL
    and     x0, x0, #CURRENTEL_EL_MASK
    
    cmp     x0, #CURRENTEL_EL3
    b.eq    el3_entry
    
    cmp     x0, #CURRENTEL_EL2
    b.eq    el2_entry
    
    cmp     x0, #CURRENTEL_EL1
    b.eq    el1_entry
    
    /* 未知 EL，停机 */
    b       halt

/*
 * el3_entry - 从 EL3 降级到 EL1
 *
 * 配置 EL3 安全状态并切换到 EL1
 */
el3_entry:
    /* 配置 SCR_EL3: 非安全状态，允许 EL2 */
    mov     x0, #0x531      /* RW=1, HCE=1, SMD=1, NS=1 */
    msr     scr_el3, x0
    
    /* 配置 SPSR_EL3: 返回到 EL1h */
    mov     x0, #0x3c5      /* D=1, A=1, I=1, F=1, M=EL1h */
    msr     spsr_el3, x0
    
    /* 设置返回地址 */
    adr     x0, el1_entry
    msr     elr_el3, x0
    
    /* 返回到 EL1 */
    eret

/*
 * el2_entry - 从 EL2 降级到 EL1
 *
 * 配置 EL2 虚拟化设置并切换到 EL1
 */
el2_entry:
    /* 配置 HCR_EL2: 允许 EL1 访问物理计数器 */
    mov     x0, #(1 << 31)  /* RW=1: EL1 is AArch64 */
    msr     hcr_el2, x0
    
    /* 配置 CNTHCTL_EL2: 允许 EL1 访问物理计时器 */
    /* EL1PCTEN=1: EL1 可以访问物理计数器 */
    /* EL1PCEN=1: EL1 可以访问物理计时器 */
    mov     x0, #3          /* EL1PCTEN | EL1PCEN */
    msr     cnthctl_el2, x0
    
    /* 配置 CNTVOFF_EL2: 虚拟计时器偏移为 0 */
    msr     cntvoff_el2, xzr
    
    /* 配置 SPSR_EL2: 返回到 EL1h */
    mov     x0, #0x3c5      /* D=1, A=1, I=1, F=1, M=EL1h */
    msr     spsr_el2, x0
    
    /* 设置返回地址 */
    adr     x0, el1_entry
    msr     elr_el2, x0
    
    /* 返回到 EL1 */
    eret

/*
 * el1_entry - EL1 初始化
 *
 * 在 EL1 中执行内核初始化
 */
el1_entry:
    /* 设置栈指针 (使用物理地址，MMU 尚未启用) */
    adrp    x0, boot_stack_top
    add     x0, x0, :lo12:boot_stack_top
    mov     sp, x0

    /* 清零 BSS 段 */
    adrp    x0, _bss_start
    add     x0, x0, :lo12:_bss_start
    adrp    x1, _bss_end
    add     x1, x1, :lo12:_bss_end
clear_bss:
    cmp     x0, x1
    b.ge    bss_done
    str     xzr, [x0], #8
    b       clear_bss
bss_done:

    /* 设置页表 */
    bl      setup_page_tables

    /* 配置 MAIR_EL1 */
    /* MAIR = 0x00_00_00_00_FF_BB_44_00 */
    /*        idx7 idx6 idx5 idx4 idx3 idx2 idx1 idx0 */
    /* Build the value: 0x0000_0000_FFBB_4400 */
    mov     x0, #0x4400         /* idx0=0x00, idx1=0x44 */
    movk    x0, #0xFFBB, lsl #16 /* idx2=0xBB, idx3=0xFF */
    msr     mair_el1, x0

    /* 配置 TCR_EL1 */
    /* T0SZ=16, T1SZ=16, TG0=4KB, TG1=4KB, SH=Inner, ORGN/IRGN=WB-WA, IPS=48bit */
    /* 
     * TCR_EL1 layout:
     * [5:0]   T0SZ = 16 (48-bit VA for TTBR0)
     * [21:16] T1SZ = 16 (48-bit VA for TTBR1)
     * [15:14] TG0 = 0 (4KB granule for TTBR0)
     * [31:30] TG1 = 2 (4KB granule for TTBR1)
     * [34:32] IPS = 5 (48-bit PA)
     * [9:8]   IRGN0 = 1 (WB-WA)
     * [11:10] ORGN0 = 1 (WB-WA)
     * [13:12] SH0 = 3 (Inner)
     * [25:24] IRGN1 = 1 (WB-WA)
     * [27:26] ORGN1 = 1 (WB-WA)
     * [29:28] SH1 = 3 (Inner)
     */
    /* Build TCR value carefully to avoid overwriting */
    mov     x0, #16             /* T0SZ = 16 */
    orr     x0, x0, #(16 << 16) /* T1SZ = 16 */
    orr     x0, x0, #(1 << 8)   /* IRGN0 = WB-WA */
    orr     x0, x0, #(1 << 10)  /* ORGN0 = WB-WA */
    orr     x0, x0, #(3 << 12)  /* SH0 = Inner */
    orr     x0, x0, #(1 << 24)  /* IRGN1 = WB-WA */
    orr     x0, x0, #(1 << 26)  /* ORGN1 = WB-WA */
    orr     x0, x0, #(3 << 28)  /* SH1 = Inner */
    /* TG1 = 2 (bits 31:30) - use orr instead of movk to preserve other bits */
    orr     x0, x0, #(2 << 30)  /* TG1 = 4KB */
    /* IPS = 5 (bits 34:32) - 48-bit physical address */
    mov     x1, #(5 << 0)       /* IPS value */
    lsl     x1, x1, #32         /* Shift to bits 34:32 */
    orr     x0, x0, x1          /* Combine */
    msr     tcr_el1, x0

    /* 加载页表基址 */
    adrp    x0, boot_l0_table
    add     x0, x0, :lo12:boot_l0_table
    msr     ttbr0_el1, x0       /* 用户空间页表 (恒等映射) */
    msr     ttbr1_el1, x0       /* 内核空间页表 */

    /* 同步屏障 */
    isb

    /* 启用 MMU */
    mrs     x0, sctlr_el1
    orr     x0, x0, #SCTLR_M    /* MMU enable */
    orr     x0, x0, #SCTLR_C    /* Data cache enable */
    orr     x0, x0, #SCTLR_I    /* Instruction cache enable */
    bic     x0, x0, #SCTLR_A    /* Disable alignment check */
    msr     sctlr_el1, x0

    /* 同步屏障 */
    isb

    /* 跳转到内核主函数 (暂时不使用高半核) */
    /* 设置栈 */
    adrp    x0, stack_top
    add     x0, x0, :lo12:stack_top
    mov     sp, x0

    /* 准备 kernel_main 参数 */
    /* Use x19 (primary) or x21 (backup) for DTB address */
    mov     x0, x19             /* DTB 地址 */
    cbnz    x0, 1f              /* If x19 != 0, use it */
    mov     x0, x21             /* Otherwise try x21 backup */
1:

    /* 调用 kernel_main */
    bl      kernel_main

    /* kernel_main 不应返回 */
halt:
    wfi
    b       halt

/* ============================================================================
 * 页表设置
 * ========================================================================== */

/*
 * setup_page_tables - 设置初始页表
 *
 * 创建恒等映射和高半核映射:
 *   - 恒等映射: 0x00000000_00000000 - 0x00000000_FFFFFFFF (前 4GB)
 *   - 高半核映射: 0xFFFF0000_00000000 - 0xFFFF0000_FFFFFFFF
 *
 * 使用 1GB 块映射简化初始设置
 */
setup_page_tables:
    /* 保存返回地址 */
    mov     x20, x30

    /* 清零页表 */
    adr     x0, boot_l0_table
    mov     x1, #(PAGE_SIZE * 4)    /* L0 + L1 tables */
    bl      zero_memory

    /* 获取页表地址 */
    adr     x0, boot_l0_table
    adr     x1, boot_l1_table_low
    adr     x2, boot_l1_table_high

    /* L0[0] -> L1 低地址表 (恒等映射) */
    orr     x3, x1, #TABLE_DESC
    str     x3, [x0, #0]

    /* L0[0] -> L1 高地址表 (内核映射) */
    /* 
     * 对于 TTBR1 (内核空间), 地址 0xFFFF_0000_xxxx_xxxx 的 L0 索引是:
     * bits [47:39] = 0 (因为 0xFFFF_0000 的 bit 47 = 0)
     * 所以 L0[0] 用于内核映射
     * 
     * 注意: TTBR0 和 TTBR1 使用相同的 L0 表，但 TTBR0 用于用户空间 (0x0000_xxxx)
     * TTBR1 用于内核空间 (0xFFFF_xxxx)。由于两者的 L0 索引相同 (都是 0)，
     * 我们需要确保 L0[0] 的 L1 表同时支持恒等映射和高半核映射。
     * 
     * 当前设计: L0[0] -> boot_l1_table_low (恒等映射)
     * 这意味着 0xFFFF_0000_4xxx_xxxx 会映射到物理 0x4xxx_xxxx，这正是我们需要的。
     */
    /* 实际上不需要单独的高地址表，因为恒等映射已经覆盖了所需的物理地址 */
    /* 保留此代码以备将来需要分离用户/内核页表时使用 */
    /* orr     x3, x2, #TABLE_DESC */
    /* str     x3, [x0, #0] */

    /* 设置 L1 低地址表: 恒等映射前 4GB */
    /* L1[0] = 0x00000000 (1GB block) */
    ldr     x3, =BLOCK_DEVICE       /* 设备内存 (前 1GB 包含 MMIO) */
    str     x3, [x1, #0]

    /* L1[1] = 0x40000000 (1GB block, 包含 RAM) */
    ldr     x3, =(0x40000000 | BLOCK_NORMAL)
    str     x3, [x1, #8]

    /* L1[2] = 0x80000000 (1GB block) */
    ldr     x3, =(0x80000000 | BLOCK_NORMAL)
    str     x3, [x1, #16]

    /* L1[3] = 0xC0000000 (1GB block) */
    ldr     x3, =(0xC0000000 | BLOCK_NORMAL)
    str     x3, [x1, #24]

    /* 设置 L1 高地址表: 内核映射 */
    /* 映射与低地址相同的物理内存 */
    /* L1[0] = 0x00000000 (1GB block) */
    ldr     x3, =BLOCK_DEVICE
    str     x3, [x2, #0]

    /* L1[1] = 0x40000000 (1GB block, 包含 RAM) */
    ldr     x3, =(0x40000000 | BLOCK_NORMAL)
    str     x3, [x2, #8]

    /* L1[2] = 0x80000000 (1GB block) */
    ldr     x3, =(0x80000000 | BLOCK_NORMAL)
    str     x3, [x2, #16]

    /* L1[3] = 0xC0000000 (1GB block) */
    ldr     x3, =(0xC0000000 | BLOCK_NORMAL)
    str     x3, [x2, #24]

    /* 数据同步屏障 */
    dsb     sy
    isb

    /* 恢复返回地址并返回 */
    mov     x30, x20
    ret

/*
 * zero_memory - 清零内存区域
 *
 * 输入:
 *   x0 - 起始地址
 *   x1 - 字节数
 */
zero_memory:
    cbz     x1, 2f
1:
    str     xzr, [x0], #8
    subs    x1, x1, #8
    b.gt    1b
2:
    ret

/* ============================================================================
 * 数据段
 * ========================================================================== */

.section .data

/* 导出页表地址供 C 代码使用 */
.global boot_page_directory
boot_page_directory:
    .quad   boot_l0_table

/* ============================================================================
 * BSS 段 - 页表和栈
 * ========================================================================== */

.section .bss
.align 12   /* 4KB 对齐 */

/* 引导页表 */
.global boot_l0_table
boot_l0_table:
    .space  PAGE_SIZE

.global boot_l1_table_low
boot_l1_table_low:
    .space  PAGE_SIZE

.global boot_l1_table_high
boot_l1_table_high:
    .space  PAGE_SIZE

/* 预留额外页表空间 */
boot_l2_table:
    .space  PAGE_SIZE

/* 引导栈 (物理地址使用) */
.align 16
boot_stack_bottom:
    .space  STACK_SIZE
boot_stack_top:

/* 内核栈 (虚拟地址使用) */
.align 16
.global stack_bottom
stack_bottom:
    .space  STACK_SIZE
.global stack_top
stack_top:
