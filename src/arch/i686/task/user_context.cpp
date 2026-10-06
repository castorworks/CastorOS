/**
 * @file user_context.cpp
 * @brief i686 的任务现场（说明见 <hal/user_context.h>）
 *
 * 系统调用入口（syscall_asm.asm）在内核栈上保存的帧：
 *   frame[0] = DS，frame[1..7] = EAX EBX ECX EDX ESI EDI EBP，
 *   之后是 CPU 压入的 IRET 帧：frame[8..12] = EIP CS EFLAGS ESP SS
 */

#include <hal/user_context.h>
#include <kernel/gdt.h>
#include <lib/string.h>

#define USER_CS     (GDT_USER_CODE_SEGMENT | 3)
#define USER_DS     (GDT_USER_DATA_SEGMENT | 3)
#define EFLAGS_IF   0x202       /* 中断使能（bit 1 恒为 1） */

uintptr_t hal::UserContext::initial_sp(uintptr_t stack_top) {
    // 入口 _start 是按普通函数编译的，它假定自己是被 call 进来的：留出返回地址的位置
    return stack_top - sizeof(uintptr_t);
}

static void set_segments(cpu_context_t *ctx, uint32_t code, uint32_t data) {
    ctx->cs = code;
    ctx->ss = data;
    ctx->ds = data;
    ctx->es = data;
    ctx->fs = data;
    ctx->gs = data;
}

void hal::UserContext::init(cpu_context_t *ctx, uintptr_t entry, uintptr_t user_sp,
                            uintptr_t space, uintptr_t kernel_sp) {
    (void)kernel_sp;            // 内核栈记在 TSS 里，见 set_kernel_stack
    memset(ctx, 0, sizeof(*ctx));
    set_segments(ctx, USER_CS, USER_DS);
    ctx->eip = entry;
    ctx->esp = user_sp;
    ctx->eflags = EFLAGS_IF;
    ctx->cr3 = space;
}

void hal::UserContext::init_kernel(cpu_context_t *ctx, void (*entry)(void), uintptr_t kernel_sp, uintptr_t space) {
    memset(ctx, 0, sizeof(*ctx));
    set_segments(ctx, GDT_KERNEL_CODE_SEGMENT, GDT_KERNEL_DATA_SEGMENT);
    ctx->eflags = EFLAGS_IF;
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
    child->eax = 0;             // 子进程里 fork() 返回 0
    child->ebx = frame[2];
    child->ecx = frame[3];
    child->edx = frame[4];
    child->esi = frame[5];
    child->edi = frame[6];
    child->ebp = frame[7];
    child->eip = frame[8];
    child->esp = frame[11];
    // 标志位只留算术标志和方向位，中断使能强制打开；段选择子不信任帧里的值，一律用用户段
    child->eflags = (frame[10] & 0x00000CD5) | EFLAGS_IF;
    set_segments(child, USER_CS, USER_DS);
    child->cr3 = space;
}

void hal::UserContext::exec_return(uintptr_t *frame, uintptr_t entry, uintptr_t user_sp) {
    frame[0] = USER_DS;
    frame[8] = entry;
    frame[9] = USER_CS;
    frame[10] = EFLAGS_IF;
    frame[11] = user_sp;
    frame[12] = USER_DS;
}

void hal::UserContext::set_kernel_stack(uintptr_t kernel_sp) {
    tss_set_kernel_stack(kernel_sp);
}

/* ============================================================================
 * 浮点/SIMD 寄存器
 * ========================================================================== */

#define FXSAVE_FCW      0       /* x87 控制字（16 位） */
#define FXSAVE_MXCSR    24      /* SSE 控制/状态寄存器（32 位） */

void hal::UserContext::fp_reset(hal_fp_state_t *state) {
    // 和 fninit 之后的状态一样：寄存器全空，所有异常屏蔽，x87 用 64 位精度，就近舍入
    memset(state, 0, sizeof(*state));
    uint16_t fcw = 0x037F;
    uint32_t mxcsr = 0x1F80;
    memcpy(state->data + FXSAVE_FCW, &fcw, sizeof(fcw));
    memcpy(state->data + FXSAVE_MXCSR, &mxcsr, sizeof(mxcsr));
}

void hal::UserContext::fp_save(hal_fp_state_t *state) {
    __asm__ volatile("fxsave (%0)" : : "r"(state) : "memory");
}

void hal::UserContext::fp_restore(const hal_fp_state_t *state) {
    __asm__ volatile("fxrstor (%0)" : : "r"(state) : "memory");
}
