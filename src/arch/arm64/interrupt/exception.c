/**
 * @file exception.c
 * @brief ARM64 Exception Handler Implementation
 * 
 * Implements the C-level exception handling for ARM64.
 * Called from the assembly vectors after register state is saved.
 * 
 * Requirements: 4.5, 6.2
 * 
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Validates: Requirements 6.2**
 */

#include "exception.h"
#include <hal/hal.h>
#include <types.h>

/* Forward declaration for serial output */
extern void serial_puts(const char *str);
extern void serial_put_hex64(uint64_t value);

/* Forward declaration for GIC functions */
extern void gic_handle_irq(void);
extern uint32_t gic_acknowledge_irq(void);
extern void gic_end_irq(uint32_t irq);

/* ============================================================================
 * Exception Class Names
 * ========================================================================== */

static const char *exception_class_names[] = {
    [ESR_EC_UNKNOWN]    = "Unknown",
    [ESR_EC_WFI_WFE]    = "WFI/WFE trapped",
    [ESR_EC_CP15_MCR]   = "MCR/MRC CP15",
    [ESR_EC_CP15_MCRR]  = "MCRR/MRRC CP15",
    [ESR_EC_CP14_MCR]   = "MCR/MRC CP14",
    [ESR_EC_CP14_LDC]   = "LDC/STC CP14",
    [ESR_EC_FP_ASIMD]   = "FP/ASIMD access",
    [ESR_EC_CP10_MCR]   = "MCR/MRC CP10",
    [ESR_EC_PAC]        = "PAC trapped",
    [ESR_EC_CP14_MRRC]  = "MRRC CP14",
    [ESR_EC_BTI]        = "BTI exception",
    [ESR_EC_ILLEGAL]    = "Illegal execution state",
    [ESR_EC_SVC32]      = "SVC (AArch32)",
    [ESR_EC_HVC32]      = "HVC (AArch32)",
    [ESR_EC_SMC32]      = "SMC (AArch32)",
    [ESR_EC_SVC64]      = "SVC (AArch64)",
    [ESR_EC_HVC64]      = "HVC (AArch64)",
    [ESR_EC_SMC64]      = "SMC (AArch64)",
    [ESR_EC_SYS64]      = "MSR/MRS/SYS trapped",
    [ESR_EC_SVE]        = "SVE access",
    [ESR_EC_ERET]       = "ERET trapped",
    [ESR_EC_FPAC]       = "FPAC exception",
    [ESR_EC_SME]        = "SME access",
    [ESR_EC_IABT_LOW]   = "Instruction abort (lower EL)",
    [ESR_EC_IABT_CUR]   = "Instruction abort (current EL)",
    [ESR_EC_PC_ALIGN]   = "PC alignment fault",
    [ESR_EC_DABT_LOW]   = "Data abort (lower EL)",
    [ESR_EC_DABT_CUR]   = "Data abort (current EL)",
    [ESR_EC_SP_ALIGN]   = "SP alignment fault",
    [ESR_EC_FP32]       = "FP exception (AArch32)",
    [ESR_EC_FP64]       = "FP exception (AArch64)",
    [ESR_EC_SERROR]     = "SError",
    [ESR_EC_BKPT_LOW]   = "Breakpoint (lower EL)",
    [ESR_EC_BKPT_CUR]   = "Breakpoint (current EL)",
    [ESR_EC_STEP_LOW]   = "Software step (lower EL)",
    [ESR_EC_STEP_CUR]   = "Software step (current EL)",
    [ESR_EC_WATCH_LOW]  = "Watchpoint (lower EL)",
    [ESR_EC_WATCH_CUR]  = "Watchpoint (current EL)",
    [ESR_EC_BKPT32]     = "BKPT (AArch32)",
    [ESR_EC_BRK64]      = "BRK (AArch64)",
};

static const char *fault_status_names[] = {
    [FSC_ADDR_L0]       = "Address size fault, level 0",
    [FSC_ADDR_L1]       = "Address size fault, level 1",
    [FSC_ADDR_L2]       = "Address size fault, level 2",
    [FSC_ADDR_L3]       = "Address size fault, level 3",
    [FSC_TRANS_L0]      = "Translation fault, level 0",
    [FSC_TRANS_L1]      = "Translation fault, level 1",
    [FSC_TRANS_L2]      = "Translation fault, level 2",
    [FSC_TRANS_L3]      = "Translation fault, level 3",
    [FSC_ACCESS_L1]     = "Access flag fault, level 1",
    [FSC_ACCESS_L2]     = "Access flag fault, level 2",
    [FSC_ACCESS_L3]     = "Access flag fault, level 3",
    [FSC_PERM_L1]       = "Permission fault, level 1",
    [FSC_PERM_L2]       = "Permission fault, level 2",
    [FSC_PERM_L3]       = "Permission fault, level 3",
    [FSC_SYNC_EXT]      = "Synchronous external abort",
    [FSC_SYNC_TAG]      = "Synchronous tag check fault",
    [FSC_ALIGN]         = "Alignment fault",
    [FSC_TLB_CONFLICT]  = "TLB conflict abort",
};

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
    if (ec < sizeof(exception_class_names) / sizeof(exception_class_names[0]) &&
        exception_class_names[ec] != NULL) {
        return exception_class_names[ec];
    }
    return "Unknown";
}

/**
 * @brief Get fault status code name
 */
const char *arm64_fault_status_name(uint32_t fsc) {
    if (fsc < sizeof(fault_status_names) / sizeof(fault_status_names[0]) &&
        fault_status_names[fsc] != NULL) {
        return fault_status_names[fsc];
    }
    return "Unknown";
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
extern void arm64_syscall_handler(arm64_regs_t *regs);

/**
 * @brief Handle synchronous exception
 */
static void handle_sync_exception(arm64_regs_t *regs, uint32_t source) {
    uint64_t esr = arm64_get_esr();
    uint64_t far = arm64_get_far();
    uint32_t ec = (esr >> ESR_EC_SHIFT) & 0x3F;
    uint32_t iss = esr & ESR_ISS_MASK;
    
    /* Handle SVC (system call) quickly without debug output */
    if (ec == ESR_EC_SVC64) {
        /* Dispatch to syscall handler
         * The syscall handler will extract arguments from the saved frame
         * and call syscall_dispatcher
         */
        arm64_syscall_handler(regs);
        
        /* Advance PC past the SVC instruction (4 bytes) */
        regs->elr += 4;
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
            /* Instruction abort */
            serial_puts("Instruction abort\n");
            serial_puts("Fault status: ");
            serial_puts(arm64_fault_status_name(iss & ESR_ISS_DFSC_MASK));
            serial_puts("\n");
            break;
            
        case ESR_EC_DABT_LOW:
        case ESR_EC_DABT_CUR:
            /* Data abort */
            serial_puts("Data abort\n");
            serial_puts("Fault status: ");
            serial_puts(arm64_fault_status_name(iss & ESR_ISS_DFSC_MASK));
            serial_puts("\n");
            serial_puts("Operation: ");
            serial_puts((iss & ESR_ISS_WNR) ? "Write" : "Read");
            serial_puts("\n");
            /* TODO: Handle page fault via VMM */
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
    (void)source;
    
    /* Acknowledge and handle IRQ via GIC */
    gic_handle_irq();
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
