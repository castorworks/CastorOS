/**
 * @file exception.c
 * @brief ARM64 Exception Handler Implementation
 * 
 * Implements the C-level exception handling for ARM64.
 * Called from the assembly vectors after register state is saved.
 * 
 * Requirements: 4.5, 6.2, 6.3
 * 
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Feature: arm64-kernel-integration**
 * **Validates: Requirements 6.2, 2.3, 6.3**
 */

#include <kernel/interrupt.h>
#include "exception.h"
#include <hal/hal.h>
#include <types.h>
#include <mm/vmm.h>
#include <kernel/task.h>

/* Forward declaration for serial output */
extern "C" void serial_puts(const char *str);
extern "C" void serial_put_hex64(uint64_t value);

/* Forward declaration for GIC functions */
extern void gic_handle_irq(void);
extern uint32_t gic_acknowledge_irq(void);
extern void gic_end_irq(uint32_t irq);

/* Forward declaration for ARM64 COW fault check */
extern bool arm64_is_cow_fault(uint64_t esr);

/* ============================================================================
 * Exception Class Names
 * ========================================================================== */

/* 稀疏表：C++ 不支持非连续的数组指定初始化，改用 switch 查找 */
static const char *exception_class_lookup(uint32_t x) {
    switch (x) {
        case ESR_EC_UNKNOWN: return "Unknown";
        case ESR_EC_WFI_WFE: return "WFI/WFE trapped";
        case ESR_EC_CP15_MCR: return "MCR/MRC CP15";
        case ESR_EC_CP15_MCRR: return "MCRR/MRRC CP15";
        case ESR_EC_CP14_MCR: return "MCR/MRC CP14";
        case ESR_EC_CP14_LDC: return "LDC/STC CP14";
        case ESR_EC_FP_ASIMD: return "FP/ASIMD access";
        case ESR_EC_CP10_MCR: return "MCR/MRC CP10";
        case ESR_EC_PAC: return "PAC trapped";
        case ESR_EC_CP14_MRRC: return "MRRC CP14";
        case ESR_EC_BTI: return "BTI exception";
        case ESR_EC_ILLEGAL: return "Illegal execution state";
        case ESR_EC_SVC32: return "SVC (AArch32)";
        case ESR_EC_HVC32: return "HVC (AArch32)";
        case ESR_EC_SMC32: return "SMC (AArch32)";
        case ESR_EC_SVC64: return "SVC (AArch64)";
        case ESR_EC_HVC64: return "HVC (AArch64)";
        case ESR_EC_SMC64: return "SMC (AArch64)";
        case ESR_EC_SYS64: return "MSR/MRS/SYS trapped";
        case ESR_EC_SVE: return "SVE access";
        case ESR_EC_ERET: return "ERET trapped";
        case ESR_EC_FPAC: return "FPAC exception";
        case ESR_EC_SME: return "SME access";
        case ESR_EC_IABT_LOW: return "Instruction abort (lower EL)";
        case ESR_EC_IABT_CUR: return "Instruction abort (current EL)";
        case ESR_EC_PC_ALIGN: return "PC alignment fault";
        case ESR_EC_DABT_LOW: return "Data abort (lower EL)";
        case ESR_EC_DABT_CUR: return "Data abort (current EL)";
        case ESR_EC_SP_ALIGN: return "SP alignment fault";
        case ESR_EC_FP32: return "FP exception (AArch32)";
        case ESR_EC_FP64: return "FP exception (AArch64)";
        case ESR_EC_SERROR: return "SError";
        case ESR_EC_BKPT_LOW: return "Breakpoint (lower EL)";
        case ESR_EC_BKPT_CUR: return "Breakpoint (current EL)";
        case ESR_EC_STEP_LOW: return "Software step (lower EL)";
        case ESR_EC_STEP_CUR: return "Software step (current EL)";
        case ESR_EC_WATCH_LOW: return "Watchpoint (lower EL)";
        case ESR_EC_WATCH_CUR: return "Watchpoint (current EL)";
        case ESR_EC_BKPT32: return "BKPT (AArch32)";
        case ESR_EC_BRK64: return "BRK (AArch64)";        default: return nullptr;
    }
}

