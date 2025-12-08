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
 * 
 * Requirements: 1.1, 1.3
 */

#ifndef _HAL_HAL_H_
#define _HAL_HAL_H_

#include <types.h>

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
void hal_cpu_init(void);

/**
 * @brief Get current CPU ID (reserved for multi-core support)
 * @return Current CPU ID (0 for single-core systems)
 */
uint32_t hal_cpu_id(void);

/**
 * @brief Halt the CPU
 * 
 * Puts the CPU into a low-power state until the next interrupt.
 */
void hal_cpu_halt(void);

/* ============================================================================
 * Interrupt Management
 * ========================================================================== */

/**
 * @brief Interrupt handler function type
 * @param data User-provided data pointer
 */
typedef void (*hal_interrupt_handler_t)(void *data);

/**
 * @brief Initialize interrupt system
 * 
 * This function initializes the interrupt system:
 *   - i686/x86_64: IDT, PIC/APIC
 *   - arm64: Exception vectors, GIC
 */
void hal_interrupt_init(void);

/**
 * @brief Register an interrupt handler
 * @param irq Architecture-independent IRQ number
 * @param handler Handler function
 * @param data User data to pass to handler
 */
void hal_interrupt_register(uint32_t irq, hal_interrupt_handler_t handler, void *data);

/**
 * @brief Unregister an interrupt handler
 * @param irq Architecture-independent IRQ number
 */
void hal_interrupt_unregister(uint32_t irq);

/**
 * @brief Enable interrupts globally
 */
void hal_interrupt_enable(void);

/**
 * @brief Disable interrupts globally
 */
void hal_interrupt_disable(void);

/**
 * @brief Save interrupt state and disable interrupts
 * @return Previous interrupt state (for restoration)
 */
uint64_t hal_interrupt_save(void);

/**
 * @brief Restore interrupt state
 * @param state Previously saved interrupt state
 */
void hal_interrupt_restore(uint64_t state);

/**
 * @brief Send End-Of-Interrupt signal
 * @param irq IRQ number that was handled
 */
void hal_interrupt_eoi(uint32_t irq);

/* ============================================================================
 * Memory Management Unit (MMU)
 * ========================================================================== */

/** Page table entry flags */
#define HAL_PAGE_PRESENT    (1 << 0)   /**< Page is present in memory */
#define HAL_PAGE_WRITE      (1 << 1)   /**< Page is writable */
#define HAL_PAGE_USER       (1 << 2)   /**< Page is accessible from user mode */
#define HAL_PAGE_NOCACHE    (1 << 3)   /**< Disable caching for this page */
#define HAL_PAGE_EXEC       (1 << 4)   /**< Page is executable */
#define HAL_PAGE_COW        (1 << 5)   /**< Copy-on-Write flag */

/**
 * @brief Initialize MMU/paging
 */
void hal_mmu_init(void);

/**
 * @brief Create a page table mapping
 * @param virt Virtual address (must be page-aligned)
 * @param phys Physical address (must be page-aligned)
 * @param flags Page flags (HAL_PAGE_*)
 * @return true on success, false on failure
 */
bool hal_mmu_map(uintptr_t virt, uintptr_t phys, uint32_t flags);

/**
 * @brief Remove a page table mapping
 * @param virt Virtual address to unmap
 */
void hal_mmu_unmap(uintptr_t virt);

/**
 * @brief Flush TLB entry for a specific address
 * @param virt Virtual address to flush
 */
void hal_mmu_flush_tlb(uintptr_t virt);

/**
 * @brief Flush entire TLB
 */
void hal_mmu_flush_tlb_all(void);

/**
 * @brief Switch address space
 * @param page_table_phys Physical address of the new page table
 */
void hal_mmu_switch_space(uintptr_t page_table_phys);

/**
 * @brief Get the faulting address from a page fault
 * @return The virtual address that caused the page fault
 */
uintptr_t hal_mmu_get_fault_addr(void);

/**
 * @brief Translate virtual address to physical address
 * @param virt Virtual address to translate
 * @return Physical address, or 0 if not mapped
 */
uintptr_t hal_mmu_virt_to_phys(uintptr_t virt);

/**
 * @brief Get current page table physical address
 * @return Physical address of the current page table (CR3 on x86, TTBR on ARM)
 */
uintptr_t hal_mmu_get_current_page_table(void);

/**
 * @brief Create a new page table
 * @return Physical address of the new page table, or 0 on failure
 */
uintptr_t hal_mmu_create_page_table(void);

/**
 * @brief Destroy a page table
 * @param page_table_phys Physical address of the page table to destroy
 */
void hal_mmu_destroy_page_table(uintptr_t page_table_phys);

