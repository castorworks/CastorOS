/**
 * @file syscall.S
 * @brief x86_64 系统调用汇编实现
 * 
 * x86_64 使用 SYSCALL 指令进行系统调用
 * 参数传递: RAX=syscall号, RDI=arg0, RSI=arg1, RDX=arg2, R10=arg3, R8=arg4, R9=arg5
 * 返回值: RAX
 * 
 * 注意: SYSCALL 会破坏 RCX 和 R11 寄存器
 * 
 * **Feature: multi-arch-support, Property 16: User Library System Call Instruction Correctness**
 * **Validates: Requirements 10.2**
 */

.section .text

/* ============================================================================
 * syscall0 - 无参数系统调用
 * uint64_t syscall0(uint64_t num)
 * ========================================================================== */
.global syscall0
.type syscall0, @function
syscall0:
    mov %rdi, %rax          /* syscall 号 */
    syscall                 /* 触发系统调用 */
    ret
.size syscall0, . - syscall0

/* ============================================================================
 * syscall1 - 1个参数系统调用
 * uint64_t syscall1(uint64_t num, uint64_t arg0)
 * ========================================================================== */
.global syscall1
.type syscall1, @function
syscall1:
    mov %rdi, %rax          /* syscall 号 */
    mov %rsi, %rdi          /* arg0 */
    syscall
    ret
.size syscall1, . - syscall1

/* ============================================================================
 * syscall2 - 2个参数系统调用
 * uint64_t syscall2(uint64_t num, uint64_t arg0, uint64_t arg1)
 * ========================================================================== */
.global syscall2
.type syscall2, @function
syscall2:
    mov %rdi, %rax          /* syscall 号 */
    mov %rsi, %rdi          /* arg0 */
    mov %rdx, %rsi          /* arg1 */
    syscall
    ret
.size syscall2, . - syscall2

/* ============================================================================
 * syscall3 - 3个参数系统调用
 * uint64_t syscall3(uint64_t num, uint64_t arg0, uint64_t arg1, uint64_t arg2)
 * ========================================================================== */
.global syscall3
.type syscall3, @function
syscall3:
    mov %rdi, %rax          /* syscall 号 */
    mov %rsi, %rdi          /* arg0 */
    mov %rdx, %rsi          /* arg1 */
    mov %rcx, %rdx          /* arg2 */
    syscall
    ret
.size syscall3, . - syscall3

/* ============================================================================
 * syscall4 - 4个参数系统调用
 * uint64_t syscall4(uint64_t num, uint64_t arg0, uint64_t arg1, uint64_t arg2,
 *                   uint64_t arg3)
 * ========================================================================== */
.global syscall4
.type syscall4, @function
syscall4:
    mov %rdi, %rax          /* syscall 号 */
    mov %rsi, %rdi          /* arg0 */
    mov %rdx, %rsi          /* arg1 */
    mov %rcx, %rdx          /* arg2 */
    mov %r8, %r10           /* arg3 (r10 用于 syscall, 因为 rcx 被 syscall 破坏) */
    syscall
    ret
.size syscall4, . - syscall4

/* ============================================================================
 * syscall5 - 5个参数系统调用
 * uint64_t syscall5(uint64_t num, uint64_t arg0, uint64_t arg1, uint64_t arg2,
 *                   uint64_t arg3, uint64_t arg4)
 * ========================================================================== */
.global syscall5
.type syscall5, @function
syscall5:
    mov %rdi, %rax          /* syscall 号 */
    mov %rsi, %rdi          /* arg0 */
    mov %rdx, %rsi          /* arg1 */
    mov %rcx, %rdx          /* arg2 */
    mov %r8, %r10           /* arg3 */
    mov %r9, %r8            /* arg4 */
    syscall
    ret
.size syscall5, . - syscall5

/* ============================================================================
 * syscall6 - 6个参数系统调用
 * uint64_t syscall6(uint64_t num, uint64_t arg0, uint64_t arg1, uint64_t arg2,
 *                   uint64_t arg3, uint64_t arg4, uint64_t arg5)
 * ========================================================================== */
.global syscall6
.type syscall6, @function
syscall6:
    mov %rdi, %rax          /* syscall 号 */
    mov %rsi, %rdi          /* arg0 */
    mov %rdx, %rsi          /* arg1 */
    mov %rcx, %rdx          /* arg2 */
    mov %r8, %r10           /* arg3 */
    mov %r9, %r8            /* arg4 */
    mov 8(%rsp), %r9        /* arg5 (从栈上获取) */
    syscall
    ret
.size syscall6, . - syscall6
