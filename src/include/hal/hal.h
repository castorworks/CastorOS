/**
 * @file hal.h
 * @brief Hardware Abstraction Layer (HAL) Interface
 * 
 * This header defines the unified HAL interface that abstracts architecture-specific
 * code. All architecture-specific implementations must implement these interfaces.
 * 
 * Supported architectures:
 *   - i686 (x86 32-bit)
 *   - x86_64 (AMD64/Intel 64-bit)
 *   - arm64 (AArch64)
 */

#ifndef _HAL_HAL_H_
#define _HAL_HAL_H_

#include <types.h>
#include <mm/mm_types.h>

/* ============================================================================
 * Forward Declarations
 * ========================================================================== */

/**
 * @brief Architecture-specific CPU context structure
 * 
 * This structure is defined differently for each architecture:
 *   - i686: 32-bit registers (EAX-EDI, EIP, EFLAGS, etc.)
 *   - x86_64: 64-bit registers (RAX-R15, RIP, RFLAGS, etc.)
 *   - arm64: ARM64 registers (X0-X30, SP, PC, PSTATE, etc.)
 */
typedef struct hal_context hal_context_t;

/**
 * @brief Interrupt handler function type
 * @param data User-provided data pointer
 */
typedef void (*hal_interrupt_handler_t)(void *data);

/** Page table entry flags (architecture-independent) */
#define HAL_PAGE_PRESENT    (1 << 0)   /**< Page is present in memory */
#define HAL_PAGE_WRITE      (1 << 1)   /**< Page is writable */
#define HAL_PAGE_USER       (1 << 2)   /**< Page is accessible from user mode */
#define HAL_PAGE_NOCACHE    (1 << 3)   /**< Disable caching for this page */
#define HAL_PAGE_EXEC       (1 << 4)   /**< Page is executable */
#define HAL_PAGE_COW        (1 << 5)   /**< Copy-on-Write flag */
#define HAL_PAGE_DIRTY      (1 << 6)   /**< Page has been modified */
#define HAL_PAGE_ACCESSED   (1 << 7)   /**< Page has been accessed */
#define HAL_PAGE_WRITECOMB  (1 << 8)   /**< Write-combining memory type */
#define HAL_PAGE_HUGE       (1 << 9)   /**< Huge page (2MB on x86_64, 2MB block on ARM64) */
#define HAL_PAGE_SHARED     (1 << 10)  /**< Shared mapping: fork shares the frame instead of making it COW */

/**
 * @brief Address space handle type
 * 
 * Represents an address space (page table hierarchy). The handle is the
 * physical address of the top-level page table:
 *   - i686: Page Directory (CR3)
 *   - x86_64: PML4 (CR3)
 *   - ARM64: Level 0 table (TTBR0_EL1/TTBR1_EL1)
 */
typedef paddr_t hal_addr_space_t;

/** @brief Invalid address space handle */
#define HAL_ADDR_SPACE_INVALID  PADDR_INVALID

/** @brief Use current address space (for hal_mmu_map/unmap/query/protect) */
#define HAL_ADDR_SPACE_CURRENT  ((hal_addr_space_t)0)

/**
 * @brief Page fault information structure
 * 
 * Architecture-independent representation of page fault details.
 * Filled by hal_mmu_parse_fault() from architecture-specific fault registers.
 */
typedef struct hal_page_fault_info {
    vaddr_t fault_addr;     /**< Virtual address that caused the fault */
    bool is_present;        /**< Page was present (protection fault vs not-present) */
    bool is_write;          /**< Fault was caused by a write operation */
    bool is_user;           /**< Fault occurred in user mode */
    bool is_exec;           /**< Fault was caused by instruction fetch */
    bool is_reserved;       /**< Fault was caused by reserved bit violation */
    uint32_t raw_error;     /**< Architecture-specific raw error code */
} hal_page_fault_info_t;

/** @brief Huge page size (2MB) */
#define HAL_HUGE_PAGE_SIZE      (2 * 1024 * 1024)

