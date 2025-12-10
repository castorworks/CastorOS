/**
 * @file syscall.S
 * @brief ARM64 系统调用汇编实现
 * 
 * ARM64 使用 SVC #0 指令进行系统调用
 * 参数传递: X8=syscall号, X0=arg0, X1=arg1, X2=arg2, X3=arg3, X4=arg4, X5=arg5
 * 返回值: X0
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
    mov x8, x0              /* syscall 号 */
    svc #0                  /* 触发系统调用 */
    ret
.size syscall0, . - syscall0

/* ============================================================================
 * syscall1 - 1个参数系统调用
 * uint64_t syscall1(uint64_t num, uint64_t arg0)
 * ========================================================================== */
.global syscall1
.type syscall1, @function
syscall1:
    mov x8, x0              /* syscall 号 */
    mov x0, x1              /* arg0 */
    svc #0
    ret
.size syscall1, . - syscall1

/* ============================================================================
 * syscall2 - 2个参数系统调用
 * uint64_t syscall2(uint64_t num, uint64_t arg0, uint64_t arg1)
 * ========================================================================== */
.global syscall2
.type syscall2, @function
syscall2:
    mov x8, x0              /* syscall 号 */
    mov x0, x1              /* arg0 */
    mov x1, x2              /* arg1 */
    svc #0
    ret
.size syscall2, . - syscall2

/* ============================================================================
 * syscall3 - 3个参数系统调用
 * uint64_t syscall3(uint64_t num, uint64_t arg0, uint64_t arg1, uint64_t arg2)
 * ========================================================================== */
.global syscall3
.type syscall3, @function
syscall3:
    mov x8, x0              /* syscall 号 */
    mov x0, x1              /* arg0 */
    mov x1, x2              /* arg1 */
    mov x2, x3              /* arg2 */
    svc #0
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
    mov x8, x0              /* syscall 号 */
    mov x0, x1              /* arg0 */
    mov x1, x2              /* arg1 */
    mov x2, x3              /* arg2 */
    mov x3, x4              /* arg3 */
    svc #0
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
    mov x8, x0              /* syscall 号 */
    mov x0, x1              /* arg0 */
    mov x1, x2              /* arg1 */
    mov x2, x3              /* arg2 */
    mov x3, x4              /* arg3 */
    mov x4, x5              /* arg4 */
    svc #0
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
    mov x8, x0              /* syscall 号 */
    mov x0, x1              /* arg0 */
    mov x1, x2              /* arg1 */
    mov x2, x3              /* arg2 */
    mov x3, x4              /* arg3 */
    mov x4, x5              /* arg4 */
    mov x5, x6              /* arg5 */
    svc #0
    ret
.size syscall6, . - syscall6
