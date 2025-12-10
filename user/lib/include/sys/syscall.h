/**
 * @file sys/syscall.h
 * @brief 系统调用号定义和底层接口
 * 
 * 本文件仅供库内部使用，应用程序应使用 unistd.h 等标准头文件
 * 
 * 系统调用实现位于架构特定的汇编文件中:
 * - i686:   src/arch/i686/syscall.S   (INT 0x80)
 * - x86_64: src/arch/x86_64/syscall.S (SYSCALL)
 * - arm64:  src/arch/arm64/syscall.S  (SVC #0)
 */

#ifndef _SYS_SYSCALL_H_
#define _SYS_SYSCALL_H_

/* 基础整数类型 - 直接定义以避免循环依赖 */
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

/* 架构相关的指针类型 */
#if defined(ARCH_X86_64) || defined(__x86_64__) || \
    defined(ARCH_ARM64) || defined(__aarch64__)
typedef uint64_t uintptr_t;
#else
typedef uint32_t uintptr_t;
#endif

#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif

// ============================================================================
// 系统调用号定义（需与内核保持一致）
// ============================================================================

enum {
    // -------------------- 进程与线程 (0x00xx) --------------------
    SYS_EXIT            = 0x0000,
    SYS_FORK            = 0x0001,
    SYS_EXECVE          = 0x0002,
    SYS_WAITPID         = 0x0003,
    SYS_GETPID          = 0x0004,
    SYS_GETPPID         = 0x0005,
    SYS_SCHED_YIELD     = 0x0006,
    SYS_CLONE           = 0x0007,

    // -------------------- 文件与文件系统 (0x01xx) --------------------
    SYS_OPEN            = 0x0100,
    SYS_CLOSE           = 0x0101,
    SYS_READ            = 0x0102,
    SYS_WRITE           = 0x0103,
    SYS_LSEEK           = 0x0104,
    SYS_STAT            = 0x0105,
    SYS_FSTAT           = 0x0106,
    SYS_MKDIR           = 0x0107,
    SYS_RMDIR           = 0x0108,
    SYS_UNLINK          = 0x0109,
    SYS_RENAME          = 0x010A,
    SYS_GETCWD          = 0x010B,
    SYS_CHDIR           = 0x010C,
    SYS_GETDENTS        = 0x010D,
    SYS_FTRUNCATE       = 0x010E,
    SYS_PIPE            = 0x010F,
    SYS_DUP             = 0x0110,
    SYS_DUP2            = 0x0111,
    SYS_IOCTL           = 0x0112,

    // -------------------- 内存管理 (0x02xx) --------------------
    SYS_BRK             = 0x0200,
    SYS_MMAP            = 0x0201,
    SYS_MUNMAP          = 0x0202,
    SYS_MPROTECT        = 0x0203,

    // -------------------- 时间与时钟 (0x03xx) --------------------
    SYS_TIME            = 0x0300,
    SYS_GETTIMEOFDAY    = 0x0301,
    SYS_NANOSLEEP       = 0x0302,
    SYS_CLOCK_GETTIME   = 0x0303,

    // -------------------- 信号与进程控制 (0x04xx) --------------------
    SYS_KILL            = 0x0400,
    SYS_SIGACTION       = 0x0401,
    SYS_SIGPROCMASK     = 0x0402,
    SYS_SIGRETURN       = 0x0403,

    // -------------------- 系统信息 / 杂项 (0x05xx) --------------------
    SYS_UNAME           = 0x0500,
    SYS_GETRANDOM       = 0x0501,
    SYS_DEBUG_PRINT     = 0x0502,
    SYS_REBOOT          = 0x0503,
    SYS_POWEROFF        = 0x0504,

    // -------------------- 网络 BSD Socket API (0x06xx) --------------------
    SYS_SOCKET          = 0x0600,
    SYS_BIND            = 0x0601,
    SYS_LISTEN          = 0x0602,
    SYS_ACCEPT          = 0x0603,
    SYS_CONNECT         = 0x0604,
    SYS_SEND            = 0x0605,
    SYS_SENDTO          = 0x0606,
    SYS_RECV            = 0x0607,
    SYS_RECVFROM        = 0x0608,
    SYS_SHUTDOWN        = 0x0609,
    SYS_SETSOCKOPT      = 0x060A,
    SYS_GETSOCKOPT      = 0x060B,
    SYS_GETSOCKNAME     = 0x060C,
    SYS_GETPEERNAME     = 0x060D,
    SYS_SELECT          = 0x060E,
    SYS_FCNTL           = 0x060F,

    SYS_MAX
};

// ============================================================================
// 架构无关的系统调用参数类型
// ============================================================================

#if defined(ARCH_X86_64) || defined(__x86_64__) || \
    defined(ARCH_ARM64) || defined(__aarch64__)
typedef uint64_t syscall_arg_t;
#else
/* 默认使用 32 位类型（向后兼容） */
typedef uint32_t syscall_arg_t;
#endif

// ============================================================================
// 系统调用接口（架构特定汇编实现）
// ============================================================================

/**
 * @brief 无参数系统调用
 * @param num 系统调用号
 * @return 系统调用返回值
 */
syscall_arg_t syscall0(syscall_arg_t num);

/**
 * @brief 1个参数系统调用
 */
syscall_arg_t syscall1(syscall_arg_t num, syscall_arg_t arg0);

/**
 * @brief 2个参数系统调用
 */
syscall_arg_t syscall2(syscall_arg_t num, syscall_arg_t arg0, syscall_arg_t arg1);

/**
 * @brief 3个参数系统调用
 */
syscall_arg_t syscall3(syscall_arg_t num, syscall_arg_t arg0, syscall_arg_t arg1,
                       syscall_arg_t arg2);

/**
 * @brief 4个参数系统调用
 */
syscall_arg_t syscall4(syscall_arg_t num, syscall_arg_t arg0, syscall_arg_t arg1,
                       syscall_arg_t arg2, syscall_arg_t arg3);

/**
 * @brief 5个参数系统调用
 */
syscall_arg_t syscall5(syscall_arg_t num, syscall_arg_t arg0, syscall_arg_t arg1,
                       syscall_arg_t arg2, syscall_arg_t arg3, syscall_arg_t arg4);

/**
 * @brief 6个参数系统调用
 */
syscall_arg_t syscall6(syscall_arg_t num, syscall_arg_t arg0, syscall_arg_t arg1,
                       syscall_arg_t arg2, syscall_arg_t arg3, syscall_arg_t arg4,
                       syscall_arg_t arg5);

#endif // _SYS_SYSCALL_H_
