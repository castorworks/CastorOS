/*
 * vectors.S - ARM64 Exception Vector Table
 *
 * Implements the exception vector table (VBAR_EL1) for ARM64.
 * Handles synchronous exceptions, IRQs, FIQs, and SErrors.
 *
 * Requirements: 4.5
 *
 * ARM64 Exception Vector Table Layout:
 *   - 4 exception types: Synchronous, IRQ, FIQ, SError
 *   - 4 exception sources: Current EL with SP0, Current EL with SPx,
 *                          Lower EL using AArch64, Lower EL using AArch32
 *   - Total: 16 vectors, each 128 bytes (0x80)
 *
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Validates: Requirements 6.2**
 */

.section .text

/* ============================================================================
 * Constants
 * ========================================================================== */

/* Exception types */
.equ EXCEPTION_SYNC,        0
.equ EXCEPTION_IRQ,         1
.equ EXCEPTION_FIQ,         2
.equ EXCEPTION_SERROR,      3

/* Exception sources */
.equ EXCEPTION_FROM_EL1_SP0,    0
.equ EXCEPTION_FROM_EL1_SPX,    1
.equ EXCEPTION_FROM_EL0_64,     2
.equ EXCEPTION_FROM_EL0_32,     3

/* Register save frame size (34 registers * 8 bytes = 272 bytes) */
/* X0-X30 (31 regs) + SP_EL0 + ELR_EL1 + SPSR_EL1 = 34 registers */
.equ FRAME_SIZE,            272

/* Register offsets in the frame */
.equ FRAME_X0,              0
.equ FRAME_X1,              8
.equ FRAME_X2,              16
.equ FRAME_X3,              24
.equ FRAME_X4,              32
.equ FRAME_X5,              40
.equ FRAME_X6,              48
.equ FRAME_X7,              56
.equ FRAME_X8,              64
.equ FRAME_X9,              72
.equ FRAME_X10,             80
.equ FRAME_X11,             88
.equ FRAME_X12,             96
.equ FRAME_X13,             104
.equ FRAME_X14,             112
.equ FRAME_X15,             120
.equ FRAME_X16,             128
.equ FRAME_X17,             136
.equ FRAME_X18,             144
.equ FRAME_X19,             152
.equ FRAME_X20,             160
.equ FRAME_X21,             168
.equ FRAME_X22,             176
.equ FRAME_X23,             184
.equ FRAME_X24,             192
.equ FRAME_X25,             200
.equ FRAME_X26,             208
.equ FRAME_X27,             216
.equ FRAME_X28,             224
.equ FRAME_X29,             232
.equ FRAME_X30,             240
.equ FRAME_SP_EL0,          248
.equ FRAME_ELR,             256
.equ FRAME_SPSR,            264

/* ============================================================================
 * Macros for saving and restoring registers
 * MUST be defined before they are used in the vector table
 * ========================================================================== */

/*
 * kernel_entry - Save all registers on exception entry
 *
 * Saves all general-purpose registers (X0-X30), SP_EL0, ELR_EL1, and SPSR_EL1
 * to the kernel stack. This creates a complete snapshot of the CPU state
 * that can be restored on exception return.
 *
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Validates: Requirements 6.2**
 */
