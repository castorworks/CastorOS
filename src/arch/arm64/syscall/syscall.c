/**
 * @file syscall.c
 * @brief ARM64 System Call HAL Implementation
 * 
 * This file implements the ARM64-specific system call initialization as part
 * of the HAL (Hardware Abstraction Layer).
 *
 * **Feature: multi-arch-support**
 * **Validates: Requirements 7.5, 8.1, 8.2**
 *
 * On ARM64, system calls are invoked using the SVC (Supervisor Call) instruction.
 * The SVC instruction generates a synchronous exception that is handled by the
 * exception vector table. The exception handler identifies SVC exceptions by
 * checking the Exception Class (EC) field in ESR_EL1.
 *
 * System call convention:
 *   - X8  = system call number
 *   - X0  = arg1 (also return value)
 *   - X1  = arg2
 *   - X2  = arg3
 *   - X3  = arg4
 *   - X4  = arg5
 *   - X5  = arg6
 */

#include <hal/hal.h>
#include <types.h>

/* Forward declaration for serial output */
extern void serial_puts(const char *str);

/* Global syscall handler (set by hal_syscall_init) */
static hal_syscall_handler_t g_syscall_handler = NULL;

/* Flag to track if syscall system is initialized */
static bool g_syscall_initialized = false;

/**
 * @brief Initialize ARM64 system call mechanism
 * @param handler The system call dispatcher function
 *
 * On ARM64, system calls are handled through the exception vector table.
 * The SVC instruction triggers a synchronous exception which is routed
 * to the appropriate handler based on the Exception Class in ESR_EL1.
 *
 * Requirements: 7.5, 8.1 - System call entry mechanism
 */
void hal_syscall_init(hal_syscall_handler_t handler) {
    serial_puts("Initializing ARM64 system call mechanism (SVC)...\n");
    
    /* Store the handler for potential future use */
    g_syscall_handler = handler;
    
    /* On ARM64, SVC handling is already set up through the exception vectors.
     * The exception handler in exception.c checks for ESR_EC_SVC64 and
     * dispatches to the syscall handler.
     * 
     * No additional setup is required here - the exception vectors are
     * installed during hal_interrupt_init().
     */
    
    g_syscall_initialized = true;
    
    serial_puts("ARM64 system call mechanism initialized\n");
}

/**
 * @brief Get the registered syscall handler
 * @return The currently registered system call handler function
 *
 * This can be used by the exception handler if needed.
 */
hal_syscall_handler_t hal_get_syscall_handler(void) {
    return g_syscall_handler;
}

/**
 * @brief Check if syscall system is initialized
 * @return true if initialized, false otherwise
 */
bool hal_syscall_initialized(void) {
    return g_syscall_initialized;
}

/* ============================================================================
 * User Mode Transition
 * ============================================================================ */

/* External assembly function for entering user mode */
extern void enter_usermode_arm64(uint64_t entry_point, uint64_t user_stack);

/**
 * @brief Enter user mode (HAL interface)
 * @param entry_point User code entry address
 * @param user_stack User stack pointer
 *
 * This function transitions from kernel mode (EL1) to user mode (EL0)
 * using the ERET instruction. It never returns.
 *
 * **Feature: multi-arch-support, Property 11: User Mode Transition Correctness (ARM64)**
 * **Validates: Requirements 7.4**
 */
void hal_enter_usermode(uintptr_t entry_point, uintptr_t user_stack) {
    serial_puts("Entering user mode...\n");
    serial_puts("  Entry point: ");
    extern void serial_put_hex64(uint64_t value);
    serial_put_hex64((uint64_t)entry_point);
    serial_puts("\n");
    serial_puts("  User stack:  ");
    serial_put_hex64((uint64_t)user_stack);
    serial_puts("\n");
    
    /* Call the assembly function to perform the actual transition */
    enter_usermode_arm64((uint64_t)entry_point, (uint64_t)user_stack);
    
    /* Should never reach here */
    serial_puts("ERROR: enter_usermode_arm64 returned!\n");
    while (1) {
        __asm__ volatile("wfi");
    }
}

