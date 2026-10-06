/**
 * @file syscall.c
 * @brief ARM64 System Call HAL Implementation
 * 
 * This file implements the ARM64-specific system call initialization as part
 * of the HAL (Hardware Abstraction Layer).
 *
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
#include <hal/hal_syscall.h>
#include <types.h>

/* Forward declaration for serial output */
extern "C" void serial_puts(const char *str);
extern "C" void serial_put_hex64(uint64_t value);

/* Global syscall handler (set by hal::Syscall::init) */
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
void hal::Syscall::init(hal_syscall_handler_t handler) {
    serial_puts("Initializing ARM64 system call mechanism (SVC)...\n");
    
    /* Store the handler for potential future use */
    g_syscall_handler = handler;
    
    /* On ARM64, SVC handling is already set up through the exception vectors.
     * The exception handler in exception.c checks for ESR_EC_SVC64 and
     * dispatches to the syscall handler.
     * 
     * No additional setup is required here - the exception vectors are
     * installed during hal::Interrupt::init().
     */
    
    g_syscall_initialized = true;
    
    serial_puts("ARM64 system call mechanism initialized\n");
}

/* ============================================================================
 * User Mode Transition
 * ============================================================================ */

/* External assembly function for entering user mode */
extern "C" void enter_usermode_arm64(uint64_t entry_point, uint64_t user_stack);

/* X5: the frame starts with X0 */
uintptr_t hal::Syscall::arg6(const uintptr_t *frame) {
    return frame[5];
}
