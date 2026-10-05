/**
 * @file user_context.cpp
 * @brief x86_64 的任务现场（说明见 <hal/user_context.h>）
 *
 * 系统调用入口（syscall64_asm.asm）在内核栈上保存的帧：
 *   frame[0..7]  = R15 R14 R13 R12 R11 R10 R9 R8
 *   frame[8..14] = RBP RDI RSI RDX RCX RBX RAX
 *   frame[15]    = 用户 RSP
 * SYSCALL 指令把返回地址放进 RCX、把 RFLAGS 放进 R11，SYSRET 再从这两个寄存器取回去。
 */

#include <hal/user_context.h>
#include <kernel/gdt.h>
#include <lib/string.h>

/* 定义在 arch/x86_64/syscall/syscall64.cpp */
extern void hal_syscall_set_kernel_stack(uint64_t stack_ptr);

#define USER_CS     (GDT_USER_CODE_SEGMENT | 3)
#define USER_DS     (GDT_USER_DATA_SEGMENT | 3)
#define RFLAGS_IF   0x202       /* 中断使能（bit 1 恒为 1） */

uintptr_t hal::UserContext::initial_sp(uintptr_t stack_top) {
    // 入口 _start 是按普通函数编译的，它假定自己是被 call 进来的：栈顶留出一个返回地址的
    // 位置，函数体内的栈才是 16 字节对齐的（编译器会对栈上的对象用 movaps，没对齐就是 #GP）
    return stack_top - sizeof(uintptr_t);
}

void hal::UserContext::init(cpu_context_t *ctx, uintptr_t entry, uintptr_t user_sp,
                            uintptr_t space, uintptr_t kernel_sp) {
    (void)kernel_sp;            // 内核栈记在 TSS 和系统调用入口里，见 set_kernel_stack
    memset(ctx, 0, sizeof(*ctx));
    ctx->cs = USER_CS;
    ctx->ss = USER_DS;
    ctx->eip = entry;
    ctx->esp = user_sp;
    ctx->eflags = RFLAGS_IF;
    ctx->cr3 = space;
}

void hal::UserContext::init_kernel(cpu_context_t *ctx, void (*entry)(void), uintptr_t kernel_sp, uintptr_t space) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->cs = GDT_KERNEL_CODE_SEGMENT;
    ctx->ss = GDT_KERNEL_DATA_SEGMENT;
    ctx->eflags = RFLAGS_IF;
    ctx->cr3 = space;
    // task_enter_kernel_thread 从栈顶弹出真正的入口函数
    uintptr_t *stack = (uintptr_t *)kernel_sp;
    stack[-1] = (uintptr_t)entry;
    ctx->esp = (uintptr_t)&stack[-1];
    ctx->eip = (uintptr_t)task_enter_kernel_thread;
}

void hal::UserContext::fork(cpu_context_t *child, const uintptr_t *frame, uintptr_t space, uintptr_t kernel_sp) {
    (void)kernel_sp;
    memset(child, 0, sizeof(*child));
    // R12-R15、RBX、RBP 是被调用者保存的：子进程从 fork() 返回后，调用者放在里面的值
    // 必须和父进程一致；其余的也照抄
    child->r15 = frame[0];
    child->r14 = frame[1];
    child->r13 = frame[2];
    child->r12 = frame[3];
    child->r11 = frame[4];
    child->r10 = frame[5];
    child->r9  = frame[6];
    child->r8  = frame[7];
    child->ebp = frame[8];
    child->edi = frame[9];
    child->esi = frame[10];
    child->edx = frame[11];
    child->ecx = frame[12];
    child->ebx = frame[13];
    child->eax = 0;             // 子进程里 fork() 返回 0
    child->eip = frame[12];     // RCX = 返回地址
    child->esp = frame[15];
    // 标志位只留算术标志和方向位，中断使能强制打开
    child->eflags = (frame[4] & 0x00000CD5) | RFLAGS_IF;
    child->cs = USER_CS;
    child->ss = USER_DS;
    child->cr3 = space;
}

void hal::UserContext::exec_return(uintptr_t *frame, uintptr_t entry, uintptr_t user_sp) {
    frame[12] = entry;          // RCX：SYSRET 的返回地址
    frame[4]  = RFLAGS_IF;      // R11：SYSRET 恢复的 RFLAGS
    frame[15] = user_sp;
}

void hal::UserContext::set_kernel_stack(uintptr_t kernel_sp) {
    tss_set_kernel_stack(kernel_sp);                    // 中断和异常
    hal_syscall_set_kernel_stack((uint64_t)kernel_sp);  // SYSCALL 指令不经过 TSS
}
