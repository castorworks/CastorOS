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
    SYS_FTRUNCATE       = 0x010E,  // 截断文件到指定大小
    SYS_PIPE            = 0x010F,  // 创建管道
    SYS_DUP             = 0x0110,  // 复制文件描述符
    SYS_DUP2            = 0x0111,  // 复制文件描述符到指定编号

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

// 5 参数系统调用（使用 edi 寄存器）
static inline __maybe_unused uint32_t __syscall5(uint32_t num, uint32_t arg0,
                                                 uint32_t arg1,
                                                 uint32_t arg2,
                                                 uint32_t arg3,
                                                 uint32_t arg4) {
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg0), "c"(arg1), "d"(arg2), "S"(arg3), "D"(arg4)
        : "memory", "cc"
    );
    return ret;
}

static inline __maybe_unused uint32_t syscall5(uint32_t num, uint32_t arg0,
                                               uint32_t arg1,
                                               uint32_t arg2,
                                               uint32_t arg3,
                                               uint32_t arg4) {
    return __syscall5(num, arg0, arg1, arg2, arg3, arg4);
}

// 6 参数系统调用（使用 ebp 寄存器作为第 6 个参数）
// 注意：ebp 会被内核从中断帧中读取
static inline __maybe_unused uint32_t __syscall6(uint32_t num, uint32_t arg0,
                                                 uint32_t arg1, uint32_t arg2,
                                                 uint32_t arg3, uint32_t arg4,
                                                 uint32_t arg5) {
    uint32_t ret;
    __asm__ volatile (
        "push %%ebp\n\t"        // 保存原 ebp
        "mov %7, %%ebp\n\t"     // 将第 6 个参数放入 ebp
        "int $0x80\n\t"
        "pop %%ebp"             // 恢复原 ebp
        : "=a"(ret)
        : "a"(num), "b"(arg0), "c"(arg1), "d"(arg2), "S"(arg3), "D"(arg4), "g"(arg5)
        : "memory", "cc"
    );
    return ret;
}

static inline __maybe_unused uint32_t syscall6(uint32_t num, uint32_t arg0,
                                               uint32_t arg1, uint32_t arg2,
                                               uint32_t arg3, uint32_t arg4,
                                               uint32_t arg5) {
    return __syscall6(num, arg0, arg1, arg2, arg3, arg4, arg5);
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
int getpid(void);
int getppid(void);
int waitpid(int pid, int *wstatus, int options);
int wait(int *wstatus);
int open(const char *path, int flags, uint32_t mode);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);        // POSIX: ssize_t read(int, void*, size_t)
ssize_t write(int fd, const void *buf, size_t count); // POSIX: ssize_t write(int, const void*, size_t)
off_t lseek(int fd, off_t offset, int whence);        // POSIX: off_t lseek(int, off_t, int)
int mkdir(const char *path, uint32_t mode);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
int getdents(int fd, uint32_t index, struct dirent *dirent);
int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int ftruncate(int fd, off_t length);                  // POSIX: int ftruncate(int, off_t)

/**
 * pipe - 创建管道
 * @param fds 用户空间数组，fds[0] 为读端，fds[1] 为写端
 * @return 0 成功，-1 失败
 */
int pipe(int fds[2]);

/**
 * dup - 复制文件描述符
 * @param oldfd 要复制的文件描述符
 * @return 新的文件描述符，失败返回 -1
 */
int dup(int oldfd);

/**
 * dup2 - 复制文件描述符到指定编号
 * @param oldfd 要复制的文件描述符
 * @param newfd 目标文件描述符编号
 * @return newfd，失败返回 -1
 */
int dup2(int oldfd, int newfd);

/**
 * brk - 调整堆边界
 * @param addr 新的堆结束地址（0 表示查询当前值）
 * @return 成功返回新的堆结束地址，失败返回 (void *)-1
 */
void *brk(void *addr);

/**
 * sbrk - 增加/减少堆大小
 * @param increment 要增加的字节数（可为负数）
 * @return 成功返回之前的堆结束地址，失败返回 (void *)-1
 */
void *sbrk(int increment);

/**
 * mmap - 内存映射（简化版：仅支持匿名映射）
 * @param addr 建议的映射地址（NULL 表示由内核选择）
 * @param length 映射长度（字节）
 * @param prot 保护标志（PROT_READ, PROT_WRITE, PROT_EXEC）
 * @param flags 映射标志（必须包含 MAP_ANONYMOUS）
 * @param fd 文件描述符（匿名映射时应传 -1）
 * @param offset 文件偏移（匿名映射时应传 0）
 * @return 成功返回映射的虚拟地址，失败返回 MAP_FAILED
 * 
 * 使用示例：
 *   void *p = mmap(NULL, 4096, PROT_READ|PROT_WRITE, 
 *                  MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
 *   if (p == MAP_FAILED) { // 处理错误 }
 */
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);

/**
 * munmap - 取消内存映射
 * @param addr 映射起始地址
 * @param length 取消映射的长度
 * @return 成功返回 0，失败返回 -1
 */
int munmap(void *addr, size_t length);

size_t strlen_simple(const char *str);
void print(const char *msg);
int reboot(void);
int poweroff(void);

#endif /* _USERLAND_LIB_SYSCALL_H_ */

