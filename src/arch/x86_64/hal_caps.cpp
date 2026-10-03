/**
 * @file hal_caps.c
 * @brief x86_64 HAL 能力查询实现
 * 
 * 实现 x86_64 架构的 HAL 能力查询接口。
 * 
 * @see Requirements 1.1, 1.2, 1.3, 1.4
 */

#include <hal/hal_caps.h>
#include <hal/hal_error.h>

/* x86_64 context structure size: 168 bytes
 * (15 GPRs + int_no + err_code + 5 interrupt frame regs) * 8 bytes
 */
#define X86_64_CONTEXT_SIZE   168

/**
 * @brief 获取 x86_64 HAL 能力信息
 * 
 * @param[out] caps 指向要填充的能力结构体的指针
 * 
 * @see Requirements 1.1, 1.3
 */
void hal_get_capabilities(hal_capabilities_t *caps) {
    if (!caps) {
        return;
    }
    
    /* ---- 硬件特性 ---- */
    caps->has_huge_pages = true;        /* x86_64 支持 2MB/1GB 大页 */
    caps->has_nx_bit = true;            /* x86_64 支持 NX 位 */
    caps->has_port_io = true;           /* x86 支持端口 I/O */
    caps->cache_coherent_dma = true;    /* x86 DMA 缓存一致 */
    caps->has_iommu = false;            /* 需要检测 VT-d/AMD-Vi */
    caps->has_smp = false;              /* 当前实现为单核 */
    caps->has_fpu = true;               /* x86_64 必须有 FPU */
    caps->has_simd = true;              /* x86_64 必须有 SSE2 */
    
    /* ---- 页表配置 ---- */
    caps->page_table_levels = 4;        /* PML4 -> PDPT -> PD -> PT */
    caps->page_sizes[0] = 4096;         /* 4KB 页 */
    caps->page_sizes[1] = 2 * 1024 * 1024;  /* 2MB 大页 */
    caps->page_sizes[2] = 1024 * 1024 * 1024; /* 1GB 巨页 */
    caps->page_size_count = 3;
    
    /* ---- 地址空间限制 ---- */
    caps->phys_addr_bits = 48;          /* 48 位物理地址 (典型) */
    caps->virt_addr_bits = 48;          /* 48 位虚拟地址 */
    caps->phys_addr_max = 0x0000FFFFFFFFFFFFULL;
    caps->virt_addr_max = 0xFFFFFFFFFFFFFFFFULL;
    caps->kernel_base = 0xFFFF800000000000ULL;  /* 高半内核 */
    caps->user_space_end = 0x00007FFFFFFFFFFFULL;
    
    /* ---- 寄存器信息 ---- */
    caps->gpr_count = 16;               /* RAX-R15 */
    caps->gpr_size = 8;                 /* 64 位寄存器 */
    caps->context_size = X86_64_CONTEXT_SIZE;
    
    /* ---- 架构标识 ---- */
    caps->arch_name = "x86_64";
    caps->arch_bits = 64;
}

/**
 * @brief 检查特定能力是否支持
 * 
 * @param cap 能力标识符
 * @return true 如果当前架构支持该能力
 * 
 * @see Requirements 1.2
 */
bool hal_has_capability(hal_cap_id_t cap) {
    switch (cap) {
        case HAL_CAP_HUGE_PAGES:
            return true;
        case HAL_CAP_NX_BIT:
            return true;
        case HAL_CAP_PORT_IO:
            return true;
        case HAL_CAP_CACHE_COHERENT_DMA:
            return true;
        case HAL_CAP_IOMMU:
            return false;  /* 需要运行时检测 */
        case HAL_CAP_SMP:
            return false;  /* 当前实现为单核 */
        case HAL_CAP_FPU:
            return true;
        case HAL_CAP_SIMD:
            return true;
        default:
            return false;
    }
}

/**
 * @brief 获取大页大小
 * 
 * @return 2MB (2 * 1024 * 1024)
 */
uint32_t hal_get_huge_page_size(void) {
    return 2 * 1024 * 1024;  /* 2MB */
}

/* Note: hal_arch_name() is defined in task/context64.c */
