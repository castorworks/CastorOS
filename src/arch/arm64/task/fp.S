/**
 * fp.S - 保存和恢复浮点/SIMD 寄存器（ARM64）
 *
 * 内核的 C++ 代码是用 -mgeneral-regs-only 编译的，不会碰这些寄存器，所以它们只在
 * 换一个用户任务上 CPU 时保存和恢复（kernel/task.cpp），这里是那两段指令。
 *
 * x0 指向 hal_fp_state_t（kernel/task.h）：
 *   +0     V0-V31，每个 16 字节
 *   +512   FPSR
 *   +520   FPCR
 */

    .arch armv8-a+fp+simd
    .text

    .global arm64_fp_save
    .type arm64_fp_save, %function
arm64_fp_save:
    stp     q0, q1, [x0, #0]
    stp     q2, q3, [x0, #32]
    stp     q4, q5, [x0, #64]
    stp     q6, q7, [x0, #96]
    stp     q8, q9, [x0, #128]
    stp     q10, q11, [x0, #160]
    stp     q12, q13, [x0, #192]
    stp     q14, q15, [x0, #224]
    stp     q16, q17, [x0, #256]
    stp     q18, q19, [x0, #288]
    stp     q20, q21, [x0, #320]
    stp     q22, q23, [x0, #352]
    stp     q24, q25, [x0, #384]
    stp     q26, q27, [x0, #416]
    stp     q28, q29, [x0, #448]
    stp     q30, q31, [x0, #480]
    mrs     x1, fpsr
    mrs     x2, fpcr
    str     x1, [x0, #512]
    str     x2, [x0, #520]
    ret
    .size arm64_fp_save, . - arm64_fp_save

    .global arm64_fp_restore
    .type arm64_fp_restore, %function
arm64_fp_restore:
    ldp     q0, q1, [x0, #0]
    ldp     q2, q3, [x0, #32]
    ldp     q4, q5, [x0, #64]
    ldp     q6, q7, [x0, #96]
    ldp     q8, q9, [x0, #128]
    ldp     q10, q11, [x0, #160]
    ldp     q12, q13, [x0, #192]
    ldp     q14, q15, [x0, #224]
    ldp     q16, q17, [x0, #256]
    ldp     q18, q19, [x0, #288]
    ldp     q20, q21, [x0, #320]
    ldp     q22, q23, [x0, #352]
    ldp     q24, q25, [x0, #384]
    ldp     q26, q27, [x0, #416]
    ldp     q28, q29, [x0, #448]
    ldp     q30, q31, [x0, #480]
    ldr     x1, [x0, #512]
    ldr     x2, [x0, #520]
    msr     fpsr, x1
    msr     fpcr, x2
    ret
    .size arm64_fp_restore, . - arm64_fp_restore
