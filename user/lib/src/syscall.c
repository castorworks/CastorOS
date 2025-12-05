/**
 * @file syscall.c
 * @brief 核心系统调用实现
 * 
 * 仅包含非网络相关的系统调用，网络 API 在 socket.c 中实现
 */

#include <types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

// ============================================================================
// 进程管理
// ============================================================================

void _exit(int code) {
    (void)syscall1(SYS_EXIT, (uint32_t)code);
    __builtin_unreachable();
}

// exit 是 _exit 的别名
void exit(int code) {
    _exit(code);
}

int fork(void) {
    return (int)syscall0(SYS_FORK);
}

int exec(const char *path) {
    return (int)syscall1(SYS_EXECVE, (uint32_t)path);
}

int getpid(void) {
    return (int)syscall0(SYS_GETPID);
}

int getppid(void) {
    return (int)syscall0(SYS_GETPPID);
}

int waitpid(int pid, int *wstatus, int options) {
    return (int)syscall3(SYS_WAITPID, (uint32_t)pid, (uint32_t)wstatus, (uint32_t)options);
}

int wait(int *wstatus) {
    return waitpid(-1, wstatus, 0);
}

// ============================================================================
// 文件系统
// ============================================================================

int open(const char *pathname, int flags, ...) {
    uint32_t mode = 0;
    if (flags & O_CREAT) {
        __builtin_va_list ap;
        __builtin_va_start(ap, flags);
        mode = __builtin_va_arg(ap, uint32_t);
        __builtin_va_end(ap);
    }
    return (int)syscall3(SYS_OPEN, (uint32_t)pathname, (uint32_t)flags, mode);
}

int creat(const char *pathname, mode_t mode) {
    return open(pathname, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

int close(int fd) {
    return (int)syscall1(SYS_CLOSE, (uint32_t)fd);
}

ssize_t read(int fd, void *buf, size_t count) {
    return (ssize_t)syscall3(SYS_READ, (uint32_t)fd, (uint32_t)buf, (uint32_t)count);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return (ssize_t)syscall3(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, (uint32_t)count);
}

off_t lseek(int fd, off_t offset, int whence) {
    return (off_t)syscall3(SYS_LSEEK, (uint32_t)fd, (uint32_t)offset, (uint32_t)whence);
}

int mkdir(const char *path, uint32_t mode) {
    return (int)syscall2(SYS_MKDIR, (uint32_t)path, mode);
}

int rmdir(const char *pathname) {
    return (int)syscall1(SYS_RMDIR, (uint32_t)pathname);
}

int unlink(const char *pathname) {
    return (int)syscall1(SYS_UNLINK, (uint32_t)pathname);
}

int chdir(const char *path) {
    return (int)syscall1(SYS_CHDIR, (uint32_t)path);
}

char *getcwd(char *buf, size_t size) {
    int ret = (int)syscall2(SYS_GETCWD, (uint32_t)buf, (uint32_t)size);
    return (ret == -1) ? NULL : buf;
}

int getdents(int fd, uint32_t index, struct dirent *dirent) {
    return (int)syscall3(SYS_GETDENTS, (uint32_t)fd, index, (uint32_t)dirent);
}

int stat(const char *path, struct stat *buf) {
    return (int)syscall2(SYS_STAT, (uint32_t)path, (uint32_t)buf);
}

int fstat(int fd, struct stat *buf) {
    return (int)syscall2(SYS_FSTAT, (uint32_t)fd, (uint32_t)buf);
}

int ftruncate(int fd, off_t length) {
    return (int)syscall2(SYS_FTRUNCATE, (uint32_t)fd, (uint32_t)length);
}

int pipe(int pipefd[2]) {
    return (int)syscall1(SYS_PIPE, (uint32_t)pipefd);
}

int dup(int oldfd) {
    return (int)syscall1(SYS_DUP, (uint32_t)oldfd);
}

int dup2(int oldfd, int newfd) {
    return (int)syscall2(SYS_DUP2, (uint32_t)oldfd, (uint32_t)newfd);
}

int ioctl(int fd, unsigned long request, ...) {
    void *argp = NULL;
    __builtin_va_list ap;
    __builtin_va_start(ap, request);
    argp = __builtin_va_arg(ap, void *);
    __builtin_va_end(ap);
    return (int)syscall3(SYS_IOCTL, (uint32_t)fd, (uint32_t)request, (uint32_t)argp);
}

int rename(const char *oldpath, const char *newpath) {
    return (int)syscall2(SYS_RENAME, (uint32_t)oldpath, (uint32_t)newpath);
}

// ============================================================================
// 内存管理
// ============================================================================

static uint32_t _brk_current = 0;

void *brk(void *addr) {
    uint32_t result = syscall1(SYS_BRK, (uint32_t)addr);
    if (result == (uint32_t)-1) return (void *)-1;
    _brk_current = result;
    return (void *)result;
}

void *sbrk(int increment) {
    if (_brk_current == 0) {
        _brk_current = syscall1(SYS_BRK, 0);
        if (_brk_current == (uint32_t)-1) return (void *)-1;
    }
    
    uint32_t old_brk = _brk_current;
    if (increment == 0) return (void *)old_brk;
    
    uint32_t new_brk = old_brk + increment;
    uint32_t result = syscall1(SYS_BRK, new_brk);
    if (result == (uint32_t)-1) return (void *)-1;
    
    _brk_current = result;
    return (void *)old_brk;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    uint32_t result = syscall6(SYS_MMAP, (uint32_t)addr, (uint32_t)length, 
                               (uint32_t)prot, (uint32_t)flags, 
                               (uint32_t)fd, (uint32_t)offset);
    if (result == (uint32_t)-1) return MAP_FAILED;
    return (void *)result;
}

int munmap(void *addr, size_t length) {
    return (int)syscall2(SYS_MUNMAP, (uint32_t)addr, (uint32_t)length);
}

// ============================================================================
// 系统信息与杂项
// ============================================================================

int uname(struct utsname *buf) {
    return (int)syscall1(SYS_UNAME, (uint32_t)buf);
}

int reboot(void) {
    return (int)syscall0(SYS_REBOOT);
}

int poweroff(void) {
    return (int)syscall0(SYS_POWEROFF);
}

// 简易打印（用于调试）
void print(const char *msg) {
    if (!msg) return;
    size_t len = 0;
    while (msg[len]) len++;
    if (len > 0) write(STDOUT_FILENO, msg, len);
}

// errno 支持（简化版）
int errno = 0;
