/**
 * @file syscall64.c
 * @brief x86_64 System Call HAL Implementation
 * 
 * This file implements the x86_64-specific system call initialization as part
 * of the HAL (Hardware Abstraction Layer).
 *
 *
 * On x86_64, system calls are invoked using the SYSCALL instruction. This file
 * sets up the MSRs (Model Specific Registers) required for SYSCALL/SYSRET
 * operation:
 *   - IA32_EFER: Enable System Call Extensions (SCE)
 *   - IA32_STAR: Segment selectors for SYSCALL/SYSRET
 *   - IA32_LSTAR: Long mode SYSCALL target RIP
 *   - IA32_CSTAR: Compatibility mode SYSCALL target RIP
 *   - IA32_FMASK: RFLAGS mask for SYSCALL
 *
 * Additionally, INT 0x80 is supported for compatibility with legacy code.
 */

#include <kernel/smp.h>
#include <hal/hal.h>
#include <hal/hal_syscall.h>
#include <kernel/syscall.h>
#include <gdt64.h>
#include <idt64.h>
#include <lib/klog.h>

/* External assembly functions */
extern "C" void syscall_entry(void);
extern "C" void syscall_entry_compat(void);
extern "C" void syscall_init_msr(void);

/* What syscall_entry needs before it has a stack to stand on, one pair per CPU. It finds
 * its CPU's pair through the GS base (MSR_KERNEL_GS_BASE + SWAPGS); layout shared with
 * syscall64_asm.asm. */
struct syscall_cpu {
    uint64_t kernel_rsp;        /* +0: kernel stack of the task this CPU is running */
    uint64_t user_rsp;          /* +8: where the entry code parks the user RSP */
};
static struct syscall_cpu syscall_cpus[MAX_CPUS];

#define MSR_KERNEL_GS_BASE  0xC0000102u

/** Per-CPU part of the SYSCALL setup: the MSRs, and where this CPU's pair is */
extern "C" void syscall_init_cpu(void);
void syscall_init_cpu(void) {
    syscall_init_msr();
    uint64_t base = (uint64_t)&syscall_cpus[hal::Cpu::id()];
    __asm__ volatile("wrmsr" : : "c"(MSR_KERNEL_GS_BASE), "a"((uint32_t)base), "d"((uint32_t)(base >> 32)));
}

/* Global syscall handler (set by hal::Syscall::init) */
static hal_syscall_handler_t g_syscall_handler = NULL;

/**
 * @brief Initialize x86_64 system call mechanism
 * @param handler The system call dispatcher function
 *
 * This function sets up the SYSCALL/SYSRET mechanism by configuring
 * the required MSRs. It also sets up INT 0x80 for compatibility.
 *
 * Requirements: 7.5, 8.1 - System call entry mechanism
 */
void hal::Syscall::init(hal_syscall_handler_t handler) {
    LOG_INFO_MSG("Initializing x86_64 system call mechanism (SYSCALL/SYSRET)...\n");
    
    /* Store the handler for potential future use */
    g_syscall_handler = handler;
    
    /* Initialize MSRs for SYSCALL/SYSRET */
    syscall_init_cpu();
    
    LOG_DEBUG_MSG("  SYSCALL MSRs configured\n");
    LOG_DEBUG_MSG("  LSTAR = syscall_entry\n");
    LOG_DEBUG_MSG("  CSTAR = syscall_entry_compat\n");
    
    /* Also register INT 0x80 handler for compatibility
     * Flags: Present | Ring 3 | Trap Gate
     * Using trap gate so interrupts remain enabled during system call handling
     */
    idt64_set_gate(0x80, 
                   (uint64_t)syscall_entry_compat, 
                   GDT64_KERNEL_CODE_SEGMENT,
                   IDT64_IST_NONE,
                   IDT64_ATTR_PRESENT | IDT64_ATTR_DPL_RING3 | IDT64_TYPE_TRAP);
    
    LOG_DEBUG_MSG("  INT 0x80 handler registered for compatibility\n");
    
    LOG_INFO_MSG("x86_64 system call mechanism initialized\n");
}

/**
 * @brief Set the kernel stack for syscall entry
 * @param stack_ptr Kernel stack pointer
 *
 * This function sets the kernel stack that will be used when entering
 * the kernel via SYSCALL. It should be called during task switch to
 * update the kernel stack for the current task.
 */
void hal_syscall_set_kernel_stack(uint64_t stack_ptr) {
    syscall_cpus[hal::Cpu::id()].kernel_rsp = stack_ptr;
}

/* R9 is at frame[6]; frame[7] is R8, the fifth argument */
uintptr_t hal::Syscall::arg6(const uintptr_t *frame) {
    return frame[6];
}