static const char *fault_status_lookup(uint32_t x) {
    switch (x) {
        case FSC_ADDR_L0: return "Address size fault, level 0";
        case FSC_ADDR_L1: return "Address size fault, level 1";
        case FSC_ADDR_L2: return "Address size fault, level 2";
        case FSC_ADDR_L3: return "Address size fault, level 3";
        case FSC_TRANS_L0: return "Translation fault, level 0";
        case FSC_TRANS_L1: return "Translation fault, level 1";
        case FSC_TRANS_L2: return "Translation fault, level 2";
        case FSC_TRANS_L3: return "Translation fault, level 3";
        case FSC_ACCESS_L1: return "Access flag fault, level 1";
        case FSC_ACCESS_L2: return "Access flag fault, level 2";
        case FSC_ACCESS_L3: return "Access flag fault, level 3";
        case FSC_PERM_L1: return "Permission fault, level 1";
        case FSC_PERM_L2: return "Permission fault, level 2";
        case FSC_PERM_L3: return "Permission fault, level 3";
        case FSC_SYNC_EXT: return "Synchronous external abort";
        case FSC_SYNC_TAG: return "Synchronous tag check fault";
        case FSC_ALIGN: return "Alignment fault";
        case FSC_TLB_CONFLICT: return "TLB conflict abort";        default: return nullptr;
    }
}

/* Exception type names */
static const char *exception_type_names[] = {
    [EXCEPTION_SYNC]    = "Synchronous",
    [EXCEPTION_IRQ]     = "IRQ",
    [EXCEPTION_FIQ]     = "FIQ",
    [EXCEPTION_SERROR]  = "SError",
};

/* Exception source names */
static const char *exception_source_names[] = {
    [EXCEPTION_FROM_EL1_SP0]    = "EL1 with SP0",
    [EXCEPTION_FROM_EL1_SPX]    = "EL1 with SPx",
    [EXCEPTION_FROM_EL0_64]     = "EL0 (AArch64)",
    [EXCEPTION_FROM_EL0_32]     = "EL0 (AArch32)",
};

/* ============================================================================
 * Helper Functions
 * ========================================================================== */

/**
 * @brief Get exception class name
 */
const char *arm64_exception_class_name(uint32_t ec) {
    const char *name = exception_class_lookup(ec);
    return name ? name : "Unknown";
}

/**
 * @brief Get fault status code name
 */
const char *arm64_fault_status_name(uint32_t fsc) {
    const char *name = fault_status_lookup(fsc);
    return name ? name : "Unknown";
}

/**
 * @brief Print a register value
 */
static void print_reg(const char *name, uint64_t value) {
    serial_puts("  ");
    serial_puts(name);
    serial_puts(" = ");
    serial_put_hex64(value);
    serial_puts("\n");
}

/**
 * @brief Dump register state
 */
static void dump_registers(arm64_regs_t *regs) {
    serial_puts("\nRegister dump:\n");
    
    /* Print X0-X30 in pairs */
    for (int i = 0; i < 30; i += 2) {
        serial_puts("  X");
        if (i < 10) serial_puts("0");
        /* Simple number to string */
        char buf[3];
        buf[0] = '0' + (i / 10);
        buf[1] = '0' + (i % 10);
        buf[2] = '\0';
        serial_puts(buf);
        serial_puts(" = ");
        serial_put_hex64(regs->x[i]);
        serial_puts("  X");
        buf[0] = '0' + ((i + 1) / 10);
        buf[1] = '0' + ((i + 1) % 10);
        serial_puts(buf);
        serial_puts(" = ");
        serial_put_hex64(regs->x[i + 1]);
        serial_puts("\n");
    }
    
    print_reg("X30 (LR)", regs->x[30]);
    print_reg("SP_EL0 ", regs->sp_el0);
    print_reg("ELR_EL1", regs->elr);
    print_reg("SPSR   ", regs->spsr);
}

/* ============================================================================
 * Exception Handlers
 * ========================================================================== */

/* External syscall handler (defined in svc.S) */
extern "C" void arm64_syscall_handler(void *regs);

/**
 * @brief Handle synchronous exception
 */
