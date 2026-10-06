#ifndef _ARCH_I686_TASK_CONTEXT_H_
#define _ARCH_I686_TASK_CONTEXT_H_

/**
 * @file task_context.h
 * @brief 一个任务里和架构有关的部分（i686）
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
/* i686: 32-bit context structure */
typedef struct {
    /* 段寄存器 */
    uint16_t gs, _gs_padding;
    uint16_t fs, _fs_padding;
    uint16_t es, _es_padding;
    uint16_t ds, _ds_padding;

    /* 通用寄存器（按 PUSHA 顺序） */
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;  // PUSHA 会压入 ESP，但我们不使用它
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    /* 特殊寄存器 */
    uint32_t eip;        ///< 指令指针
    uint16_t cs, _cs_padding;
    uint32_t eflags;     ///< 标志寄存器

    /* 用户态栈指针（Ring 3 时使用） */
    uint32_t esp;        ///< 栈指针
    uint16_t ss, _ss_padding;

    /* 页目录基址寄存器 */
    uint32_t cr3;        ///< 页目录物理地址
} __attribute__((packed)) cpu_context_t;

/**
 * 一个用户任务的浮点/SIMD 寄存器：x87、MMX、XMM、MXCSR，FXSAVE 的格式。
 * FXSAVE/FXRSTOR 要求这块内存 16 字节对齐。
 */
typedef struct {
    uint8_t data[512];
} __attribute__((aligned(16))) hal_fp_state_t;

#endif /* _ARCH_I686_TASK_CONTEXT_H_ */
