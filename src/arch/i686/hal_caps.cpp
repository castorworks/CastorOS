/**
 * @file hal_caps.c
 * @brief i686 HAL 能力查询实现
 * 
 * 实现 i686 架构的 HAL 能力查询接口。
 * 
 * @see Requirements 1.1, 1.2, 1.3, 1.4
 */

#include <hal/hal_caps.h>
#include <hal/hal_error.h>

/* i686 context structure size: 72 bytes
 * (4 segment regs + 8 GPRs + int_no + err_code + 5 interrupt frame regs) * 4 bytes
 */
#define I686_CONTEXT_SIZE   72

/**
 * @brief 获取 i686 HAL 能力信息
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
    caps->has_huge_pages = false;       /* i686 不支持大页 (无 PAE 模式) */
    caps->has_nx_bit = false;           /* i686 不支持 NX 位 (需要 PAE) */
    caps->has_port_io = true;           /* x86 支持端口 I/O */
    caps->cache_coherent_dma = true;    /* x86 DMA 缓存一致 */
    caps->has_iommu = false;            /* 基本 i686 无 IOMMU */
    caps->has_smp = false;              /* 当前实现为单核 */
    caps->has_fpu = true;               /* 假设有 FPU */
    caps->has_simd = false;             /* 不假设 SSE 支持 */
    
    /* ---- 页表配置 ---- */
    caps->page_table_levels = 2;        /* 页目录 + 页表 */
    caps->page_sizes[0] = 4096;         /* 4KB 页 */
    caps->page_size_count = 1;          /* 只支持 4KB 页 */
    
    /* ---- 地址空间限制 ---- */
    caps->phys_addr_bits = 32;          /* 32 位物理地址 */
    caps->virt_addr_bits = 32;          /* 32 位虚拟地址 */
    caps->phys_addr_max = 0xFFFFFFFFULL;
    caps->virt_addr_max = 0xFFFFFFFFULL;
    caps->kernel_base = 0x80000000ULL;  /* 2GB 内核基址 */
    caps->user_space_end = 0x80000000ULL;
    
    /* ---- 寄存器信息 ---- */
    caps->gpr_count = 8;                /* EAX-EDI */
    caps->gpr_size = 4;                 /* 32 位寄存器 */
    caps->context_size = I686_CONTEXT_SIZE;
    
    /* ---- 架构标识 ---- */
    caps->arch_name = "i686";
    caps->arch_bits = 32;
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
            return false;
        case HAL_CAP_NX_BIT:
            return false;
        case HAL_CAP_PORT_IO:
            return true;
        case HAL_CAP_CACHE_COHERENT_DMA:
            return true;
        case HAL_CAP_IOMMU:
            return false;
        case HAL_CAP_SMP:
            return false;
        case HAL_CAP_FPU:
            return true;
        case HAL_CAP_SIMD:
            return false;
        default:
            return false;
    }
}

/**
 * @brief 获取大页大小
 * 
 * @return 0 (i686 不支持大页)
 */
uint32_t hal_get_huge_page_size(void) {
    return 0;  /* i686 不支持大页 */
}

/* Note: hal_arch_name() is defined in task/context.c */
