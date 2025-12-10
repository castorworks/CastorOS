/**
 * @file hal_caps.c
 * @brief ARM64 HAL 能力查询实现
 * 
 * 实现 ARM64 架构的 HAL 能力查询接口。
 * 
 * @see Requirements 1.1, 1.2, 1.3, 1.4
 */

#include <hal/hal_caps.h>
#include <hal/hal_error.h>

/* ARM64 context structure size: 296 bytes
 * 31 GPRs (X0-X30) + SP + PC + PSTATE + TTBR0 + ESR + FAR = 37 * 8 bytes
 */
#define ARM64_CONTEXT_SIZE   296

/**
 * @brief 获取 ARM64 HAL 能力信息
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
    caps->has_huge_pages = true;        /* ARM64 支持 2MB/1GB 块 */
    caps->has_nx_bit = true;            /* ARM64 支持 PXN/UXN */
    caps->has_port_io = false;          /* ARM 无端口 I/O，使用 MMIO */
    caps->cache_coherent_dma = false;   /* ARM64 需要显式缓存维护 */
    caps->has_iommu = false;            /* 需要检测 SMMU */
    caps->has_smp = false;              /* 当前实现为单核 */
    caps->has_fpu = true;               /* ARM64 必须有 FPU */
    caps->has_simd = true;              /* ARM64 必须有 NEON */
    
    /* ---- 页表配置 ---- */
    caps->page_table_levels = 4;        /* L0 -> L1 -> L2 -> L3 (4KB granule) */
    caps->page_sizes[0] = 4096;         /* 4KB 页 */
    caps->page_sizes[1] = 2 * 1024 * 1024;  /* 2MB 块 (L2) */
    caps->page_sizes[2] = 1024 * 1024 * 1024; /* 1GB 块 (L1) */
    caps->page_size_count = 3;
    
    /* ---- 地址空间限制 ---- */
    caps->phys_addr_bits = 48;          /* 48 位物理地址 (典型) */
    caps->virt_addr_bits = 48;          /* 48 位虚拟地址 */
    caps->phys_addr_max = 0x0000FFFFFFFFFFFFULL;
    caps->virt_addr_max = 0xFFFFFFFFFFFFFFFFULL;
    caps->kernel_base = 0xFFFF000000000000ULL;  /* TTBR1 区域 */
    caps->user_space_end = 0x0000FFFFFFFFFFFFULL; /* TTBR0 区域 */
    
    /* ---- 寄存器信息 ---- */
    caps->gpr_count = 31;               /* X0-X30 */
    caps->gpr_size = 8;                 /* 64 位寄存器 */
    caps->context_size = ARM64_CONTEXT_SIZE;
    
    /* ---- 架构标识 ---- */
    caps->arch_name = "arm64";
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
            return false;  /* ARM 无端口 I/O */
        case HAL_CAP_CACHE_COHERENT_DMA:
            return false;  /* 需要显式缓存维护 */
        case HAL_CAP_IOMMU:
            return false;  /* 需要运行时检测 SMMU */
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

/* Note: hal_arch_name() is already defined in hal.c for ARM64 */
