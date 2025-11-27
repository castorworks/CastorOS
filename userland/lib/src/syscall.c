#include <syscall.h>
#include <types.h>
#include <string.h>

// 系统调用封装函数（非内联版本）

void exit(int code) {
    (void)syscall1(SYS_EXIT, (uint32_t)code);
    __builtin_unreachable();
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
    // wait() 等价于 waitpid(-1, wstatus, 0)
    return waitpid(-1, wstatus, 0);
}

int open(const char *path, int flags, uint32_t mode) {
    return (int)syscall3(SYS_OPEN, (uint32_t)path, (uint32_t)flags, mode);
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

int chdir(const char *path) {
    return (int)syscall1(SYS_CHDIR, (uint32_t)path);
}

char *getcwd(char *buf, size_t size) {
    int ret = (int)syscall2(SYS_GETCWD, (uint32_t)buf, (uint32_t)size);
    return (ret == -1) ? 0 : buf;
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

int pipe(int fds[2]) {
    return (int)syscall1(SYS_PIPE, (uint32_t)fds);
}

int dup(int oldfd) {
    return (int)syscall1(SYS_DUP, (uint32_t)oldfd);
}

int dup2(int oldfd, int newfd) {
    return (int)syscall2(SYS_DUP2, (uint32_t)oldfd, (uint32_t)newfd);
}

// 静态变量，用于 sbrk 跟踪当前堆位置
static uint32_t _brk_current = 0;

void *brk(void *addr) {
    uint32_t result = syscall1(SYS_BRK, (uint32_t)addr);
    if (result == (uint32_t)-1) {
        return (void *)-1;
    }
    _brk_current = result;
    return (void *)result;
}

void *sbrk(int increment) {
    // 如果还没有初始化，先获取当前堆位置
    if (_brk_current == 0) {
        _brk_current = syscall1(SYS_BRK, 0);
        if (_brk_current == (uint32_t)-1) {
            return (void *)-1;
        }
    }
    
    // 保存旧的堆位置
    uint32_t old_brk = _brk_current;
    
    // 如果 increment 为 0，直接返回当前位置
    if (increment == 0) {
        return (void *)old_brk;
    }
    
    // 计算新的堆位置
    uint32_t new_brk = old_brk + increment;
    
    // 调用 brk 设置新位置
    uint32_t result = syscall1(SYS_BRK, new_brk);
    if (result == (uint32_t)-1) {
        return (void *)-1;
    }
    
    _brk_current = result;
    return (void *)old_brk;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    // 使用 syscall6：6 个参数分别通过 ebx, ecx, edx, esi, edi, ebp 传递
    uint32_t result = syscall6(SYS_MMAP, 
                               (uint32_t)addr, 
                               (uint32_t)length, 
                               (uint32_t)prot, 
                               (uint32_t)flags, 
                               (uint32_t)fd,
                               (uint32_t)offset);
    
    if (result == (uint32_t)-1) {
        return MAP_FAILED;
    }
    return (void *)result;
}

int munmap(void *addr, size_t length) {
    return (int)syscall2(SYS_MUNMAP, (uint32_t)addr, (uint32_t)length);
}

size_t strlen_simple(const char *str) {
    if (!str) {
        return 0;
    }
    const char *s = str;
    while (*s) {
        ++s;
    }
    return (size_t)(s - str);
}

void print(const char *msg) {
    size_t len = strlen_simple(msg);
    if (len == 0) {
        return;
    }
    (void)write(STDOUT_FILENO, msg, (uint32_t)len);
}

int reboot(void) {
    return (int)syscall0(SYS_REBOOT);
}

int poweroff(void) {
    return (int)syscall0(SYS_POWEROFF);
}

int uname(struct utsname *buf) {
    return (int)syscall1(SYS_UNAME, (uint32_t)buf);
}

int rename(const char *oldpath, const char *newpath) {
    return (int)syscall2(SYS_RENAME, (uint32_t)oldpath, (uint32_t)newpath);
}
