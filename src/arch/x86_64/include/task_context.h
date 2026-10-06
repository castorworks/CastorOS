#ifndef _ARCH_X86_64_TASK_CONTEXT_H_
#define _ARCH_X86_64_TASK_CONTEXT_H_

/**
 * @file task_context.h
 * @brief 一个任务里和架构有关的部分（x86_64）
 *
 * kernel/task.h 包含这个文件；每个架构在自己的 include 目录里各有一份，提供：
 *   - USER_SPACE_END、USER_STACK_TOP：用户地址空间的上界和用户栈区域的顶端（都不含）
 *   - cpu_context_t：任务不在 CPU 上时保存的寄存器，task_switch_context 读写它
 *   - hal_fp_state_t：用户任务的浮点/SIMD 寄存器，不在 cpu_context_t 里，因为内核自己
 *     不用它们，只在换一个用户任务上 CPU 时保存和恢复（hal::UserContext::fp_save / fp_restore）
 */

#include <types.h>

/** 用户空间结束地址（内核空间起始地址） */
#define USER_SPACE_END  0x80000000

/** 用户栈区域的顶端（不含） */
#define USER_STACK_TOP  USER_SPACE_END

/**
 * 任务切换时保存和恢复的 CPU 寄存器。通用代码不直接读写里面的字段，
 * 构造和改写现场都通过 hal::UserContext（task/user_context.cpp）。
 */
/* x86_64: 64-bit context structure */
typedef struct {
    /* General purpose registers */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    /* Instruction pointer */
    uint64_t rip;        ///< 指令指针

    /* Code segment */
    uint64_t cs;

    /* Flags register */
    uint64_t rflags;     ///< 标志寄存器

    /* Stack pointer */
    uint64_t rsp;        ///< 栈指针

    /* Stack segment */
    uint64_t ss;

    /* Page table base register */
    uint64_t cr3;        ///< 页目录物理地址
} __attribute__((packed)) cpu_context_t;

/* Compatibility aliases for x86_64 */
#define eip rip
#define esp rsp
#define eflags rflags
#define eax rax
#define ebx rbx
#define ecx rcx
#define edx rdx
#define esi rsi
#define edi rdi
#define ebp rbp


/**
 * 一个用户任务的浮点/SIMD 寄存器：x87、MMX、XMM、MXCSR，FXSAVE 的格式。
 * FXSAVE/FXRSTOR 要求这块内存 16 字节对齐。
 */
typedef struct {
    uint8_t data[512];
} __attribute__((aligned(16))) hal_fp_state_t;

#endif /* _ARCH_X86_64_TASK_CONTEXT_H_ */