/** @brief Huge page alignment mask */
#define HAL_HUGE_PAGE_MASK      (~((vaddr_t)HAL_HUGE_PAGE_SIZE - 1))

/**
 * @brief System call handler function type
 * @param syscall_num System call number
 * @param arg1-arg6 System call arguments
 * @return System call return value
 */
typedef int64_t (*hal_syscall_handler_t)(uint32_t syscall_num,
                                          uint64_t arg1, uint64_t arg2,
                                          uint64_t arg3, uint64_t arg4,
                                          uint64_t arg5, uint64_t arg6);

/**
 * @brief Timer callback function type
 */
typedef void (*hal_timer_callback_t)(void);

/**
 * @brief Full memory barrier (read and write)
 */
static inline void hal_memory_barrier(void) {
#if defined(ARCH_ARM64)
    __asm__ volatile("dmb sy" ::: "memory");
#elif defined(ARCH_X86_64) || defined(ARCH_I686)
    __asm__ volatile("mfence" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

/**
 * @brief Read memory barrier
 */
static inline void hal_read_barrier(void) {
#if defined(ARCH_ARM64)
    __asm__ volatile("dmb ld" ::: "memory");
#elif defined(ARCH_X86_64) || defined(ARCH_I686)
    __asm__ volatile("lfence" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

/**
 * @brief Write memory barrier
 */
static inline void hal_write_barrier(void) {
#if defined(ARCH_ARM64)
    __asm__ volatile("dmb st" ::: "memory");
#elif defined(ARCH_X86_64) || defined(ARCH_I686)
    __asm__ volatile("sfence" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

/**
 * @brief Instruction synchronization barrier (ARM64) / serialize (x86)
 */
static inline void hal_instruction_barrier(void) {
#if defined(ARCH_ARM64)
    __asm__ volatile("isb" ::: "memory");
#elif defined(ARCH_X86_64) || defined(ARCH_I686)
    __asm__ volatile("" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

#if defined(ARCH_I686) || defined(ARCH_X86_64)

#endif /* ARCH_I686 || ARCH_X86_64 */

/**
 * @brief Get architecture name string
 * @return Architecture name (e.g., "i686", "x86_64", "arm64")
 */
const char *hal_arch_name(void);

/**
 * @brief Get pointer size for current architecture
 * @return Pointer size in bytes (4 for 32-bit, 8 for 64-bit)
 */
static inline size_t hal_pointer_size(void) {
    return sizeof(void *);
}

/**
 * @brief Check if running on 64-bit architecture
 * @return true if 64-bit, false if 32-bit
 */
static inline bool hal_is_64bit(void) {
#if defined(ARCH_X86_64) || defined(ARCH_ARM64)
    return true;
#else
    return false;
#endif
}

namespace hal {

/**
 * @brief CPU 初始化与控制
 */
class Cpu {
public:
    /* ============================================================================
     * CPU Initialization
     * ========================================================================== */

    /**
     * @brief Initialize CPU architecture-specific features
     * 
     * This function initializes architecture-specific CPU features:
     *   - i686: GDT, TSS
     *   - x86_64: GDT64, TSS64
     *   - arm64: Exception Level configuration
     */
    static void init();

    /**
     * @brief Halt the CPU
     * 
     * Puts the CPU into a low-power state until the next interrupt.
     */
    static void halt();

    /**
     * @brief 空闲等待：调用时中断必须已关闭；等到有中断到来，返回时中断已打开
     *
     * “打开中断”和“停下来等”是一个原子步骤：调用之前就已经挂起的中断
     * 不会被错过（它会立刻把 CPU 唤醒），所以调用者可以先关中断检查
     * “有没有事可做”，没有再调用它，中间没有空档。
     */
    static void idle();

    /* ============================================================================
     * Interrupt Management
     * ========================================================================== */

    /* ============================================================================
     * Memory Management Unit (MMU)
     * 
     * Extended HAL MMU interface providing architecture-independent page table
     * operations, address space management, and page fault handling.
     * 
     * ========================================================================== */

    /*----------------------------------------------------------------------------
     * Address Space Handle
     *----------------------------------------------------------------------------*/

    /*----------------------------------------------------------------------------
     * Page Fault Information
     *----------------------------------------------------------------------------*/

    /*----------------------------------------------------------------------------
     * MMU Initialization
     *----------------------------------------------------------------------------*/

    /*----------------------------------------------------------------------------
     * Page Mapping Operations
     *----------------------------------------------------------------------------*/

    /*----------------------------------------------------------------------------
     * Huge Page Mapping Operations (2MB pages)
     *----------------------------------------------------------------------------*/

    /*----------------------------------------------------------------------------
     * TLB Management
     *----------------------------------------------------------------------------*/

    /*----------------------------------------------------------------------------
     * Address Space Management
     *----------------------------------------------------------------------------*/

    /*----------------------------------------------------------------------------
     * Page Fault Handling
     *----------------------------------------------------------------------------*/

    /*----------------------------------------------------------------------------
     * Address Translation (Legacy/Convenience)
     *----------------------------------------------------------------------------*/

    /* ============================================================================
     * Context Switch
     * ========================================================================== */

    /* ============================================================================
     * System Call Interface
     * ========================================================================== */

    /* ============================================================================
     * Timer
     * ========================================================================== */

    /* ============================================================================
     * I/O Operations
     * ========================================================================== */

    /* ============================================================================
     * Cache Maintenance Operations (DMA Support)
     * 
     * These functions are required for DMA operations on architectures with
     * non-coherent caches (primarily ARM64). On x86, caches are typically
     * coherent with DMA, so these are no-ops.
     * 
     * ========================================================================== */

    /* ============================================================================
     * Memory Barriers
     * ========================================================================== */

    /* ============================================================================
     * Port I/O (x86 only)
     * ========================================================================== */

    /* ============================================================================
     * Architecture Information
     * ========================================================================== */

    /* ============================================================================
     * HAL Initialization State Query
     * ========================================================================== */

};

} // namespace hal

namespace hal {

/**
 * @brief MMU / 页表操作
 */
class Mmu {
public:
    /**
     * @brief Initialize MMU/paging
     * 
     * Initializes architecture-specific MMU configuration:
     *   - i686: Enable paging, set up initial page tables
     *   - x86_64: Configure 4-level paging
     *   - ARM64: Configure TCR_EL1, MAIR_EL1, enable MMU
     */
    static void init();

    /**
     * @brief Create a page table mapping
     * 
     * Maps a virtual address to a physical address with specified flags.
     * Allocates intermediate page table levels as needed.
     * 
     * @param space Address space handle (HAL_ADDR_SPACE_CURRENT for current)
     * @param virt Virtual address (must be page-aligned)
     * @param phys Physical address (must be page-aligned)
     * @param flags Page flags (HAL_PAGE_*)
     * @return true on success, false on failure (e.g., out of memory)
     * 
     * @note This function does NOT flush the TLB. Caller must call
     *       hal_mmu_flush_tlb() if the mapping is for the current address space.
     */
    static bool map(hal_addr_space_t space, vaddr_t virt, paddr_t phys, uint32_t flags);

    /**
     * @brief Remove a page table mapping
     * 
     * Unmaps a virtual address and returns the previously mapped physical address.
     * Does NOT free intermediate page table levels.
     * 
     * @param space Address space handle (HAL_ADDR_SPACE_CURRENT for current)
     * @param virt Virtual address to unmap
     * @return Previously mapped physical address, or PADDR_INVALID if not mapped
     * 
     * @note This function does NOT flush the TLB. Caller must call
     *       hal_mmu_flush_tlb() if the mapping was for the current address space.
     */
    static paddr_t unmap(hal_addr_space_t space, vaddr_t virt);

    /**
     * @brief Query page table mapping
     * 
     * Retrieves the physical address and flags for a virtual address mapping.
     * 
     * @param space Address space handle (HAL_ADDR_SPACE_CURRENT for current)
     * @param virt Virtual address to query
     * @param[out] phys Pointer to store physical address (can be NULL)
     * @param[out] flags Pointer to store page flags (can be NULL)
     * @return true if mapping exists, false if not mapped
     */
    static bool query(hal_addr_space_t space, vaddr_t virt, paddr_t *phys, uint32_t *flags);

    /**
     * @brief Modify page table entry flags
     * 
     * Changes the flags of an existing mapping without changing the physical address.
     * Useful for implementing COW (clearing write flag) and protection changes.
     * 
     * @param space Address space handle (HAL_ADDR_SPACE_CURRENT for current)
     * @param virt Virtual address of the mapping to modify
     * @param set_flags Flags to set (OR'd into existing flags)
     * @param clear_flags Flags to clear (AND'd out of existing flags)
     * @return true on success, false if mapping doesn't exist
     * 
     * @note This function does NOT flush the TLB. Caller must call
     *       hal_mmu_flush_tlb() after modifying mappings.
     */
    static bool protect(hal_addr_space_t space, vaddr_t virt, 
                         uint32_t set_flags, uint32_t clear_flags);

    /**
     * @brief Map a 2MB huge page
     * 
     * Creates a 2MB huge page mapping. Both virtual and physical addresses
     * must be 2MB aligned.
     * 
     * @param space Address space handle (HAL_ADDR_SPACE_CURRENT for current)
     * @param virt Virtual address (must be 2MB aligned)
     * @param phys Physical address (must be 2MB aligned)
     * @param flags Page flags (HAL_PAGE_*)
     * @return true on success, false on failure
     * 
     * @note On architectures that don't support huge pages, this falls back
     *       to mapping 512 individual 4KB pages.
     * @note This function does NOT flush the TLB.
     */
    static bool map_huge(hal_addr_space_t space, vaddr_t virt, paddr_t phys, uint32_t flags);

    /**
     * @brief Flush TLB entry for a specific address
     * @param virt Virtual address to flush
     */
    static void flush_tlb(vaddr_t virt);

    /**
     * @brief Flush entire TLB
     */
    static void flush_tlb_all();

    /**
     * @brief Create a new address space
     * 
     * Allocates and initializes a new page table hierarchy. The kernel portion
     * of the address space is shared with all other address spaces.
     * 
     * @return Address space handle, or HAL_ADDR_SPACE_INVALID on failure
     */
    static hal_addr_space_t create_space();

    /**
     * @brief Clone an address space with COW semantics
     * 
     * Creates a copy of an address space where user-space pages are shared
     * with copy-on-write semantics:
     *   - User pages are marked read-only in both parent and child
     *   - Physical pages have their reference count incremented
     *   - Kernel space is shared (not copied)
     * 
     * @param src Source address space to clone
     * @return New address space handle, or HAL_ADDR_SPACE_INVALID on failure
     */
    static hal_addr_space_t clone_space(hal_addr_space_t src);

    /**
     * @brief Destroy an address space
     * 
     * Frees all page table structures and decrements reference counts on
     * physical pages. Does NOT free physical pages that are still referenced
     * by other address spaces (COW).
     * 
     * @param space Address space handle to destroy
     * 
     * @warning Must not be the currently active address space.
     */
    static void destroy_space(hal_addr_space_t space);

    /**
     * @brief Switch to a different address space
     * 
     * Changes the current address space by updating the page table base register:
     *   - i686/x86_64: Updates CR3
     *   - ARM64: Updates TTBR0_EL1 and issues appropriate barriers
     * 
     * @param space Address space handle to switch to
     */
    static void switch_space(hal_addr_space_t space);

    /**
     * @brief Get the current address space
     * @return Handle of the currently active address space
     */
    static hal_addr_space_t current_space();

    /**
     * @brief Translate virtual address to physical address
     * 
     * Convenience wrapper around hal_mmu_query() for the current address space.
     * 
     * @param virt Virtual address to translate
     * @return Physical address, or PADDR_INVALID if not mapped
     */
    static paddr_t virt_to_phys(vaddr_t virt);

    /**
     * @brief Get current page table physical address
     * 
     * @return Physical address of the current page table (CR3 on x86, TTBR on ARM)
     * @deprecated Use hal_mmu_current_space() instead
     */
    static paddr_t get_current_page_table();

    /**
     * @brief Create a new page table
     * 
     * @return Physical address of the new page table, or PADDR_INVALID on failure
     * @deprecated Use hal_mmu_create_space() instead
     */
    static paddr_t create_page_table();

};

} // namespace hal

namespace hal {

/**
 * @brief 中断控制与处理函数注册
 */
class Interrupt {
public:
    /**
     * @brief Initialize interrupt system
     * 
     * This function initializes the interrupt system:
     *   - i686/x86_64: IDT, PIC/APIC
     *   - arm64: Exception vectors, GIC
     */
    static void init();

    /**
     * @brief Register an interrupt handler
     * @param irq Architecture-independent IRQ number
     * @param handler Handler function
     * @param data User data to pass to handler
     */
    static void register_handler(uint32_t irq, hal_interrupt_handler_t handler, void *data);

    /**
     * @brief Enable interrupts globally
     */
    static void enable();

    /**
     * @brief 设备中断线 irq 能否交给用户态驱动：线号有效，且内核自己没有在用
     */
    static bool irq_is_free(uint32_t irq);

    /** @brief 在中断控制器上屏蔽一条中断线 */
    static void mask_irq(uint32_t irq);

    /** @brief 在中断控制器上打开一条中断线 */
    static void unmask_irq(uint32_t irq);

};

} // namespace hal

namespace hal {

/**
 * @brief 平台定时器
 */
class Timer {
public:
    /**
     * @brief Initialize system timer
     * @param freq_hz Timer frequency in Hz
     * @param callback Function to call on each timer tick
     */
    static void init(uint32_t freq_hz, hal_timer_callback_t callback);

    /**
     * @brief Get timer frequency
     * @return Timer frequency in Hz
     */
    static uint32_t get_frequency();
};

} // namespace hal

namespace hal {

/**
 * @brief 缓存维护操作
 */
class Cache {
public:
};

} // namespace hal

namespace hal {

/**
 * @brief 内存映射 I/O 访问
 */
class Mmio {
public:
    /**
     * @brief Read 8-bit value from MMIO address
     * @param addr MMIO address
     * @return Value read
     */
    static inline uint8_t read8(volatile void *addr) {
        uint8_t val = *(volatile uint8_t *)addr;
    #if defined(ARCH_ARM64)
        __asm__ volatile("dmb sy" ::: "memory");
    #else
        __asm__ volatile("" ::: "memory");
    #endif
        return val;
    }

    /**
     * @brief Read 16-bit value from MMIO address
     * @param addr MMIO address
     * @return Value read
     */
    static inline uint16_t read16(volatile void *addr) {
        uint16_t val = *(volatile uint16_t *)addr;
    #if defined(ARCH_ARM64)
        __asm__ volatile("dmb sy" ::: "memory");
    #else
        __asm__ volatile("" ::: "memory");
    #endif
        return val;
    }

    /**
     * @brief Read 32-bit value from MMIO address
     * @param addr MMIO address
     * @return Value read
     */
    static inline uint32_t read32(volatile void *addr) {
        uint32_t val = *(volatile uint32_t *)addr;
    #if defined(ARCH_ARM64)
        __asm__ volatile("dmb sy" ::: "memory");
    #else
        __asm__ volatile("" ::: "memory");
    #endif
        return val;
    }

    /**
     * @brief Read 64-bit value from MMIO address
     * @param addr MMIO address
     * @return Value read
     */
    static inline uint64_t read64(volatile void *addr) {
        uint64_t val = *(volatile uint64_t *)addr;
    #if defined(ARCH_ARM64)
        __asm__ volatile("dmb sy" ::: "memory");
    #else
        __asm__ volatile("" ::: "memory");
    #endif
        return val;
    }

    /**
     * @brief Write 8-bit value to MMIO address
     * @param addr MMIO address
     * @param val Value to write
     */
    static inline void write8(volatile void *addr, uint8_t val) {
    #if defined(ARCH_ARM64)
        __asm__ volatile("dmb sy" ::: "memory");
    #else
        __asm__ volatile("" ::: "memory");
    #endif
        *(volatile uint8_t *)addr = val;
    }

    /**
     * @brief Write 16-bit value to MMIO address
     * @param addr MMIO address
     * @param val Value to write
     */
    static inline void write16(volatile void *addr, uint16_t val) {
    #if defined(ARCH_ARM64)
        __asm__ volatile("dmb sy" ::: "memory");
    #else
        __asm__ volatile("" ::: "memory");
    #endif
        *(volatile uint16_t *)addr = val;
    }

    /**
     * @brief Write 32-bit value to MMIO address
     * @param addr MMIO address
     * @param val Value to write
     */
    static inline void write32(volatile void *addr, uint32_t val) {
    #if defined(ARCH_ARM64)
        __asm__ volatile("dmb sy" ::: "memory");
    #else
        __asm__ volatile("" ::: "memory");
    #endif
        *(volatile uint32_t *)addr = val;
    }

    /**
     * @brief Write 64-bit value to MMIO address
     * @param addr MMIO address
     * @param val Value to write
     */
    static inline void write64(volatile void *addr, uint64_t val) {
    #if defined(ARCH_ARM64)
        __asm__ volatile("dmb sy" ::: "memory");
    #else
        __asm__ volatile("" ::: "memory");
    #endif
        *(volatile uint64_t *)addr = val;
    }
};

} // namespace hal

namespace hal {

/**
 * @brief 端口 I/O 访问（仅 x86）
 */
class Port {
public:
    /**
     * @brief Read 8-bit value from I/O port
     * @param port Port number
     * @return Value read
     */
    static inline uint8_t read8(uint16_t port) {
        uint8_t ret;
        __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
        return ret;
    }

    /**
     * @brief Read 16-bit value from I/O port
     * @param port Port number
     * @return Value read
     */
    static inline uint16_t read16(uint16_t port) {
        uint16_t ret;
        __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
        return ret;
    }

    /**
     * @brief Read 32-bit value from I/O port
     * @param port Port number
     * @return Value read
     */
    static inline uint32_t read32(uint16_t port) {
        uint32_t ret;
        __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
        return ret;
    }

    /**
     * @brief Write 8-bit value to I/O port
     * @param port Port number
     * @param val Value to write
     */
    static inline void write8(uint16_t port, uint8_t val) {
        __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
    }

    /**
     * @brief Write 16-bit value to I/O port
     * @param port Port number
     * @param val Value to write
     */
    static inline void write16(uint16_t port, uint16_t val) {
        __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
    }

    /**
     * @brief Write 32-bit value to I/O port
     * @param port Port number
     * @param val Value to write
     */
    static inline void write32(uint16_t port, uint32_t val) {
        __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
    }
};

} // namespace hal

namespace hal {

/**
 * @brief 任务上下文初始化与切换
 */
class Context {
public:
    /**
     * @brief Initialize a task context
     * @param ctx Pointer to context structure to initialize
     * @param entry Entry point address
     * @param stack Stack pointer
     * @param is_user true if this is a user-mode context
     */
    static void init(hal_context_t *ctx, uintptr_t entry, 
                          uintptr_t stack, bool is_user);

    /**
     * @brief Set the kernel stack for the current CPU
     * @param stack_top Top of the kernel stack
     */
    static void set_kernel_stack(uintptr_t stack_top);
};

} // namespace hal


#endif /* _HAL_HAL_H_ */