static void handle_sync_exception(arm64_regs_t *regs, uint32_t source) {
    uint64_t esr = arm64_get_esr();
    uint64_t far = arm64_get_far();
    uint32_t ec = (esr >> ESR_EC_SHIFT) & 0x3F;
    uint32_t iss = esr & ESR_ISS_MASK;
    
    /* Debug: Print first few sync exceptions from EL0 */
    static int el0_sync_count = 0;
    if (source == EXCEPTION_FROM_EL0_64 && el0_sync_count < 5) {
        serial_puts("[SYNC] From EL0, EC=");
        serial_put_hex64(ec);
        serial_puts(", ELR=");
        serial_put_hex64(regs->elr);
        serial_puts("\n");
        el0_sync_count++;
    }
    
    /* Handle SVC (system call) */
    if (ec == ESR_EC_SVC64) {
        /* Debug: Print first few syscalls */
        static int syscall_count = 0;
        if (syscall_count < 10) {
            serial_puts("[SVC] Syscall from EL0, X8=");
            serial_put_hex64(regs->x[8]);
            serial_puts("\n");
            syscall_count++;
        }
        /* Dispatch to syscall handler
         * The syscall handler will extract arguments from the saved frame
         * and call syscall_dispatcher
         */
        arm64_syscall_handler(regs);
        
        /* NOTE: For SVC exceptions, ARM64 hardware already sets ELR_EL1 to PC+4
         * (the preferred return address), so we do NOT need to advance it here.
         * This is different from some other exception types where ELR points
         * to the faulting instruction.
         */
        
        /* Debug: Print return info */
        if (syscall_count <= 10) {
            serial_puts("[SVC] Return: X0=");
            serial_put_hex64(regs->x[0]);
            serial_puts(" ELR=");
            serial_put_hex64(regs->elr);
            serial_puts("\n");
        }
        return;
    }
    
    serial_puts("\n========== SYNCHRONOUS EXCEPTION ==========\n");
    serial_puts("Exception class: ");
    serial_puts(arm64_exception_class_name(ec));
    serial_puts("\n");
    serial_puts("Source: ");
    serial_puts(exception_source_names[source]);
    serial_puts("\n");
    
    print_reg("ESR_EL1", esr);
    print_reg("FAR_EL1", far);
    print_reg("ELR_EL1", regs->elr);
    
    /* Handle specific exception types */
    switch (ec) {
        case ESR_EC_IABT_LOW:
        case ESR_EC_IABT_CUR:
            /* Instruction abort - try to handle via VMM first */
            {
                uint32_t ifsc = iss & ESR_ISS_DFSC_MASK;
                bool is_user = (ec == ESR_EC_IABT_LOW);
                
                /* 
                 * Check for kernel page fault (might need page table sync)
                 * 
                 * **Feature: arm64-kernel-integration**
                 * **Validates: Requirements 2.3**
                 */
                if (!is_user && far >= KERNEL_VIRTUAL_BASE) {
                    if (mm::Vmm::handle_kernel_page_fault((uintptr_t)far)) {
                        /* Kernel page fault handled, return to faulting instruction */
                        return;
                    }
                }
                
                /* 
                 * Unhandled instruction abort
                 * 
                 * **Feature: arm64-kernel-integration**
                 * **Validates: Requirements 6.3**
                 */
                serial_puts("Instruction abort\n");
                serial_puts("Fault status: ");
                serial_puts(arm64_fault_status_name(ifsc));
                serial_puts("\n");
                serial_puts("User mode: ");
                serial_puts(is_user ? "Yes" : "No");
                serial_puts("\n");
                
                /* 
                 * For user mode faults, terminate the process with SIGSEGV-like behavior
                 * 
                 * **Feature: arm64-kernel-integration**
                 * **Validates: Requirements 6.3**
                 */
                if (is_user) {
                    serial_puts("Terminating user process due to illegal instruction fetch\n");
                    arm64_terminate_user_process(regs, ARM64_SIGNAL_SEGV, far);
                    return;  /* Should not reach here */
                }
            }
            break;
            
        case ESR_EC_DABT_LOW:
        case ESR_EC_DABT_CUR:
            /* Data abort - try to handle via VMM first */
            {
                uint32_t dfsc = iss & ESR_ISS_DFSC_MASK;
                bool is_write = (iss & ESR_ISS_WNR) != 0;
                bool is_user = (ec == ESR_EC_DABT_LOW);
                
                /* 
                 * Try to handle page fault via VMM
                 * 
                 * **Feature: arm64-kernel-integration**
                 * **Validates: Requirements 2.3**
                 */
                
                /* Check for COW fault first (permission fault + write) */
                if (arm64_is_cow_fault(esr)) {
                    /* 
                     * Convert ARM64 fault info to x86-compatible error code for mm::Vmm::handle_cow_page_fault:
                     * Bit 0 (P): 1 = page present (permission fault means page exists)
                     * Bit 1 (W): 1 = write operation
                     * Bit 2 (U): 1 = user mode
                     */
                    uint32_t error_code = 0x3;  /* Present + Write */
                    if (is_user) {
                        error_code |= 0x4;  /* User mode */
                    }
                    
                    if (mm::Vmm::handle_cow_page_fault((uintptr_t)far, error_code)) {
                        /* COW fault handled successfully, return to faulting instruction */
                        return;
                    }
                }
                
                /* Check for kernel page fault (might need page table sync) */
                if (!is_user && far >= KERNEL_VIRTUAL_BASE) {
                    if (mm::Vmm::handle_kernel_page_fault((uintptr_t)far)) {
                        /* Kernel page fault handled, return to faulting instruction */
                        return;
                    }
                }
                
                /* 
                 * Unhandled page fault
                 * 
                 * **Feature: arm64-kernel-integration**
                 * **Validates: Requirements 6.3**
                 */
                serial_puts("Data abort\n");
                serial_puts("Fault status: ");
                serial_puts(arm64_fault_status_name(dfsc));
                serial_puts("\n");
                serial_puts("Operation: ");
                serial_puts(is_write ? "Write" : "Read");
                serial_puts("\n");
                serial_puts("User mode: ");
                serial_puts(is_user ? "Yes" : "No");
                serial_puts("\n");
                
                /* 
                 * For user mode faults, terminate the process with SIGSEGV-like behavior
                 * 
                 * **Feature: arm64-kernel-integration**
                 * **Validates: Requirements 6.3**
                 */
                if (is_user) {
                    serial_puts("Terminating user process due to segmentation fault\n");
                    arm64_terminate_user_process(regs, ARM64_SIGNAL_SEGV, far);
                    return;  /* Should not reach here */
                }
            }
            break;
            
        case ESR_EC_PC_ALIGN:
            serial_puts("PC alignment fault\n");
            break;
            
        case ESR_EC_SP_ALIGN:
            serial_puts("SP alignment fault\n");
            break;
            
        case ESR_EC_BRK64:
            serial_puts("Breakpoint (BRK instruction)\n");
            serial_puts("Comment: ");
            serial_put_hex64(iss & 0xFFFF);
            serial_puts("\n");
            break;
            
        default:
            serial_puts("Unhandled exception class\n");
            break;
    }
    
    /* 来自 EL0 的未处理异常（未定义指令、BRK、对齐错误等）只终止出错进程 */
    if ((regs->spsr & 0xF) == 0) {
        serial_puts("Terminating user process due to unhandled exception\n");
        arm64_terminate_user_process(regs, ARM64_SIGNAL_SEGV, regs->elr);
        return;
    }

    dump_registers(regs);
    serial_puts("============================================\n");
    
    /* Halt on unhandled exceptions */
    serial_puts("\nSystem halted.\n");
    while (1) {
        __asm__ volatile("wfi");
    }
}

