// ============================================================================
// syscall.c - i686 System Call HAL Implementation
// ============================================================================
//
// This file implements the i686-specific system call initialization as part
// of the HAL (Hardware Abstraction Layer).
//
// **Feature: multi-arch-support**
// **Validates: Requirements 8.1, 12.1**
//
// On i686, system calls are invoked using INT 0x80. This file sets up the
// IDT entry for interrupt 0x80 to point to the syscall_handler assembly
// routine.
// ============================================================================

#include <hal/hal.h>
#include <kernel/syscall.h>
#include <idt.h>
#include <gdt.h>
#include <lib/klog.h>

// External assembly entry point
extern void syscall_handler(void);

// Global syscall handler (set by hal_syscall_init)
static hal_syscall_handler_t g_syscall_handler = NULL;

/**
 * hal_syscall_init - Initialize i686 system call mechanism
 * @handler: The system call dispatcher function
 *
 * This function sets up INT 0x80 as the system call entry point.
 * The handler is registered in the IDT with Ring 3 access so that
 * user-mode programs can invoke system calls.
 *
 * Requirements: 8.1 - System call entry mechanism
 */
void hal_syscall_init(hal_syscall_handler_t handler) {
    LOG_INFO_MSG("Initializing i686 system call mechanism (INT 0x80)...\n");
    
    // Store the handler for potential future use
    g_syscall_handler = handler;
    
    // Register INT 0x80 handler in IDT
    // Flags: Present | Ring 3 | Trap Gate (0x8F = 0x80 | 0x60 | 0x0F)
    // Using trap gate (0x0F) instead of interrupt gate (0x0E) so interrupts
    // remain enabled during system call handling
    idt_set_gate(0x80, 
                 (uint32_t)syscall_handler, 
                 GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING3 | IDT_FLAG_GATE_TRAP);
    
    LOG_INFO_MSG("i686 system call mechanism initialized\n");
}

/**
 * hal_get_syscall_handler - Get the registered syscall handler
 *
 * Returns the currently registered system call handler function.
 * This can be used by the assembly entry point if needed.
 */
hal_syscall_handler_t hal_get_syscall_handler(void) {
    return g_syscall_handler;
}

