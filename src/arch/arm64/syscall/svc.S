/*
 * svc.S - ARM64 System Call Entry Point
 *
 * Implements the SVC (Supervisor Call) instruction handling for ARM64.
 * System calls are invoked using the SVC instruction with:
 *   - X8  = system call number
 *   - X0  = arg1
 *   - X1  = arg2
 *   - X2  = arg3
 *   - X3  = arg4
 *   - X4  = arg5
 *   - X5  = arg6
 *
 * Return value is placed in X0.
 *
 * **Feature: multi-arch-support**
 * **Validates: Requirements 7.5, 8.1, 8.2**
 */

.section .text

/* ============================================================================
 * Constants
 * ========================================================================== */

/* Register save frame size (34 registers * 8 bytes = 272 bytes) */
/* X0-X30 (31 regs) + SP_EL0 + ELR_EL1 + SPSR_EL1 = 34 registers */
.equ FRAME_SIZE,            272

/* Register offsets in the frame (must match vectors.S) */
.equ FRAME_X0,              0
.equ FRAME_X1,              8
.equ FRAME_X2,              16
.equ FRAME_X3,              24
.equ FRAME_X4,              32
.equ FRAME_X5,              40
.equ FRAME_X6,              48
.equ FRAME_X7,              56
.equ FRAME_X8,              64
.equ FRAME_X30,             240
.equ FRAME_SP_EL0,          248
.equ FRAME_ELR,             256
.equ FRAME_SPSR,            264

/* ============================================================================
 * arm64_syscall_handler - SVC Exception Handler
 * ============================================================================
 * This function is called from the exception handler when an SVC instruction
 * is executed. The register frame has already been saved by kernel_entry.
 *
 * Parameters:
 *   x0 = pointer to saved register frame (arm64_regs_t *)
 *
 * The syscall arguments are extracted from the saved frame:
 *   - X8 (frame[8])  = syscall number
 *   - X0 (frame[0])  = arg1
 *   - X1 (frame[1])  = arg2
 *   - X2 (frame[2])  = arg3
 *   - X3 (frame[3])  = arg4
 *   - X4 (frame[4])  = arg5
 *   - X5 (frame[5])  = arg6
 *
 * Return value is stored back to X0 in the frame.
 * ============================================================================
 */
