/**
 * @file syscall.c
 * @brief 核心系统调用实现
 * 
 * 仅包含非网络相关的系统调用，网络 API 在 socket.c 中实现
 * 
 * 系统调用底层实现位于架构特定的汇编文件中:
 * - i686:   src/arch/i686/syscall.S
 * - x86_64: src/arch/x86_64/syscall.S
 * - arm64:  src/arch/arm64/syscall.S
 */

#include <types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

/* 简化的指针转换宏 */
#define PTR_TO_ARG(p) ((syscall_arg_t)(uintptr_t)(p))

// ============================================================================
// 进程管理
// ============================================================================

void _exit(int code) {
    (void)syscall1(SYS_EXIT, (syscall_arg_t)code);
    __builtin_unreachable();
}

void exit(int code) {
    _exit(code);
}

int fork(void) {
    return (int)syscall0(SYS_FORK);
}

int exec(const char *path) {
    return (int)syscall1(SYS_EXECVE, PTR_TO_ARG(path));
}

int getpid(void) {
    return (int)syscall0(SYS_GETPID);
}

int getppid(void) {
    return (int)syscall0(SYS_GETPPID);
}

int waitpid(int pid, int *wstatus, int options) {
    return (int)syscall3(SYS_WAITPID, (syscall_arg_t)pid, 
                         PTR_TO_ARG(wstatus), (syscall_arg_t)options);
}

int wait(int *wstatus) {
    return waitpid(-1, wstatus, 0);
}

// ============================================================================
// 文件系统
// ============================================================================

int open(const char *pathname, int flags, ...) {
    syscall_arg_t mode = 0;
    if (flags & O_CREAT) {
        __builtin_va_list ap;
        __builtin_va_start(ap, flags);
        mode = __builtin_va_arg(ap, syscall_arg_t);
        __builtin_va_end(ap);
    }
    return (int)syscall3(SYS_OPEN, PTR_TO_ARG(pathname), 
                         (syscall_arg_t)flags, mode);
}

int creat(const char *pathname, mode_t mode) {
    return open(pathname, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

int close(int fd) {
    return (int)syscall1(SYS_CLOSE, (syscall_arg_t)fd);
}

ssize_t read(int fd, void *buf, size_t count) {
    return (ssize_t)syscall3(SYS_READ, (syscall_arg_t)fd, 
                             PTR_TO_ARG(buf), (syscall_arg_t)count);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return (ssize_t)syscall3(SYS_WRITE, (syscall_arg_t)fd, 
                             PTR_TO_ARG(buf), (syscall_arg_t)count);
}

off_t lseek(int fd, off_t offset, int whence) {
    return (off_t)syscall3(SYS_LSEEK, (syscall_arg_t)fd, 
                           (syscall_arg_t)offset, (syscall_arg_t)whence);
}

int mkdir(const char *path, uint32_t mode) {
    return (int)syscall2(SYS_MKDIR, PTR_TO_ARG(path), (syscall_arg_t)mode);
}

int rmdir(const char *pathname) {
    return (int)syscall1(SYS_RMDIR, PTR_TO_ARG(pathname));
}

int unlink(const char *pathname) {
    return (int)syscall1(SYS_UNLINK, PTR_TO_ARG(pathname));
}

int chdir(const char *path) {
    return (int)syscall1(SYS_CHDIR, PTR_TO_ARG(path));
}

char *getcwd(char *buf, size_t size) {
    int ret = (int)syscall2(SYS_GETCWD, PTR_TO_ARG(buf), (syscall_arg_t)size);
    return (ret == -1) ? NULL : buf;
}

int getdents(int fd, uint32_t index, struct dirent *dirent) {
    return (int)syscall3(SYS_GETDENTS, (syscall_arg_t)fd, 
                         (syscall_arg_t)index, PTR_TO_ARG(dirent));
}

int stat(const char *path, struct stat *buf) {
    return (int)syscall2(SYS_STAT, PTR_TO_ARG(path), PTR_TO_ARG(buf));
}

int fstat(int fd, struct stat *buf) {
    return (int)syscall2(SYS_FSTAT, (syscall_arg_t)fd, PTR_TO_ARG(buf));
}

int ftruncate(int fd, off_t length) {
    return (int)syscall2(SYS_FTRUNCATE, (syscall_arg_t)fd, (syscall_arg_t)length);
}

int pipe(int pipefd[2]) {
    return (int)syscall1(SYS_PIPE, PTR_TO_ARG(pipefd));
}

int dup(int oldfd) {
    return (int)syscall1(SYS_DUP, (syscall_arg_t)oldfd);
}

int dup2(int oldfd, int newfd) {
    return (int)syscall2(SYS_DUP2, (syscall_arg_t)oldfd, (syscall_arg_t)newfd);
}

int ioctl(int fd, unsigned long request, ...) {
    void *argp = NULL;
    __builtin_va_list ap;
    __builtin_va_start(ap, request);
    argp = __builtin_va_arg(ap, void *);
    __builtin_va_end(ap);
    return (int)syscall3(SYS_IOCTL, (syscall_arg_t)fd, 
                         (syscall_arg_t)request, PTR_TO_ARG(argp));
}

int rename(const char *oldpath, const char *newpath) {
    return (int)syscall2(SYS_RENAME, PTR_TO_ARG(oldpath), PTR_TO_ARG(newpath));
}

// ============================================================================
// 内存管理
// ============================================================================

static syscall_arg_t _brk_current = 0;

void *brk(void *addr) {
    syscall_arg_t result = syscall1(SYS_BRK, PTR_TO_ARG(addr));
    if (result == (syscall_arg_t)-1) return (void *)-1;
    _brk_current = result;
    return (void *)(uintptr_t)result;
}

void *sbrk(int increment) {
    if (_brk_current == 0) {
        _brk_current = syscall1(SYS_BRK, 0);
        if (_brk_current == (syscall_arg_t)-1) return (void *)-1;
    }
    
    syscall_arg_t old_brk = _brk_current;
    if (increment == 0) return (void *)(uintptr_t)old_brk;
    
    syscall_arg_t new_brk = old_brk + increment;
    syscall_arg_t result = syscall1(SYS_BRK, new_brk);
    if (result == (syscall_arg_t)-1) return (void *)-1;
    
    _brk_current = result;
    return (void *)(uintptr_t)old_brk;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    syscall_arg_t result = syscall6(SYS_MMAP, PTR_TO_ARG(addr), 
                                    (syscall_arg_t)length, (syscall_arg_t)prot, 
                                    (syscall_arg_t)flags, (syscall_arg_t)fd, 
                                    (syscall_arg_t)offset);
    if (result == (syscall_arg_t)-1) return MAP_FAILED;
    return (void *)(uintptr_t)result;
}

int munmap(void *addr, size_t length) {
    return (int)syscall2(SYS_MUNMAP, PTR_TO_ARG(addr), (syscall_arg_t)length);
}

// ============================================================================
// 系统信息与杂项
// ============================================================================

int uname(struct utsname *buf) {
    return (int)syscall1(SYS_UNAME, PTR_TO_ARG(buf));
}

int reboot(void) {
    return (int)syscall0(SYS_REBOOT);
}

int poweroff(void) {
    return (int)syscall0(SYS_POWEROFF);
}

void print(const char *msg) {
    if (!msg) return;
    size_t len = 0;
    while (msg[len]) len++;
    if (len > 0) write(STDOUT_FILENO, msg, len);
}

int errno = 0;
