/**
 * @file dma.h
 * @brief DMA Cache Coherency Interface
 * 
 * This header defines the DMA cache coherency interface for ensuring proper
 * data consistency between CPU caches and DMA-capable devices.
 * 
 * On architectures with non-coherent caches (primarily ARM64), explicit cache
 * maintenance operations are required before and after DMA transfers:
 * 
 *   - Before DMA read (device reads from memory):
 *     Call hal_dma_sync_for_device() to clean caches, ensuring device sees
 *     the latest CPU-written data.
 * 
 *   - After DMA write (device writes to memory):
 *     Call hal_dma_sync_for_cpu() to invalidate caches, ensuring CPU sees
 *     the data written by the device.
 * 
 * On x86/x86_64, caches are DMA-coherent (snooped), so these operations are
 * no-ops but should still be called for portability.
 * 
 * @see Requirements 9.4
 */

#ifndef _HAL_DMA_H_
#define _HAL_DMA_H_

#include <types.h>
#include <hal/hal.h>

/* ============================================================================
 * DMA Direction Flags
 * ========================================================================== */

/**
 * @brief DMA transfer direction
 * 
 * Specifies the direction of DMA data transfer, which determines what
 * cache maintenance operations are needed.
 */
typedef enum {
    /** Device reads from memory (CPU -> Device) */
    DMA_TO_DEVICE = 0,
    
    /** Device writes to memory (Device -> CPU) */
    DMA_FROM_DEVICE = 1,
    
    /** Bidirectional transfer (both directions) */
    DMA_BIDIRECTIONAL = 2
} dma_direction_t;

/* ============================================================================
 * DMA Buffer Synchronization
 * ========================================================================== */

/**
 * @brief Prepare a buffer for DMA access by the device
 * 
 * This function should be called before initiating a DMA transfer to ensure
 * the device sees consistent data:
 * 
 *   - DMA_TO_DEVICE: Cleans cache lines to ensure device reads latest data
 *   - DMA_FROM_DEVICE: Invalidates cache lines to prepare for device writes
 *   - DMA_BIDIRECTIONAL: Cleans and invalidates cache lines
 * 
 * @param addr Virtual address of the DMA buffer
 * @param size Size of the buffer in bytes
 * @param direction Direction of the DMA transfer
 * 
 * @note On x86/x86_64, this is a no-op as caches are DMA-coherent.
 * @note On ARM64, this performs appropriate DC operations.
 * 
 * @see Requirements 9.4
 */
static inline void hal_dma_sync_for_device(void *addr, size_t size, 
                                            dma_direction_t direction) {
    switch (direction) {
        case DMA_TO_DEVICE:
            /* Device will read: clean cache to ensure device sees latest data */
            hal_cache_clean(addr, size);
            break;
            
        case DMA_FROM_DEVICE:
            /* Device will write: invalidate cache to discard stale data */
            hal_cache_invalidate(addr, size);
            break;
            
        case DMA_BIDIRECTIONAL:
            /* Both directions: clean and invalidate */
            hal_cache_clean_invalidate(addr, size);
            break;
    }
}

/**
 * @brief Synchronize a buffer after DMA access by the device
 * 
 * This function should be called after a DMA transfer completes to ensure
 * the CPU sees consistent data:
 * 
 *   - DMA_TO_DEVICE: No action needed (device only read)
 *   - DMA_FROM_DEVICE: Invalidates cache lines to see device-written data
 *   - DMA_BIDIRECTIONAL: Invalidates cache lines
 * 
 * @param addr Virtual address of the DMA buffer
 * @param size Size of the buffer in bytes
 * @param direction Direction of the DMA transfer
 * 
 * @note On x86/x86_64, this is a no-op as caches are DMA-coherent.
 * @note On ARM64, this performs appropriate DC operations.
 * 
 * @see Requirements 9.4
 */
static inline void hal_dma_sync_for_cpu(void *addr, size_t size,
                                         dma_direction_t direction) {
    switch (direction) {
        case DMA_TO_DEVICE:
            /* Device only read: no action needed */
            break;
            
        case DMA_FROM_DEVICE:
        case DMA_BIDIRECTIONAL:
            /* Device wrote: invalidate cache to see new data */
            hal_cache_invalidate(addr, size);
            break;
    }
}

/* ============================================================================
 * DMA Buffer Allocation (Optional Helpers)
 * ========================================================================== */

/**
 * @brief Check if DMA cache coherency operations are needed
 * 
 * Returns true if the current architecture requires explicit cache
 * maintenance for DMA operations.
 * 
 * @return true if cache maintenance is needed, false if hardware-coherent
 * 
 * @note This can be used to optimize code paths on coherent architectures.
 */
static inline bool hal_dma_needs_cache_ops(void) {
#if defined(ARCH_ARM64)
    return true;   /* ARM64 has non-coherent DMA by default */
#else
    return false;  /* x86/x86_64 have coherent DMA */
#endif
}

/**
 * @brief Get the cache line size for DMA alignment
 * 
 * Returns the CPU cache line size, which is the minimum alignment
 * recommended for DMA buffers to avoid cache line sharing issues.
 * 
 * @return Cache line size in bytes
 */
static inline size_t hal_dma_cache_line_size(void) {
#if defined(ARCH_ARM64)
    /* ARM64 typically has 64-byte cache lines */
    return 64;
#elif defined(ARCH_X86_64)
    /* x86_64 typically has 64-byte cache lines */
    return 64;
#elif defined(ARCH_I686)
    /* i686 typically has 32 or 64-byte cache lines */
    return 64;
#else
    /* Default to 64 bytes */
    return 64;
#endif
}

/**
 * @brief Align a size up to cache line boundary
 * 
 * Rounds up a size to the next cache line boundary, useful for
 * allocating DMA buffers.
 * 
 * @param size Size to align
 * @return Size aligned up to cache line boundary
 */
static inline size_t hal_dma_align_size(size_t size) {
    size_t line_size = hal_dma_cache_line_size();
    return (size + line_size - 1) & ~(line_size - 1);
}

/**
 * @brief Check if an address is cache-line aligned
 * 
 * @param addr Address to check
 * @return true if address is cache-line aligned
 */
static inline bool hal_dma_is_aligned(void *addr) {
    size_t line_size = hal_dma_cache_line_size();
    return ((uintptr_t)addr & (line_size - 1)) == 0;
}

#endif /* _HAL_DMA_H_ */