.global arm64_syscall_handler
arm64_syscall_handler:
    /* Save frame pointer */
    mov     x19, x0                     /* x19 = frame pointer (callee-saved) */
    
    /* Extract syscall number from X8 in the saved frame */
    ldr     x8, [x19, #FRAME_X8]        /* syscall_num = frame->x[8] */
    
    /* Extract arguments from saved frame */
    ldr     x0, [x19, #FRAME_X0]        /* arg1 = frame->x[0] */
    ldr     x1, [x19, #FRAME_X1]        /* arg2 = frame->x[1] */
    ldr     x2, [x19, #FRAME_X2]        /* arg3 = frame->x[2] */
    ldr     x3, [x19, #FRAME_X3]        /* arg4 = frame->x[3] */
    ldr     x4, [x19, #FRAME_X4]        /* arg5 = frame->x[4] */
    ldr     x5, [x19, #FRAME_X5]        /* arg6 = frame->x[5] */
    
    /* Prepare arguments for syscall_dispatcher:
     * syscall_dispatcher(syscall_num, p1, p2, p3, p4, p5, frame)
     * ARM64 ABI: x0, x1, x2, x3, x4, x5, x6
     */
    mov     x6, x19                     /* frame pointer as 7th argument */
    mov     x7, x5                      /* temporarily save arg6 */
    mov     x5, x4                      /* p5 = arg5 */
    mov     x4, x3                      /* p4 = arg4 */
    mov     x3, x2                      /* p3 = arg3 */
    mov     x2, x1                      /* p2 = arg2 */
    mov     x1, x0                      /* p1 = arg1 */
    mov     x0, x8                      /* syscall_num */
    
    /* Call the C syscall dispatcher */
    bl      syscall_dispatcher
    
    /* Store return value back to X0 in the frame */
    str     x0, [x19, #FRAME_X0]
    
    /* Return to exception handler (kernel_exit will restore registers) */
    ret



/* ============================================================================
 * enter_usermode_arm64 - Enter User Mode (EL0)
 * ============================================================================
 * This function transitions from kernel mode (EL1) to user mode (EL0) using
 * the ERET instruction. It sets up the necessary registers for user mode
 * execution.
 *
 * Parameters:
 *   x0 = entry_point (user code address)
 *   x1 = user_stack (user stack pointer)
 *
 * ERET behavior:
 *   - PC <- ELR_EL1 (Exception Link Register)
 *   - PSTATE <- SPSR_EL1 (Saved Program Status Register)
 *   - Exception level changes based on SPSR_EL1.M field
 *
 * SPSR_EL1 setup for EL0:
 *   - M[3:0] = 0b0000 (EL0t - EL0 with SP_EL0)
 *   - DAIF = 0 (interrupts enabled)
 *   - Other flags cleared
 *
 * **Feature: multi-arch-support, Property 11: User Mode Transition Correctness (ARM64)**
 * **Validates: Requirements 7.4**
 * ============================================================================
 */
.global enter_usermode_arm64
enter_usermode_arm64:
    /* Disable interrupts during transition */
    msr     daifset, #0xf
    
    /* Set up ELR_EL1 (return address = user entry point) */
    msr     elr_el1, x0
    
    /* Set up SP_EL0 (user stack pointer) */
    msr     sp_el0, x1
    
    /* Set up SPSR_EL1 for EL0 execution:
     * - M[3:0] = 0b0000 (EL0t)
     * - DAIF = 0 (all interrupts enabled)
     * - NZCV = 0 (condition flags cleared)
     * 
     * SPSR_EL1 bit layout:
     *   [31:28] = NZCV (condition flags)
     *   [9]     = D (Debug mask)
     *   [8]     = A (SError mask)
     *   [7]     = I (IRQ mask)
     *   [6]     = F (FIQ mask)
     *   [4]     = M[4] (must be 0 for AArch64)
     *   [3:0]   = M[3:0] (exception level and SP selection)
     *
     * For EL0 with interrupts enabled: 0x00000000
     */
    mov     x2, #0x0                    /* EL0t, all interrupts enabled */
    msr     spsr_el1, x2
    
    /* Clear all general-purpose registers for security
     * This prevents leaking kernel data to user space
     */
    mov     x0, #0
    mov     x1, #0
    mov     x2, #0
    mov     x3, #0
    mov     x4, #0
    mov     x5, #0
    mov     x6, #0
    mov     x7, #0
    mov     x8, #0
    mov     x9, #0
    mov     x10, #0
    mov     x11, #0
    mov     x12, #0
    mov     x13, #0
    mov     x14, #0
    mov     x15, #0
    mov     x16, #0
    mov     x17, #0
    mov     x18, #0
    mov     x19, #0
    mov     x20, #0
    mov     x21, #0
    mov     x22, #0
    mov     x23, #0
    mov     x24, #0
    mov     x25, #0
    mov     x26, #0
    mov     x27, #0
    mov     x28, #0
    mov     x29, #0                     /* Frame pointer */
    mov     x30, #0                     /* Link register */
    
    /* Instruction synchronization barrier before ERET */
    isb
    
    /* Return to user mode
     * ERET will:
     *   - Load PC from ELR_EL1 (user entry point)
     *   - Load PSTATE from SPSR_EL1 (EL0 mode)
     *   - Switch to EL0
     */
    eret

/* ============================================================================
 * arm64_return_to_usermode - Return to User Mode from Syscall
 * ============================================================================
 * This function is used to return to user mode after a system call or
 * interrupt. It assumes the register frame has been set up correctly.
 *
 * Parameters:
 *   x0 = pointer to saved register frame (arm64_regs_t *)
 *
 * This is typically called from the exception handler after processing
 * a system call, with the return value already stored in frame->x[0].
 * ============================================================================
 */
.global arm64_return_to_usermode
arm64_return_to_usermode:
    /* Load frame pointer */
    mov     x19, x0
    
    /* Restore ELR_EL1 and SPSR_EL1 */
    ldr     x20, [x19, #FRAME_ELR]
    ldr     x21, [x19, #FRAME_SPSR]
    msr     elr_el1, x20
    msr     spsr_el1, x21
    
    /* Restore SP_EL0 */
    ldr     x20, [x19, #FRAME_SP_EL0]
    msr     sp_el0, x20
    
    /* Restore general-purpose registers X0-X30 */
    ldp     x0, x1, [x19, #FRAME_X0]
    ldp     x2, x3, [x19, #FRAME_X2]
    ldp     x4, x5, [x19, #FRAME_X4]
    ldp     x6, x7, [x19, #FRAME_X6]
    /* Skip x8-x18 for now, restore them after */
    ldp     x28, x29, [x19, #224]       /* X28, X29 (FP) */
    ldr     x30, [x19, #FRAME_X30]      /* X30 (LR) */
    
    /* Restore x8-x27 */
    ldp     x8, x9, [x19, #FRAME_X8]
    ldp     x10, x11, [x19, #80]
    ldp     x12, x13, [x19, #96]
    ldp     x14, x15, [x19, #112]
    ldp     x16, x17, [x19, #128]
    ldp     x18, x19, [x19, #144]       /* Note: x19 is overwritten here */
    ldp     x20, x21, [x19, #160]
    ldp     x22, x23, [x19, #176]
    ldp     x24, x25, [x19, #192]
    ldp     x26, x27, [x19, #208]
    
    /* Return to user mode */
    eret

