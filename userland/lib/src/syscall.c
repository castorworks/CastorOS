#include <syscall.h>
#include <types.h>
#include <string.h>

// ============================================================================
// 进程管理系统调用
// ============================================================================

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
    return waitpid(-1, wstatus, 0);
}

// ============================================================================
// 文件系统系统调用
// ============================================================================

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

int ioctl(int fd, unsigned long request, void *argp) {
    return (int)syscall3(SYS_IOCTL, (uint32_t)fd, (uint32_t)request, (uint32_t)argp);
}

int rename(const char *oldpath, const char *newpath) {
    return (int)syscall2(SYS_RENAME, (uint32_t)oldpath, (uint32_t)newpath);
}

// ============================================================================
// 内存管理系统调用
// ============================================================================

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
    if (_brk_current == 0) {
        _brk_current = syscall1(SYS_BRK, 0);
        if (_brk_current == (uint32_t)-1) {
            return (void *)-1;
        }
    }
    
    uint32_t old_brk = _brk_current;
    
    if (increment == 0) {
        return (void *)old_brk;
    }
    
    uint32_t new_brk = old_brk + increment;
    uint32_t result = syscall1(SYS_BRK, new_brk);
    if (result == (uint32_t)-1) {
        return (void *)-1;
    }
    
    _brk_current = result;
    return (void *)old_brk;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
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

// ============================================================================
// 系统信息与杂项
// ============================================================================

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

// ============================================================================
// BSD Socket API 实现（符合 POSIX.1-2008 标准）
// ============================================================================

int socket(int domain, int type, int protocol) {
    return (int)syscall3(SYS_SOCKET, (uint32_t)domain, (uint32_t)type, (uint32_t)protocol);
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    return (int)syscall3(SYS_BIND, (uint32_t)sockfd, (uint32_t)addr, (uint32_t)addrlen);
}

int listen(int sockfd, int backlog) {
    return (int)syscall2(SYS_LISTEN, (uint32_t)sockfd, (uint32_t)backlog);
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    return (int)syscall3(SYS_ACCEPT, (uint32_t)sockfd, (uint32_t)addr, (uint32_t)addrlen);
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    return (int)syscall3(SYS_CONNECT, (uint32_t)sockfd, (uint32_t)addr, (uint32_t)addrlen);
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
    return (ssize_t)syscall4(SYS_SEND, (uint32_t)sockfd, (uint32_t)buf, (uint32_t)len, (uint32_t)flags);
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen) {
    return (ssize_t)syscall6(SYS_SENDTO, (uint32_t)sockfd, (uint32_t)buf, (uint32_t)len, 
                             (uint32_t)flags, (uint32_t)dest_addr, (uint32_t)addrlen);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    return (ssize_t)syscall4(SYS_RECV, (uint32_t)sockfd, (uint32_t)buf, (uint32_t)len, (uint32_t)flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen) {
    return (ssize_t)syscall6(SYS_RECVFROM, (uint32_t)sockfd, (uint32_t)buf, (uint32_t)len, 
                             (uint32_t)flags, (uint32_t)src_addr, (uint32_t)addrlen);
}

int shutdown(int sockfd, int how) {
    return (int)syscall2(SYS_SHUTDOWN, (uint32_t)sockfd, (uint32_t)how);
}

int setsockopt(int sockfd, int level, int optname,
               const void *optval, socklen_t optlen) {
    return (int)syscall5(SYS_SETSOCKOPT, (uint32_t)sockfd, (uint32_t)level, 
                         (uint32_t)optname, (uint32_t)optval, (uint32_t)optlen);
}

int getsockopt(int sockfd, int level, int optname,
               void *optval, socklen_t *optlen) {
    return (int)syscall5(SYS_GETSOCKOPT, (uint32_t)sockfd, (uint32_t)level, 
                         (uint32_t)optname, (uint32_t)optval, (uint32_t)optlen);
}

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    return (int)syscall3(SYS_GETSOCKNAME, (uint32_t)sockfd, (uint32_t)addr, (uint32_t)addrlen);
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    return (int)syscall3(SYS_GETPEERNAME, (uint32_t)sockfd, (uint32_t)addr, (uint32_t)addrlen);
}
