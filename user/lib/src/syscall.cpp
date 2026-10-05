/**
 * 系统调用的 C 包装。陷入指令本身在 src/arch/<arch>/syscall.S。
 */

#include <syscall.h>
#include <string.h>

#define PTR_TO_ARG(p) ((syscall_arg_t)(uintptr_t)(p))

// ============================================================================
// 进程
// ============================================================================

void exit(int code) {
    syscall1(SYS_EXIT, (syscall_arg_t)code);
    for (;;) { }
}

int fork(void) {
    return (int)syscall0(SYS_FORK);
}

int exec(const void *image, size_t size, const char *const argv[]) {
    // 打包成内核要的参数块："arg0\0arg1\0...argN\0"
    static char block[4092];
    size_t len = 0;
    for (int i = 0; argv && argv[i]; i++) {
        size_t n = strlen(argv[i]) + 1;
        if (len + n > sizeof(block)) {
            return -1;
        }
        memcpy(block + len, argv[i], n);
        len += n;
    }
    return (int)syscall4(SYS_EXEC, PTR_TO_ARG(image), (syscall_arg_t)size,
                         PTR_TO_ARG(block), (syscall_arg_t)len);
}

int waitpid(int pid, int *wstatus, int options) {
    return (int)syscall3(SYS_WAITPID, (syscall_arg_t)pid, PTR_TO_ARG(wstatus),
                         (syscall_arg_t)options);
}

int wait(int *wstatus) {
    return waitpid(-1, wstatus, 0);
}

int getpid(void) {
    return (int)syscall0(SYS_GETPID);
}

int getppid(void) {
    return (int)syscall0(SYS_GETPPID);
}

void yield(void) {
    syscall0(SYS_SCHED_YIELD);
}

int kill(int pid, int signal) {
    return (int)syscall2(SYS_KILL, (syscall_arg_t)pid, (syscall_arg_t)signal);
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    return (int)syscall2(SYS_NANOSLEEP, PTR_TO_ARG(req), PTR_TO_ARG(rem));
}

unsigned int sleep(unsigned int seconds) {
    struct timespec req = { .tv_sec = seconds, .tv_nsec = 0 };
    return nanosleep(&req, NULL) == 0 ? 0 : seconds;
}

int usleep(unsigned int usec) {
    struct timespec req = {
        .tv_sec = usec / 1000000u,
        .tv_nsec = (usec % 1000000u) * 1000u,
    };
    return nanosleep(&req, NULL);
}

// ============================================================================
// 内存
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

    syscall_arg_t result = syscall1(SYS_BRK, old_brk + increment);
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

int mem_grant(int pid, void *addr, size_t length) {
    return (int)syscall3(SYS_MEM_GRANT, (syscall_arg_t)pid, PTR_TO_ARG(addr), (syscall_arg_t)length);
}

// ============================================================================
// 调试输出
// ============================================================================

ssize_t console_write(const void *buf, size_t count) {
    return (ssize_t)syscall2(SYS_CONSOLE_WRITE, PTR_TO_ARG(buf), (syscall_arg_t)count);
}

// ============================================================================
// 进程间通信
// ============================================================================

int ipc_send(int dest, const struct ipc_msg *msg) {
    return (int)syscall2(SYS_IPC_SEND, (syscall_arg_t)dest, PTR_TO_ARG(msg));
}

int ipc_recv(int from, struct ipc_msg *msg) {
    return (int)syscall2(SYS_IPC_RECV, (syscall_arg_t)from, PTR_TO_ARG(msg));
}

int ipc_call(int dest, struct ipc_msg *msg) {
    return (int)syscall2(SYS_IPC_CALL, (syscall_arg_t)dest, PTR_TO_ARG(msg));
}

int ipc_reply(int dest, const struct ipc_msg *msg) {
    return (int)syscall2(SYS_IPC_REPLY, (syscall_arg_t)dest, PTR_TO_ARG(msg));
}

// ============================================================================
// 硬件访问
// ============================================================================

int io_read(uintptr_t port, int width, uint32_t *value) {
    return (int)syscall3(SYS_IO_READ, (syscall_arg_t)port, (syscall_arg_t)width, PTR_TO_ARG(value));
}

int io_write(uintptr_t port, int width, uint32_t value) {
    return (int)syscall3(SYS_IO_WRITE, (syscall_arg_t)port, (syscall_arg_t)width, (syscall_arg_t)value);
}

void *map_device(uintptr_t phys, size_t length) {
    return (void *)(uintptr_t)syscall2(SYS_MAP_DEVICE, (syscall_arg_t)phys, (syscall_arg_t)length);
}

void *dma_alloc(size_t length, uint64_t *phys) {
    return (void *)(uintptr_t)syscall2(SYS_DMA_ALLOC, (syscall_arg_t)length, PTR_TO_ARG(phys));
}

int irq_claim(int irq) {
    return (int)syscall1(SYS_IRQ_CLAIM, (syscall_arg_t)irq);
}

int irq_ack(int irq) {
    return (int)syscall1(SYS_IRQ_ACK, (syscall_arg_t)irq);
}

void drop_privilege(void) {
    syscall0(SYS_DROP_PRIVILEGE);
}
