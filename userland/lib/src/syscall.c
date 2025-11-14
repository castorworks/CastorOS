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

int open(const char *path, int flags, uint32_t mode) {
    return (int)syscall3(SYS_OPEN, (uint32_t)path, (uint32_t)flags, mode);
}

int close(int fd) {
    return (int)syscall1(SYS_CLOSE, (uint32_t)fd);
}

int read(int fd, void *buf, uint32_t count) {
    return (int)syscall3(SYS_READ, (uint32_t)fd, (uint32_t)buf, count);
}

int write(int fd, const void *buf, uint32_t count) {
    return (int)syscall3(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, count);
}

int lseek(int fd, int offset, int whence) {
    return (int)syscall3(SYS_LSEEK, (uint32_t)fd, (uint32_t)offset, (uint32_t)whence);
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