/**
 * @brief Handle IRQ interrupt
 */
static void handle_irq(arm64_regs_t *regs, uint32_t source) {
    (void)regs;
    
    /* Debug: Print source to see if we're getting EL0 IRQs */
    static int irq_count = 0;
    irq_count++;
    
    if (irq_count <= 5) {
        serial_puts("[IRQ] source=");
        serial_put_hex64(source);
        if (source == EXCEPTION_FROM_EL0_64) {
            serial_puts(" (EL0)");
        } else if (source == EXCEPTION_FROM_EL1_SPX) {
            serial_puts(" (EL1)");
        }
        serial_puts("\n");
    }
    
    /* Acknowledge and handle IRQ via GIC */
    interrupt_enter();
    gic_handle_irq();
    interrupt_exit();
}

/**
 * @brief Handle FIQ interrupt
 */
static void handle_fiq(arm64_regs_t *regs, uint32_t source) {
    (void)regs;
    (void)source;
    
    serial_puts("FIQ received (not implemented)\n");
    /* FIQ is typically used for secure interrupts, not implemented */
}

/**
 * @brief Handle SError (System Error)
 */
static void handle_serror(arm64_regs_t *regs, uint32_t source) {
    uint64_t esr = arm64_get_esr();
    
    serial_puts("\n========== SYSTEM ERROR (SError) ==========\n");
    serial_puts("Source: ");
    serial_puts(exception_source_names[source]);
    serial_puts("\n");
    
    print_reg("ESR_EL1", esr);
    dump_registers(regs);
    
    serial_puts("============================================\n");
    serial_puts("\nFatal error - System halted.\n");
    
    while (1) {
        __asm__ volatile("wfi");
    }
}

/* ============================================================================
 * Main Exception Handler
 * ========================================================================== */

/**
 * @brief Main exception handler (called from vectors.S)
 * 
 * This function is called from the assembly exception vectors after
 * the register state has been saved to the stack.
 * 
 * @param regs Pointer to saved register frame
 * @param type Exception type (EXCEPTION_SYNC, EXCEPTION_IRQ, etc.)
 * @param source Exception source (EXCEPTION_FROM_EL1_SPX, etc.)
 */
