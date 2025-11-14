#ifndef _USERLAND_LIB_SYSCALL_H_
#define _USERLAND_LIB_SYSCALL_H_

#include <types.h>

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
    SYS_GETDENTS        = 0x010D,  // 读取目录项（简化版本，与 Linux getdents 接口不同）

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

    SYS_MAX
};

// ============================================================================
// 通用系统调用入口（必须内联，因为包含内联汇编）
// ============================================================================

static inline __maybe_unused uint32_t __syscall4(uint32_t num, uint32_t arg0,
                                                 uint32_t arg1,
                                                 uint32_t arg2,
                                                 uint32_t arg3) {
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg0), "c"(arg1), "d"(arg2), "S"(arg3)
        : "memory", "cc"
    );
    return ret;
}

static inline __maybe_unused uint32_t syscall0(uint32_t num) {
    return __syscall4(num, 0, 0, 0, 0);
}

static inline __maybe_unused uint32_t syscall1(uint32_t num, uint32_t arg0) {
    return __syscall4(num, arg0, 0, 0, 0);
}

static inline __maybe_unused uint32_t syscall2(uint32_t num, uint32_t arg0,
                                               uint32_t arg1) {
    return __syscall4(num, arg0, arg1, 0, 0);
}

static inline __maybe_unused uint32_t syscall3(uint32_t num, uint32_t arg0,
                                               uint32_t arg1,
                                               uint32_t arg2) {
    return __syscall4(num, arg0, arg1, arg2, 0);
}

static inline __maybe_unused uint32_t syscall4(uint32_t num, uint32_t arg0,
                                               uint32_t arg1,
                                               uint32_t arg2,
                                               uint32_t arg3) {
    return __syscall4(num, arg0, arg1, arg2, arg3);
}

// ============================================================================
// 用户态封装函数声明
// ============================================================================

#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

void exit(int code);
int fork(void);
int exec(const char *path);
int open(const char *path, int flags, uint32_t mode);
int close(int fd);
int read(int fd, void *buf, uint32_t count);
int write(int fd, const void *buf, uint32_t count);
int lseek(int fd, int offset, int whence);
int mkdir(const char *path, uint32_t mode);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
int getdents(int fd, uint32_t index, struct dirent *dirent);

size_t strlen_simple(const char *str);
void print(const char *msg);
int reboot(void);
int poweroff(void);

#endif /* _USERLAND_LIB_SYSCALL_H_ */

