#ifndef _ARCH_ARM64_TASK_CONTEXT_H_
#define _ARCH_ARM64_TASK_CONTEXT_H_

/**
 * @file task_context.h
 * @brief 一个任务里和架构有关的部分（arm64）
 *
 * kernel/task.h 包含这个文件；每个架构在自己的 include 目录里各有一份，提供：
 *   - USER_SPACE_END、USER_STACK_TOP：用户地址空间的上界和用户栈区域的顶端（都不含）
 *   - cpu_context_t：任务不在 CPU 上时保存的寄存器，task_switch_context 读写它
 *   - hal_fp_state_t：用户任务的浮点/SIMD 寄存器，不在 cpu_context_t 里，因为内核自己
 *     不用它们，只在换一个用户任务上 CPU 时保存和恢复（hal::UserContext::fp_save / fp_restore）
 */

#include <types.h>

/* User space is in the TTBR0 region (0x0000_0000_0000_0000 - 0x0000_FFFF_FFFF_FFFF).
 * We use a more conservative limit for user stack placement */
#define USER_SPACE_END          0x0000800000000000ULL  /* 128TB - reasonable user space limit */
#define ARM64_USER_STACK_TOP    0x00007FFFFF000000ULL  /* User stack top (below 128TB) */

/** 用户栈区域的顶端（不含） */
#define USER_STACK_TOP  ARM64_USER_STACK_TOP

/**
 * 任务切换时保存和恢复的 CPU 寄存器。通用代码不直接读写里面的字段，
 * 构造和改写现场都通过 hal::UserContext（task/user_context.cpp）。
 */
/* ARM64: 64-bit context structure */
typedef struct {
    /* General purpose registers X0-X30 */
    uint64_t x[31];              /* X0-X30 */

    /* Stack pointer */
    uint64_t sp;

    /* Program counter - stored in ELR_EL1 */
    uint64_t pc;

    /* Processor state - stored in SPSR_EL1 */
    uint64_t pstate;

    /* User page table base register (TTBR0_EL1) */
    uint64_t ttbr0;

    /* Kernel stack top loaded into SP_EL1 before returning to EL0.
     * Layout must match arm64_context_t (arch/arm64/include/context.h). */
    uint64_t kernel_sp;
} __attribute__((packed, aligned(16))) cpu_context_t;

static_assert(sizeof(cpu_context_t) == 288, "cpu_context_t must match arm64_context_t");
static_assert(__builtin_offsetof(cpu_context_t, kernel_sp) == 280, "kernel_sp offset mismatch");

/* Compatibility aliases for ARM64 */
#define eip pc
#define esp sp
#define cr3 ttbr0

/* ARM64 PSTATE bits */
#define ARM64_PSTATE_EL0t   0x00    /* EL0 with SP_EL0 */
#define ARM64_PSTATE_EL1t   0x04    /* EL1 with SP_EL0 */
#define ARM64_PSTATE_EL1h   0x05    /* EL1 with SP_EL1 */


/**
 * 一个用户任务的浮点/SIMD 寄存器：V0-V31（各 128 位）、FPSR、FPCR。
 * 布局和 task/fp.S 里的偏移一致。
 */
typedef struct {
    uint64_t v[64];
    uint64_t fpsr;
    uint64_t fpcr;
} __attribute__((aligned(16))) hal_fp_state_t;

static_assert(__builtin_offsetof(hal_fp_state_t, fpsr) == 512, "fpsr offset must match fp.S");

#endif /* _ARCH_ARM64_TASK_CONTEXT_H_ */
