/*
 * context.S - ARM64 Architecture-Specific Task Switching Code
 *
 * This file implements the low-level context switching routines for the
 * ARM64 (AArch64) architecture. It provides the HAL context switch
 * interface implementation.
 *
 * Requirements: 7.2, 7.3, 12.1
 *
 * **Feature: multi-arch-support, Property 9: Context Switch Register Preservation (ARM64)**
 * **Validates: Requirements 7.2**
 *
 * **Feature: multi-arch-support, Property 10: Address Space Switch Correctness (ARM64)**
 * **Validates: Requirements 7.3**
 *
 * Context Structure Offsets (must match arm64_context_t in context.h):
 *   x0-x30:   0-240   (31 registers × 8 bytes)
 *   sp:      248
 *   pc:      256      (ELR_EL1)
 *   pstate:  264      (SPSR_EL1)
 *   ttbr0:   272      (TTBR0_EL1)
 */

/* Context structure offsets */
.equ CTX_X0,        0
.equ CTX_X1,        8
.equ CTX_X2,        16
.equ CTX_X3,        24
.equ CTX_X4,        32
.equ CTX_X5,        40
.equ CTX_X6,        48
.equ CTX_X7,        56
.equ CTX_X8,        64
.equ CTX_X9,        72
.equ CTX_X10,       80
.equ CTX_X11,       88
.equ CTX_X12,       96
.equ CTX_X13,       104
.equ CTX_X14,       112
.equ CTX_X15,       120
.equ CTX_X16,       128
.equ CTX_X17,       136
.equ CTX_X18,       144
.equ CTX_X19,       152
.equ CTX_X20,       160
.equ CTX_X21,       168
.equ CTX_X22,       176
.equ CTX_X23,       184
.equ CTX_X24,       192
.equ CTX_X25,       200
.equ CTX_X26,       208
.equ CTX_X27,       216
.equ CTX_X28,       224
.equ CTX_X29,       232
.equ CTX_X30,       240
.equ CTX_SP,        248
.equ CTX_PC,        256
.equ CTX_PSTATE,    264
.equ CTX_TTBR0,     272

/* PSTATE values */
.equ PSTATE_EL0t,   0x00
.equ PSTATE_EL1h,   0x05

.section .text

/* ============================================================================
 * void hal_context_switch_asm(hal_context_t **old_ctx, hal_context_t *new_ctx)
 *
 * Performs a context switch from the current task to a new task.
 * This is the HAL implementation for ARM64 architecture.
 *
 * Parameters (AAPCS64 calling convention):
 *   x0 = old_ctx - Pointer to pointer where current context is saved
 *   x1 = new_ctx - Pointer to context to switch to
 *
 * This function:
 *   1. Saves all registers of the current task to *old_ctx
 *   2. Switches TTBR0_EL1 if necessary (address space switch)
 *   3. Restores all registers from new_ctx
 *   4. Returns to the new task's execution point
 * ========================================================================== */

