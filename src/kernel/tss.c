// ============================================================================
// tss.c - 任务状态段实现
// ============================================================================

#include <kernel/tss.h>
#include <kernel/gdt.h>
#include <lib/string.h>
#include <lib/klog.h>

/* 全局 TSS */
static tss_entry_t tss;

/**
 * 初始化 TSS
 */
void tss_init(uint32_t kernel_stack, uint32_t kernel_ss) {
    LOG_INFO_MSG("Initializing TSS...\n");
    
    /* 清零 TSS */
    memset(&tss, 0, sizeof(tss_entry_t));
    
    /* 设置内核栈 */
    tss.ss0 = kernel_ss;
    tss.esp0 = kernel_stack;
    
    /* 设置 I/O 位图基址（没有 I/O 位图） */
    tss.iomap_base = sizeof(tss_entry_t);
    
    /* 
     * 注意：TSS 中的段寄存器字段（cs, ss, ds 等）仅在硬件任务切换时使用
     * CastorOS 使用软件任务切换，这些字段不需要设置（memset 已清零）
     * 
     * 如果未来需要硬件任务切换支持，应该设置为内核段（不加 RPL）：
     *   tss.cs = GDT_KERNEL_CODE_SEGMENT;     // 0x08
     *   tss.ss = tss.ds = tss.es = tss.fs = tss.gs = GDT_KERNEL_DATA_SEGMENT;  // 0x10
     * 
     * ⚠️ 错误：不要设置 RPL=3 (如 0x0b, 0x13)，这在硬件任务切换时会导致特权级错误
     */
    
    LOG_DEBUG_MSG("  TSS address: %x\n", (uint32_t)&tss);
    LOG_DEBUG_MSG("  TSS size: %zu bytes\n", sizeof(tss_entry_t));
    LOG_DEBUG_MSG("  Kernel stack (SS0:ESP0): %x:%x\n", tss.ss0, tss.esp0);
    
    LOG_INFO_MSG("TSS initialized successfully\n");
}

/**
 * 加载 TSS 到 TR 寄存器
 * 必须在 gdt_add_tss_descriptor() 之后调用
 */
void tss_load(uint16_t tss_selector) {
    tss_flush(tss_selector);
    LOG_DEBUG_MSG("TSS loaded (selector: %x)\n", tss_selector);
}

/**
 * 设置内核栈
 */
void tss_set_kernel_stack(uint32_t kernel_stack) {
    tss.esp0 = kernel_stack;
}

/**
 * 获取 TSS 结构地址
 */
uint32_t tss_get_address(void) {
    return (uint32_t)&tss;
}

/**
 * 获取 TSS 大小
 */
uint32_t tss_get_size(void) {
    return sizeof(tss_entry_t);
}
