/**
 * @file syscall.S
 * @brief i686 系统调用汇编实现
 * 
 * i686 使用 INT 0x80 进行系统调用
 * 参数传递: EAX=syscall号, EBX=arg0, ECX=arg1, EDX=arg2, ESI=arg3, EDI=arg4, EBP=arg5
 * 返回值: EAX
 * 
 * **Feature: multi-arch-support, Property 16: User Library System Call Instruction Correctness**
 * **Validates: Requirements 10.2**
 */

.section .text

/* ============================================================================
 * syscall0 - 无参数系统调用
 * uint32_t syscall0(uint32_t num)
 * ========================================================================== */
.global syscall0
.type syscall0, @function
syscall0:
    push %ebx               /* 保存被调用者保存寄存器 */
    mov 8(%esp), %eax       /* syscall 号 */
    xor %ebx, %ebx          /* arg0 = 0 */
    xor %ecx, %ecx          /* arg1 = 0 */
    xor %edx, %edx          /* arg2 = 0 */
    int $0x80               /* 触发系统调用 */
    pop %ebx                /* 恢复寄存器 */
    ret
.size syscall0, . - syscall0

/* ============================================================================
 * syscall1 - 1个参数系统调用
 * uint32_t syscall1(uint32_t num, uint32_t arg0)
 * ========================================================================== */
.global syscall1
.type syscall1, @function
syscall1:
    push %ebx
    mov 8(%esp), %eax       /* syscall 号 */
    mov 12(%esp), %ebx      /* arg0 */
    xor %ecx, %ecx
    xor %edx, %edx
    int $0x80
    pop %ebx
    ret
.size syscall1, . - syscall1

/* ============================================================================
 * syscall2 - 2个参数系统调用
 * uint32_t syscall2(uint32_t num, uint32_t arg0, uint32_t arg1)
 * ========================================================================== */
.global syscall2
.type syscall2, @function
syscall2:
    push %ebx
    mov 8(%esp), %eax       /* syscall 号 */
    mov 12(%esp), %ebx      /* arg0 */
    mov 16(%esp), %ecx      /* arg1 */
    xor %edx, %edx
    int $0x80
    pop %ebx
    ret
.size syscall2, . - syscall2

/* ============================================================================
 * syscall3 - 3个参数系统调用
 * uint32_t syscall3(uint32_t num, uint32_t arg0, uint32_t arg1, uint32_t arg2)
 * ========================================================================== */
.global syscall3
.type syscall3, @function
syscall3:
    push %ebx
    mov 8(%esp), %eax       /* syscall 号 */
    mov 12(%esp), %ebx      /* arg0 */
    mov 16(%esp), %ecx      /* arg1 */
    mov 20(%esp), %edx      /* arg2 */
    int $0x80
    pop %ebx
    ret
.size syscall3, . - syscall3

/* ============================================================================
 * syscall4 - 4个参数系统调用
 * uint32_t syscall4(uint32_t num, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3)
 * ========================================================================== */
.global syscall4
.type syscall4, @function
syscall4:
    push %ebx
    push %esi
    mov 12(%esp), %eax      /* syscall 号 */
    mov 16(%esp), %ebx      /* arg0 */
    mov 20(%esp), %ecx      /* arg1 */
    mov 24(%esp), %edx      /* arg2 */
    mov 28(%esp), %esi      /* arg3 */
    int $0x80
    pop %esi
    pop %ebx
    ret
.size syscall4, . - syscall4

/* ============================================================================
 * syscall5 - 5个参数系统调用
 * uint32_t syscall5(uint32_t num, uint32_t arg0, uint32_t arg1, uint32_t arg2, 
 *                   uint32_t arg3, uint32_t arg4)
 * ========================================================================== */
.global syscall5
.type syscall5, @function
syscall5:
    push %ebx
    push %esi
    push %edi
    mov 16(%esp), %eax      /* syscall 号 */
    mov 20(%esp), %ebx      /* arg0 */
    mov 24(%esp), %ecx      /* arg1 */
    mov 28(%esp), %edx      /* arg2 */
    mov 32(%esp), %esi      /* arg3 */
    mov 36(%esp), %edi      /* arg4 */
    int $0x80
    pop %edi
    pop %esi
    pop %ebx
    ret
.size syscall5, . - syscall5

/* ============================================================================
 * syscall6 - 6个参数系统调用
 * uint32_t syscall6(uint32_t num, uint32_t arg0, uint32_t arg1, uint32_t arg2,
 *                   uint32_t arg3, uint32_t arg4, uint32_t arg5)
 * ========================================================================== */
.global syscall6
.type syscall6, @function
syscall6:
    push %ebx
    push %esi
    push %edi
    push %ebp
    mov 20(%esp), %eax      /* syscall 号 */
    mov 24(%esp), %ebx      /* arg0 */
    mov 28(%esp), %ecx      /* arg1 */
    mov 32(%esp), %edx      /* arg2 */
    mov 36(%esp), %esi      /* arg3 */
    mov 40(%esp), %edi      /* arg4 */
    mov 44(%esp), %ebp      /* arg5 */
    int $0x80
    pop %ebp
    pop %edi
    pop %esi
    pop %ebx
    ret
.size syscall6, . - syscall6