/* ============================================================================
 * Context Switch
 * ========================================================================== */

/**
 * @brief Get the size of the architecture-specific context structure
 * @return Size in bytes
 */
size_t hal_context_size(void);

/**
 * @brief Initialize a task context
 * @param ctx Pointer to context structure to initialize
 * @param entry Entry point address
 * @param stack Stack pointer
 * @param is_user true if this is a user-mode context
 */
void hal_context_init(hal_context_t *ctx, uintptr_t entry, 
                      uintptr_t stack, bool is_user);

/**
 * @brief Perform a context switch
 * @param old_ctx Pointer to save current context (can be NULL)
 * @param new_ctx Pointer to context to switch to
 */
void hal_context_switch(hal_context_t **old_ctx, hal_context_t *new_ctx);

/**
 * @brief Set the kernel stack for the current CPU
 * @param stack_top Top of the kernel stack
 */
void hal_context_set_kernel_stack(uintptr_t stack_top);

/* ============================================================================
 * System Call Interface
 * ========================================================================== */

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
 * @brief Initialize system call entry mechanism
 * @param handler The system call dispatcher function
 */
void hal_syscall_init(hal_syscall_handler_t handler);

/* ============================================================================
 * Timer
 * ========================================================================== */

/**
 * @brief Timer callback function type
 */
typedef void (*hal_timer_callback_t)(void);

/**
 * @brief Initialize system timer
 * @param freq_hz Timer frequency in Hz
 * @param callback Function to call on each timer tick
 */
void hal_timer_init(uint32_t freq_hz, hal_timer_callback_t callback);

/**
 * @brief Get system tick count
 * @return Number of timer ticks since boot
 */
uint64_t hal_timer_get_ticks(void);

/**
 * @brief Get timer frequency
 * @return Timer frequency in Hz
 */
uint32_t hal_timer_get_frequency(void);

/* ============================================================================
 * I/O Operations
 * ========================================================================== */

/**
 * @brief Read 8-bit value from MMIO address
 * @param addr MMIO address
 * @return Value read
 */
static inline uint8_t hal_mmio_read8(volatile void *addr) {
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
static inline uint16_t hal_mmio_read16(volatile void *addr) {
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
static inline uint32_t hal_mmio_read32(volatile void *addr) {
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
static inline uint64_t hal_mmio_read64(volatile void *addr) {
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
static inline void hal_mmio_write8(volatile void *addr, uint8_t val) {
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
static inline void hal_mmio_write16(volatile void *addr, uint16_t val) {
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
static inline void hal_mmio_write32(volatile void *addr, uint32_t val) {
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
static inline void hal_mmio_write64(volatile void *addr, uint64_t val) {
#if defined(ARCH_ARM64)
    __asm__ volatile("dmb sy" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
    *(volatile uint64_t *)addr = val;
}

/* ============================================================================
 * Memory Barriers
 * ========================================================================== */

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

/* ============================================================================
 * Port I/O (x86 only)
 * ========================================================================== */

#if defined(ARCH_I686) || defined(ARCH_X86_64)

/**
 * @brief Read 8-bit value from I/O port
 * @param port Port number
 * @return Value read
 */
static inline uint8_t hal_port_read8(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * @brief Read 16-bit value from I/O port
 * @param port Port number
 * @return Value read
 */
static inline uint16_t hal_port_read16(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * @brief Read 32-bit value from I/O port
 * @param port Port number
 * @return Value read
 */
static inline uint32_t hal_port_read32(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * @brief Write 8-bit value to I/O port
 * @param port Port number
 * @param val Value to write
 */
static inline void hal_port_write8(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/**
 * @brief Write 16-bit value to I/O port
 * @param port Port number
 * @param val Value to write
 */
static inline void hal_port_write16(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

/**
 * @brief Write 32-bit value to I/O port
 * @param port Port number
 * @param val Value to write
 */
static inline void hal_port_write32(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

#endif /* ARCH_I686 || ARCH_X86_64 */

/* ============================================================================
 * Architecture Information
 * ========================================================================== */

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

/* ============================================================================
 * HAL Initialization State Query
 * ========================================================================== */

/**
 * @brief Check if CPU has been initialized via HAL
 * @return true if hal_cpu_init() has completed successfully
 */
bool hal_cpu_initialized(void);

/**
 * @brief Check if interrupt system has been initialized via HAL
 * @return true if hal_interrupt_init() has completed successfully
 */
bool hal_interrupt_initialized(void);

/**
 * @brief Check if MMU has been initialized via HAL
 * @return true if hal_mmu_init() has completed successfully
 */
bool hal_mmu_initialized(void);

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

#endif /* _HAL_HAL_H_ */
