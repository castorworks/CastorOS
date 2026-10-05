/**
 * @file user_context.cpp
 * @brief arm64 的任务现场（说明见 <hal/user_context.h>）
 *
 * 异常入口（vectors.S 的 kernel_entry）在内核栈上保存的帧：
 *   frame[0..30] = X0-X30，frame[31] = SP_EL0，frame[32] = ELR_EL1（返回地址），
 *   frame[33] = SPSR_EL1（返回后的 PSTATE）
 */

#include <hal/user_context.h>
#include <lib/string.h>

uintptr_t hal::UserContext::initial_sp(uintptr_t stack_top) {
    return stack_top - 16;      // AAPCS64：栈指针 16 字节对齐
}

void hal::UserContext::init(cpu_context_t *ctx, uintptr_t entry, uintptr_t user_sp,
                            uintptr_t space, uintptr_t kernel_sp) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->pc = entry;
    ctx->sp = user_sp;
    ctx->pstate = ARM64_PSTATE_EL0t;    // 用户态，中断使能
    ctx->ttbr0 = space;
    ctx->kernel_sp = kernel_sp;         // 返回用户态前 context_asm.S 用它设置 SP_EL1
}

void hal::UserContext::init_kernel(cpu_context_t *ctx, void (*entry)(void), uintptr_t kernel_sp, uintptr_t space) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->sp = kernel_sp;
    ctx->pc = (uintptr_t)task_enter_kernel_thread;
    ctx->x[19] = (uintptr_t)entry;      // task_enter_kernel_thread 从 X19 取真正的入口函数
    ctx->pstate = ARM64_PSTATE_EL1h;
    ctx->ttbr0 = space;
}

void hal::UserContext::fork(cpu_context_t *child, const uintptr_t *frame, uintptr_t space, uintptr_t kernel_sp) {
    memset(child, 0, sizeof(*child));
    child->x[0] = 0;                    // 子进程里 fork() 返回 0
    for (int i = 1; i < 31; i++) {
        child->x[i] = frame[i];
    }
    child->sp = frame[31];
    child->pc = frame[32];
    child->pstate = ARM64_PSTATE_EL0t;  // 不照抄帧里的 PSTATE：强制回到用户态、开中断
    child->ttbr0 = space;
    child->kernel_sp = kernel_sp;
}

void hal::UserContext::exec_return(uintptr_t *frame, uintptr_t entry, uintptr_t user_sp) {
    for (int i = 0; i < 31; i++) {
        frame[i] = 0;                   // 旧程序的寄存器内容不带进新程序
    }
    frame[31] = user_sp;
    frame[32] = entry;
    frame[33] = ARM64_PSTATE_EL0t;
}

void hal::UserContext::set_kernel_stack(uintptr_t kernel_sp) {
    (void)kernel_sp;            // 内核栈随现场一起保存和恢复（cpu_context_t::kernel_sp）
}