.macro kernel_entry
    /* 
     * Simple approach: Trust that SP is valid when we enter from EL1.
     * For EL0 exceptions, SP_EL1 is used automatically.
     * No SP validation to avoid corrupting registers.
     */
    
    /* Allocate stack frame */
    sub     sp, sp, #FRAME_SIZE

    /* Save general-purpose registers X0-X29 */
    stp     x0, x1, [sp, #FRAME_X0]
    stp     x2, x3, [sp, #FRAME_X2]
    stp     x4, x5, [sp, #FRAME_X4]
    stp     x6, x7, [sp, #FRAME_X6]
    stp     x8, x9, [sp, #FRAME_X8]
    stp     x10, x11, [sp, #FRAME_X10]
    stp     x12, x13, [sp, #FRAME_X12]
    stp     x14, x15, [sp, #FRAME_X14]
    stp     x16, x17, [sp, #FRAME_X16]
    stp     x18, x19, [sp, #FRAME_X18]
    stp     x20, x21, [sp, #FRAME_X20]
    stp     x22, x23, [sp, #FRAME_X22]
    stp     x24, x25, [sp, #FRAME_X24]
    stp     x26, x27, [sp, #FRAME_X26]
    stp     x28, x29, [sp, #FRAME_X28]

    /* Save X30 (link register) and SP_EL0 */
    mrs     x21, sp_el0
    stp     x30, x21, [sp, #FRAME_X30]

    /* Save ELR_EL1 (return address) and SPSR_EL1 (saved program status) */
    mrs     x22, elr_el1
    mrs     x23, spsr_el1
    stp     x22, x23, [sp, #FRAME_ELR]
.endm

/*
 * kernel_exit - Restore all registers on exception return
 *
 * Restores all registers from the stack frame and returns from exception
 * using ERET instruction.
 *
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Validates: Requirements 6.2**
 */
.macro kernel_exit
    /* Restore ELR_EL1 and SPSR_EL1 */
    ldp     x22, x23, [sp, #FRAME_ELR]
    msr     elr_el1, x22
    msr     spsr_el1, x23

    /* Restore X30 and SP_EL0 */
    ldp     x30, x21, [sp, #FRAME_X30]
    msr     sp_el0, x21

    /* Restore general-purpose registers X0-X29 */
    ldp     x28, x29, [sp, #FRAME_X28]
    ldp     x26, x27, [sp, #FRAME_X26]
    ldp     x24, x25, [sp, #FRAME_X24]
    ldp     x22, x23, [sp, #FRAME_X22]
    ldp     x20, x21, [sp, #FRAME_X20]
    ldp     x18, x19, [sp, #FRAME_X18]
    ldp     x16, x17, [sp, #FRAME_X16]
    ldp     x14, x15, [sp, #FRAME_X14]
    ldp     x12, x13, [sp, #FRAME_X12]
    ldp     x10, x11, [sp, #FRAME_X10]
    ldp     x8, x9, [sp, #FRAME_X8]
    ldp     x6, x7, [sp, #FRAME_X6]
    ldp     x4, x5, [sp, #FRAME_X4]
    ldp     x2, x3, [sp, #FRAME_X2]
    ldp     x0, x1, [sp, #FRAME_X0]

    /* Deallocate stack frame */
    add     sp, sp, #FRAME_SIZE

    /* Return from exception */
    eret
.endm

/* ============================================================================
 * Exception Vector Table
 * ========================================================================== */

/*
 * The vector table must be 2KB aligned (11 bits).
 * Each vector entry is 128 bytes (0x80).
 */
.balign 2048
.global exception_vectors
exception_vectors:

/* ============================================================================
 * Current EL with SP0 (EL1t) - Should not happen in normal operation
 * ========================================================================== */

.balign 128
el1t_sync:
    b       el1t_sync_handler

.balign 128
el1t_irq:
    b       el1t_irq_handler

.balign 128
el1t_fiq:
    b       el1t_fiq_handler

.balign 128
el1t_serror:
    b       el1t_serror_handler

/* ============================================================================
 * Current EL with SPx (EL1h) - Kernel mode exceptions
 * Vector entries must be small (branch only) to fit in 128 bytes
 * ========================================================================== */

.balign 128
el1h_sync:
    b       el1h_sync_handler

.balign 128
el1h_irq:
    b       el1h_irq_handler

.balign 128
el1h_fiq:
    b       el1h_fiq_handler

.balign 128
el1h_serror:
    b       el1h_serror_handler

/* ============================================================================
 * Lower EL using AArch64 (EL0) - User mode exceptions
 * Vector entries must be small (branch only) to fit in 128 bytes
 * ========================================================================== */

.balign 128
el0_64_sync:
    b       el0_64_sync_handler

.balign 128
el0_64_irq:
    b       el0_64_irq_handler

.balign 128
el0_64_fiq:
    b       el0_64_fiq_handler

.balign 128
el0_64_serror:
    b       el0_64_serror_handler

/* ============================================================================
 * Lower EL using AArch32 - Not supported
 * ========================================================================== */

.balign 128
el0_32_sync:
    b       el0_32_sync             /* Hang - AArch32 not supported */

.balign 128
el0_32_irq:
    b       el0_32_irq

.balign 128
el0_32_fiq:
    b       el0_32_fiq

.balign 128
el0_32_serror:
    b       el0_32_serror

/* ============================================================================
 * EL1t Exception Handlers (SP0 - should not happen)
 * ========================================================================== */

el1t_sync_handler:
    kernel_entry
    mov     x0, sp
    mov     x1, #EXCEPTION_SYNC
    mov     x2, #EXCEPTION_FROM_EL1_SP0
    bl      arm64_exception_handler
    kernel_exit

el1t_irq_handler:
    kernel_entry
    mov     x0, sp
    mov     x1, #EXCEPTION_IRQ
    mov     x2, #EXCEPTION_FROM_EL1_SP0
    bl      arm64_exception_handler
    kernel_exit

el1t_fiq_handler:
    kernel_entry
    mov     x0, sp
    mov     x1, #EXCEPTION_FIQ
    mov     x2, #EXCEPTION_FROM_EL1_SP0
    bl      arm64_exception_handler
    kernel_exit

el1t_serror_handler:
    kernel_entry
    mov     x0, sp
    mov     x1, #EXCEPTION_SERROR
    mov     x2, #EXCEPTION_FROM_EL1_SP0
    bl      arm64_exception_handler
    kernel_exit

/* ============================================================================
 * EL1h Exception Handlers (SPx - normal kernel mode)
 * ========================================================================== */

el1h_sync_handler:
    kernel_entry
    mov     x0, sp
    mov     x1, #EXCEPTION_SYNC
    mov     x2, #EXCEPTION_FROM_EL1_SPX
    bl      arm64_exception_handler
    kernel_exit

el1h_irq_handler:
    kernel_entry
    mov     x0, sp
    mov     x1, #EXCEPTION_IRQ
    mov     x2, #EXCEPTION_FROM_EL1_SPX
    bl      arm64_exception_handler
    kernel_exit

el1h_fiq_handler:
    kernel_entry
    mov     x0, sp
    mov     x1, #EXCEPTION_FIQ
    mov     x2, #EXCEPTION_FROM_EL1_SPX
    bl      arm64_exception_handler
    kernel_exit

el1h_serror_handler:
    kernel_entry
    mov     x0, sp
    mov     x1, #EXCEPTION_SERROR
    mov     x2, #EXCEPTION_FROM_EL1_SPX
    bl      arm64_exception_handler
    kernel_exit

/* ============================================================================
 * EL0 (64-bit) Exception Handlers - User mode
 * ========================================================================== */

el0_64_sync_handler:
    kernel_entry
    mov     x0, sp
    mov     x1, #EXCEPTION_SYNC
    mov     x2, #EXCEPTION_FROM_EL0_64
    bl      arm64_exception_handler
    kernel_exit

el0_64_irq_handler:
    /* Debug: We should see this if EL0 IRQs are being handled */
    kernel_entry
    mov     x0, sp
    mov     x1, #EXCEPTION_IRQ
    mov     x2, #EXCEPTION_FROM_EL0_64
    bl      arm64_exception_handler
    kernel_exit

el0_64_fiq_handler:
    kernel_entry
    mov     x0, sp
    mov     x1, #EXCEPTION_FIQ
    mov     x2, #EXCEPTION_FROM_EL0_64
    bl      arm64_exception_handler
    kernel_exit

el0_64_serror_handler:
    kernel_entry
    mov     x0, sp
    mov     x1, #EXCEPTION_SERROR
    mov     x2, #EXCEPTION_FROM_EL0_64
    bl      arm64_exception_handler
    kernel_exit

/* ============================================================================
 * Vector Table Installation
 * ========================================================================== */

/*
 * arm64_install_vectors - Install the exception vector table
 *
 * Sets VBAR_EL1 to point to our exception vector table.
 * Called during interrupt system initialization.
 */
.global arm64_install_vectors
arm64_install_vectors:
    adr     x0, exception_vectors
    msr     vbar_el1, x0
    isb
    ret

/* ============================================================================
 * Helper Functions
 * ========================================================================== */

/*
 * arm64_get_esr - Get Exception Syndrome Register value
 *
 * Returns the ESR_EL1 value which contains information about the exception.
 */
.global arm64_get_esr
arm64_get_esr:
    mrs     x0, esr_el1
    ret

/*
 * arm64_get_far - Get Fault Address Register value
 *
 * Returns the FAR_EL1 value which contains the faulting address.
 */
.global arm64_get_far
arm64_get_far:
    mrs     x0, far_el1
    ret

/*
 * arm64_get_elr - Get Exception Link Register value
 *
 * Returns the ELR_EL1 value which contains the return address.
 */
.global arm64_get_elr
arm64_get_elr:
    mrs     x0, elr_el1
    ret

/*
 * arm64_get_spsr - Get Saved Program Status Register value
 *
 * Returns the SPSR_EL1 value which contains the saved processor state.
 */
.global arm64_get_spsr
arm64_get_spsr:
    mrs     x0, spsr_el1
    ret