void arm64_exception_handler(arm64_regs_t *regs, uint32_t type, uint32_t source) {
    /* Debug: Track first few exceptions from each source */
    static int el0_exception_count = 0;
    static int el1_exception_count = 0;
    
    if (source == EXCEPTION_FROM_EL0_64 && el0_exception_count < 5) {
        serial_puts("[EXC] From EL0: type=");
        serial_put_hex64(type);
        serial_puts(", ELR=");
        serial_put_hex64(regs->elr);
        serial_puts("\n");
        el0_exception_count++;
    } else if (source == EXCEPTION_FROM_EL1_SPX && el1_exception_count < 3) {
        /* Only print first few EL1 exceptions to avoid spam */
        el1_exception_count++;
    }
    
    switch (type) {
        case EXCEPTION_SYNC:
            handle_sync_exception(regs, source);
            break;
            
        case EXCEPTION_IRQ:
            handle_irq(regs, source);
            break;
            
        case EXCEPTION_FIQ:
            handle_fiq(regs, source);
            break;
            
        case EXCEPTION_SERROR:
            handle_serror(regs, source);
            break;
            
        default:
            serial_puts("Unknown exception type!\n");
            while (1) {
                __asm__ volatile("wfi");
            }
    }
}

/* ============================================================================
 * Initialization
 * ========================================================================== */

/**
 * @brief Initialize ARM64 exception handling
 */
void arm64_exception_init(void) {
    serial_puts("Initializing ARM64 exception handling...\n");
    
    /* Install exception vector table */
    arm64_install_vectors();
    
    serial_puts("Exception vectors installed at VBAR_EL1\n");
}

/* ============================================================================
 * User Process Termination
 * 
 * **Feature: arm64-kernel-integration**
 * **Validates: Requirements 6.3**
 * ========================================================================== */

/**
 * @brief Get signal name string
 * @param signal Signal number
 * @return Human-readable signal name
 */
static const char *arm64_signal_name(uint32_t signal) {
    switch (signal) {
        case ARM64_SIGNAL_SEGV: return "SIGSEGV";
        case ARM64_SIGNAL_BUS:  return "SIGBUS";
        case ARM64_SIGNAL_ILL:  return "SIGILL";
        case ARM64_SIGNAL_FPE:  return "SIGFPE";
        case ARM64_SIGNAL_TRAP: return "SIGTRAP";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Terminate a user process due to a fatal exception
 * 
 * This function is called when a user process causes an unhandled exception
 * (e.g., segmentation fault, illegal instruction). It terminates the process
 * and schedules another task.
 * 
 * **Feature: arm64-kernel-integration**
 * **Validates: Requirements 6.3**
 * 
 * @param regs Pointer to saved register frame
 * @param signal Signal number (ARM64_SIGNAL_*)
 * @param fault_addr Faulting address (for debugging)
 */
void arm64_terminate_user_process(arm64_regs_t *regs, uint32_t signal, uint64_t fault_addr) {
    task_t *current = kernel::Scheduler::get_current();
    
    serial_puts("\n========== USER PROCESS TERMINATED ==========\n");
    serial_puts("Signal: ");
    serial_puts(arm64_signal_name(signal));
    serial_puts("\n");
    
    if (current) {
        serial_puts("Process: PID=");
        serial_put_hex64(current->pid);
        serial_puts(", name=");
        serial_puts(current->name);
        serial_puts("\n");
    }
    
    serial_puts("Fault address: ");
    serial_put_hex64(fault_addr);
    serial_puts("\n");
    
    serial_puts("PC at fault: ");
    serial_put_hex64(regs->elr);
    serial_puts("\n");
    
    serial_puts("User SP: ");
    serial_put_hex64(regs->sp_el0);
    serial_puts("\n");
    
    serial_puts("==============================================\n\n");
    
    /* 
     * Terminate the current process
     * 
     * The exit code encodes the signal number in the upper bits
     * (similar to how POSIX encodes signal termination)
     * Exit code = 128 + signal_number
     */
    if (current) {
        /* Mark the process as terminated by signal */
        current->exit_signaled = true;
        current->exit_signal = signal;
        
        /* Call task_exit with signal-based exit code */
        task_exit(128 + signal);
        
        /* task_exit should not return, but just in case... */
    }
    
    /* If no current task or task_exit returned, halt */
    serial_puts("ERROR: Failed to terminate user process, halting\n");
    while (1) {
        __asm__ volatile("wfi");
    }
}