.global hal_context_switch_asm
.type hal_context_switch_asm, @function
hal_context_switch_asm:
    /* DEBUG: Print entry message */
    stp     x0, x1, [sp, #-16]!
    stp     x30, xzr, [sp, #-16]!
    
    adr     x0, .ctx_switch_entry_msg
    bl      serial_puts
    
    ldp     x30, xzr, [sp], #16
    ldp     x0, x1, [sp], #16
    
    /* ========================================================================
     * Step 1: Save current task context
     * ======================================================================== */
    
    /* Check if old_ctx is NULL */
    cbz     x0, .load_new_context
    
    /* Load *old_ctx (pointer to context structure) */
    ldr     x2, [x0]
    cbz     x2, .load_new_context
    
    /* Save general purpose registers X0-X30 */
    /* Note: X0 and X1 contain our parameters, save them first */
    stp     x0, x1, [x2, #CTX_X0]
    stp     x2, x3, [x2, #CTX_X2]
    stp     x4, x5, [x2, #CTX_X4]
    stp     x6, x7, [x2, #CTX_X6]
    stp     x8, x9, [x2, #CTX_X8]
    stp     x10, x11, [x2, #CTX_X10]
    stp     x12, x13, [x2, #CTX_X12]
    stp     x14, x15, [x2, #CTX_X14]
    stp     x16, x17, [x2, #CTX_X16]
    stp     x18, x19, [x2, #CTX_X18]
    stp     x20, x21, [x2, #CTX_X20]
    stp     x22, x23, [x2, #CTX_X22]
    stp     x24, x25, [x2, #CTX_X24]
    stp     x26, x27, [x2, #CTX_X26]
    stp     x28, x29, [x2, #CTX_X28]
    str     x30, [x2, #CTX_X30]         /* Link register */
    
    /* Save stack pointer */
    mov     x3, sp
    str     x3, [x2, #CTX_SP]
    
    /* Save return address as PC (LR contains return address) */
    str     x30, [x2, #CTX_PC]
    
    /* Save PSTATE (use NZCV for now, kernel mode) */
    mrs     x3, nzcv
    mov     x4, #PSTATE_EL1h            /* EL1 with SP_EL1 */
    orr     x3, x3, x4
    str     x3, [x2, #CTX_PSTATE]
    
    /* Save TTBR0_EL1 (user page table) */
    mrs     x3, ttbr0_el1
    str     x3, [x2, #CTX_TTBR0]
    
    /* x1 still contains new_ctx, continue to load */

.load_new_context:
    /* ========================================================================
     * Step 2: Load new task context
     * ======================================================================== */
    
    /* DEBUG: Print load_new_context entry */
    stp     x0, x1, [sp, #-16]!
    stp     x30, xzr, [sp, #-16]!
    adr     x0, .load_ctx_msg
    bl      serial_puts
    ldp     x30, xzr, [sp], #16
    ldp     x0, x1, [sp], #16
    
    /* x1 = new_ctx */
    cbz     x1, .done
    
    /* DEBUG: Print TTBR switch */
    stp     x0, x1, [sp, #-16]!
    stp     x30, xzr, [sp, #-16]!
    adr     x0, .ttbr_switch_msg
    bl      serial_puts
    ldp     x30, xzr, [sp], #16
    ldp     x0, x1, [sp], #16
    
    /* Switch TTBR0_EL1 (address space) if different */
    /* Property 10: Address Space Switch Correctness */
    ldr     x2, [x1, #CTX_TTBR0]
    mrs     x3, ttbr0_el1
    cmp     x2, x3
    b.eq    .skip_ttbr_switch
    
    /* DEBUG: Print old and new TTBR0 values */
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!
    stp     x30, xzr, [sp, #-16]!
    
    adr     x0, .ttbr_old_msg
    bl      serial_puts
    ldp     x2, x3, [sp, #16]           /* Get saved x3 (old TTBR0) */
    mov     x0, x3
    bl      serial_put_hex64
    
    adr     x0, .ttbr_new_msg
    bl      serial_puts
    ldp     x2, x3, [sp, #16]           /* Get saved x2 (new TTBR0) */
    mov     x0, x2
    bl      serial_put_hex64
    
    adr     x0, .newline
    bl      serial_puts
    
    ldp     x30, xzr, [sp], #16
    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16
    
    /* Reload x2 with new TTBR0 value */
    ldr     x2, [x1, #CTX_TTBR0]
    
    /* 
     * Switch address space with proper barrier sequence.
     * ARM ARM recommends: DSB, write TTBR, ISB for TTBR changes.
     * We also invalidate TLB to ensure no stale entries.
     */
    dsb     ish                         /* Ensure all prior memory accesses complete */
    tlbi    vmalle1is                   /* Invalidate all TLB entries (inner shareable) */
    dsb     ish                         /* Wait for TLB invalidation to complete */
    isb                                 /* Synchronize context */
    
    msr     ttbr0_el1, x2               /* Switch to new page tables */
    isb                                 /* Ensure TTBR change takes effect */
    
    /* DEBUG: Print message after TTBR switch */
    stp     x0, x1, [sp, #-16]!
    stp     x30, xzr, [sp, #-16]!
    adr     x0, .ttbr_done_msg
    bl      serial_puts
    ldp     x30, xzr, [sp], #16
    ldp     x0, x1, [sp], #16

.skip_ttbr_switch:
    /* 
     * Check if returning to user mode or kernel mode.
     * Use callee-saved registers (x19-x28) to preserve values across function calls.
     * x19 = new_ctx pointer (preserved)
     * x20 = PSTATE value
     * x21 = M field
     */
    
    /* Save callee-saved registers we'll use */
    stp     x19, x20, [sp, #-16]!
    stp     x21, x30, [sp, #-16]!
    
    /* Save new_ctx in callee-saved register */
    mov     x19, x1
    
    /* Load PSTATE and extract M field */
    ldr     x20, [x19, #CTX_PSTATE]
    and     x21, x20, #0x0F             /* Extract M[3:0] field */
    
    /* DEBUG: Print PSTATE */
    adr     x0, .pstate_check_msg
    bl      serial_puts
    mov     x0, x20
    bl      serial_put_hex64
    adr     x0, .newline
    bl      serial_puts
    
    /* DEBUG: Print M field */
    adr     x0, .m_field_msg
    bl      serial_puts
    mov     x0, x21
    bl      serial_put_hex64
    adr     x0, .newline
    bl      serial_puts
    
    /* 
     * Compare M field and branch.
     * x21 still contains the M field value (callee-saved).
     * x19 still contains the context pointer (callee-saved).
     */
    cmp     x21, #PSTATE_EL0t
    
    /* Restore x1 from x19 before restoring callee-saved registers */
    mov     x1, x19
    
    /* Restore callee-saved registers */
    ldp     x21, x30, [sp], #16
    ldp     x19, x20, [sp], #16
    
    b.eq    .restore_user
    
    /* ========================================================================
     * Kernel mode restore (using RET)
     * ======================================================================== */
.restore_kernel:
    /* Restore stack pointer */
    ldr     x2, [x1, #CTX_SP]
    mov     sp, x2
    
    /* Load return address (PC) into x30 */
    ldr     x30, [x1, #CTX_PC]
    
    /* Restore general purpose registers X0-X29 */
    /* Restore X2-X29 first, then X0-X1 last */
    ldp     x2, x3, [x1, #CTX_X2]
    ldp     x4, x5, [x1, #CTX_X4]
    ldp     x6, x7, [x1, #CTX_X6]
    ldp     x8, x9, [x1, #CTX_X8]
    ldp     x10, x11, [x1, #CTX_X10]
    ldp     x12, x13, [x1, #CTX_X12]
    ldp     x14, x15, [x1, #CTX_X14]
    ldp     x16, x17, [x1, #CTX_X16]
    ldp     x18, x19, [x1, #CTX_X18]
    ldp     x20, x21, [x1, #CTX_X20]
    ldp     x22, x23, [x1, #CTX_X22]
    ldp     x24, x25, [x1, #CTX_X24]
    ldp     x26, x27, [x1, #CTX_X26]
    ldp     x28, x29, [x1, #CTX_X28]
    
    /* Restore X0-X1 last (X1 was our context pointer) */
    ldp     x0, x1, [x1, #CTX_X0]
    
    /* Return to new task */
    ret

    /* ========================================================================
     * User mode restore (using ERET)
     * ======================================================================== */
.restore_user:
    /* DEBUG: Print restore_user entry */
    stp     x0, x1, [sp, #-16]!
    stp     x30, xzr, [sp, #-16]!
    adr     x0, .restore_user_msg
    bl      serial_puts
    ldp     x30, xzr, [sp], #16
    ldp     x0, x1, [sp], #16
    
    /* 
     * CRITICAL: Set SP_EL1 to the task's kernel stack before ERET.
     * When an exception occurs in user mode (EL0), the CPU will use SP_EL1
     * as the kernel stack. We need to set this to the task's kernel stack.
     * 
     * The kernel stack top is stored in X28 of the context (callee-saved).
     * We use X28 as a convention to pass the kernel stack pointer.
     */
    ldr     x2, [x1, #CTX_X28]
    cbz     x2, .skip_sp_el1_setup      /* Skip if X28 is 0 */
    mov     sp, x2                       /* Set SP_EL1 to kernel stack */
    b       .do_eret_setup
.skip_sp_el1_setup:
    /* If no kernel stack in X28, use a default - this shouldn't happen */
    adrp    x2, stack_top
    add     x2, x2, :lo12:stack_top
    mov     sp, x2
.do_eret_setup:
    /* Debug: Save SP_EL1 value for later verification */
    mov     x20, sp                      /* Save SP_EL1 in x20 (will be restored later) */

    /* Set up ELR_EL1 (return address) */
    ldr     x2, [x1, #CTX_PC]
    msr     elr_el1, x2
    
    /* Set up SPSR_EL1 (saved program status) 
     * IMPORTANT: Ensure DAIF bits are clear (interrupts enabled in EL0)
     * SPSR_EL1[9:6] = DAIF, should be 0 for interrupts enabled
     */
    ldr     x2, [x1, #CTX_PSTATE]
    bic     x2, x2, #0x3c0              /* Clear DAIF bits to enable interrupts */
    msr     spsr_el1, x2
    
    /* Restore stack pointer for EL0 */
    ldr     x2, [x1, #CTX_SP]
    msr     sp_el0, x2
    
    /* DEBUG: Print state before ERET */
    /* Save x0, x1 which we'll need */
    stp     x0, x1, [sp, #-16]!
    
    /* Print "ERET:" */
    adr     x0, .eret_msg
    bl      serial_puts
    
    /* Print SP_EL1 */
    adr     x0, .sp_el1_msg
    bl      serial_puts
    mov     x0, x20
    bl      serial_put_hex64
    
    /* Print ELR_EL1 */
    adr     x0, .elr_msg
    bl      serial_puts
    mrs     x0, elr_el1
    bl      serial_put_hex64
    
    /* Print SPSR_EL1 */
    adr     x0, .spsr_msg
    bl      serial_puts
    mrs     x0, spsr_el1
    bl      serial_put_hex64
    
    /* Print SP_EL0 */
    adr     x0, .sp_el0_msg
    bl      serial_puts
    mrs     x0, sp_el0
    bl      serial_put_hex64
    
    /* Print newline */
    adr     x0, .newline
    bl      serial_puts
    
    /* Restore x0, x1 */
    ldp     x0, x1, [sp], #16
    
    /* Restore general purpose registers X2-X30 */
    ldp     x2, x3, [x1, #CTX_X2]
    ldp     x4, x5, [x1, #CTX_X4]
    ldp     x6, x7, [x1, #CTX_X6]
    ldp     x8, x9, [x1, #CTX_X8]
    ldp     x10, x11, [x1, #CTX_X10]
    ldp     x12, x13, [x1, #CTX_X12]
    ldp     x14, x15, [x1, #CTX_X14]
    ldp     x16, x17, [x1, #CTX_X16]
    ldp     x18, x19, [x1, #CTX_X18]
    ldp     x20, x21, [x1, #CTX_X20]
    ldp     x22, x23, [x1, #CTX_X22]
    ldp     x24, x25, [x1, #CTX_X24]
    ldp     x26, x27, [x1, #CTX_X26]
    ldp     x28, x29, [x1, #CTX_X28]
    ldr     x30, [x1, #CTX_X30]
    
    /* Restore X0-X1 last */
    ldp     x0, x1, [x1, #CTX_X0]
    
    /* Return to user mode */
    eret

.done:
    ret

.size hal_context_switch_asm, . - hal_context_switch_asm

/* ============================================================================
 * void hal_context_enter_kernel_thread(void)
 *
 * Entry point for newly created kernel threads.
 * The thread entry function address is expected in X19 (callee-saved).
 *
 * This function:
 *   1. Enables interrupts
 *   2. Calls the entry function (address in X19)
 *   3. Calls task_exit(0) when the function returns
 * ========================================================================== */

.global hal_context_enter_kernel_thread
.type hal_context_enter_kernel_thread, @function
hal_context_enter_kernel_thread:
    /* Enable interrupts */
    msr     daifclr, #0xf
    
    /* Get entry function address from X19 */
    mov     x0, x19
    
    /* Call the entry function */
    blr     x0
    
    /* If the function returns, exit the task */
    mov     x0, #0                      /* Exit code = 0 */
    bl      task_exit
    
    /* Should never reach here */
    msr     daifset, #0xf               /* Disable interrupts */
1:
    wfi
    b       1b

.size hal_context_enter_kernel_thread, . - hal_context_enter_kernel_thread

/* ============================================================================
 * Legacy compatibility symbols
 * These are aliases for backward compatibility with existing code
 * ========================================================================== */

.global task_switch_context
.type task_switch_context, @function
task_switch_context:
    b       hal_context_switch_asm
.size task_switch_context, . - task_switch_context

.global task_enter_kernel_thread
.type task_enter_kernel_thread, @function
task_enter_kernel_thread:
    b       hal_context_enter_kernel_thread
.size task_enter_kernel_thread, . - task_enter_kernel_thread

/* ============================================================================
 * Debug string constants
 * ========================================================================== */

.section .rodata
.eret_msg:
    .asciz "ERET: "
.sp_el1_msg:
    .asciz "SP_EL1="
.elr_msg:
    .asciz " ELR="
.spsr_msg:
    .asciz " SPSR="
.sp_el0_msg:
    .asciz " SP_EL0="
.newline:
    .asciz "\n"
.pstate_check_msg:
    .asciz "CTX_PSTATE="
.ctx_switch_entry_msg:
    .asciz "[CTX_SWITCH] Entry\n"
.load_ctx_msg:
    .asciz "[CTX_SWITCH] load_new_context\n"
.ttbr_switch_msg:
    .asciz "[CTX_SWITCH] TTBR switch\n"
.restore_user_msg:
    .asciz "[CTX_SWITCH] restore_user\n"
.m_field_msg:
    .asciz "M_FIELD="
.ttbr_old_msg:
    .asciz "TTBR0_old="
.ttbr_new_msg:
    .asciz " TTBR0_new="
.ttbr_done_msg:
    .asciz "[CTX_SWITCH] TTBR switch done\n"
