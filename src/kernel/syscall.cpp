// ============================================================================
// syscall.c - System Call Dispatcher (Architecture-Independent)
// ============================================================================
//
// This file implements the architecture-independent system call dispatcher.
// The actual system call entry mechanism is implemented in architecture-specific
// code under src/arch/{arch}/syscall/.
//
// **Feature: multi-arch-support**
// **Validates: Requirements 8.1**
// ============================================================================

#include <kernel/syscall.h>
#include <hal/hal_syscall.h>
#include <kernel/syscalls/process.h>
#include <kernel/syscalls/mm.h>
#include <kernel/uaccess.h>
#include <kernel/ipc.h>
#include <kernel/user_irq.h>
#include <kernel/task.h>
#include <hal/hal.h>
#include <lib/klog.h>
#include <lib/kprintf.h>

/* 系统调用处理函数表 - 使用 syscall_arg_t 支持 32/64 位架构 */
typedef syscall_arg_t (*syscall_handler_t)(syscall_arg_t*, syscall_arg_t, syscall_arg_t, 
                                           syscall_arg_t, syscall_arg_t, syscall_arg_t);

static syscall_handler_t syscall_table[SYS_MAX];

/* 栈帧布局（架构相关）：
 * i686 (syscall_handler 中 "mov ebp, esp" 后)：
 *   frame[0]  = DS
 *   frame[1]  = EAX (syscall_num)
 *   frame[2]  = EBX (arg1)
 *   ...
 *   frame[12] = SS (IRET)
 * 
 * x86_64 (syscall_entry 中保存的寄存器)：
 *   frame[0]  = r15
 *   frame[1]  = r14
 *   ...
 *   frame[15] = user_rsp
 */

/**
 * @brief 取第 6 个系统调用参数
 *
 * syscall_dispatcher 只通过寄存器传递前 5 个参数，第 6 个要从保存的寄存器帧里取，
 * 它所在的寄存器由各架构用户库的 syscall6 约定决定：
 *   - i686:   EBP -> frame[7]
 *   - x86_64: R9  -> frame[6]（frame[7] 是 R8，即第 5 个参数）
 *   - arm64:  X5  -> frame[5]
 */
static inline syscall_arg_t syscall_arg6(const syscall_arg_t *frame) {
#if defined(ARCH_X86_64)
    return frame[6];
#elif defined(ARCH_ARM64)
    return frame[5];
#else
    return frame[7];
#endif
}

/* ============================================================================
 * 用户指针校验辅助
 * 包装器拿到的地址/长度全部来自用户态，传给实现函数之前先在这里校验，
 * 不合法一律返回 -1。
 * ============================================================================ */

#define SYSCALL_FAIL        ((syscall_arg_t)-1)
/* 单次控制台写的长度上限 */
#define CONSOLE_IO_MAX      ((syscall_arg_t)4096)

/**
 * 实现函数用 32 位值表示结果，出错时是负数（如 (uint32_t)-1、(uint32_t)-12）。
 * syscall_arg_t 在 64 位架构上是 64 位，直接返回会被零扩展成一个很大的正数，
 * 用户态的 `if (n < 0)` 就失效了，所以统一在这里做符号扩展。
 */
static inline syscall_arg_t sys_ret32(uint32_t value) {
    return (syscall_arg_t)(intptr_t)(int32_t)value;
}

static inline bool user_rd(syscall_arg_t p, size_t len) {
    return kernel::UAccess::can_read((const void *)(uintptr_t)p, len);
}

static inline bool user_wr(syscall_arg_t p, size_t len) {
    return kernel::UAccess::can_write((void *)(uintptr_t)p, len);
}

/* 可为空的输出指针 */
static inline bool user_wr_opt(syscall_arg_t p, size_t len) {
    return p == 0 || user_wr(p, len);
}

static syscall_arg_t sys_exit_wrapper(syscall_arg_t *frame, syscall_arg_t exit_code, 
                                      syscall_arg_t p2, syscall_arg_t p3, 
                                      syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame;
    (void)p2; (void)p3; (void)p4; (void)p5;
    syscall::Process::exit((uint32_t)exit_code);
    return 0;  // 永远不会返回
}

static syscall_arg_t sys_fork_wrapper(syscall_arg_t *frame, syscall_arg_t p1, syscall_arg_t p2, 
                                      syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    
    return sys_ret32(syscall::Process::fork(frame));
}

/**
 * exec(image, size)：ELF 映像由调用者放在自己的地址空间里，内核不认识文件
 */
static syscall_arg_t sys_exec_wrapper(syscall_arg_t *frame, syscall_arg_t image, syscall_arg_t size,
                                      syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)p3; (void)p4; (void)p5;
    if (!user_rd(image, (size_t)size)) return SYSCALL_FAIL;
    return sys_ret32(syscall::Process::exec(frame, (const void *)(uintptr_t)image, (size_t)size));
}

static syscall_arg_t sys_waitpid_wrapper(syscall_arg_t *frame, syscall_arg_t pid, syscall_arg_t wstatus_ptr,
                                         syscall_arg_t options, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p4; (void)p5;
    if (!(user_wr_opt(wstatus_ptr, sizeof(uint32_t)))) return SYSCALL_FAIL;
    return sys_ret32(syscall::Process::waitpid((int32_t)pid, (uint32_t *)(uintptr_t)wstatus_ptr, (uint32_t)options));
}

static syscall_arg_t sys_getpid_wrapper(syscall_arg_t *frame, syscall_arg_t p1, syscall_arg_t p2, 
                                        syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_ret32(syscall::Process::getpid());
}

static syscall_arg_t sys_getppid_wrapper(syscall_arg_t *frame, syscall_arg_t p1, syscall_arg_t p2, 
                                         syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_ret32(syscall::Process::getppid());
}

static syscall_arg_t sys_yield_wrapper(syscall_arg_t *frame, syscall_arg_t p1, syscall_arg_t p2, 
                                       syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_ret32(syscall::Process::yield());
}

static syscall_arg_t sys_kill_wrapper(syscall_arg_t *frame, syscall_arg_t pid, syscall_arg_t signal,
                                      syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_ret32(syscall::Process::kill((uint32_t)pid, (uint32_t)signal));
}

static syscall_arg_t sys_nanosleep_wrapper(syscall_arg_t *frame, syscall_arg_t req_ptr, 
                                           syscall_arg_t rem_ptr, syscall_arg_t p3,
                                           syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    const struct timespec *req = (const struct timespec *)(uintptr_t)req_ptr;
    struct timespec *rem = (struct timespec *)(uintptr_t)rem_ptr;
    if (!(user_rd(req_ptr, sizeof(struct timespec)) && user_wr_opt(rem_ptr, sizeof(struct timespec)))) return SYSCALL_FAIL;
    return sys_ret32(syscall::Process::nanosleep(req, rem));
}

static syscall_arg_t sys_brk_wrapper(syscall_arg_t *frame, syscall_arg_t addr, syscall_arg_t p2,
                                     syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p2; (void)p3; (void)p4; (void)p5;
    return syscall::Mm::brk((uintptr_t)addr);
}

static syscall_arg_t sys_mmap_wrapper(syscall_arg_t *frame, syscall_arg_t addr, syscall_arg_t length,
                                      syscall_arg_t prot, syscall_arg_t flags, syscall_arg_t fd) {
    // 第 6 个参数 (offset) 不在寄存器参数里，从保存的寄存器帧中取
    syscall_arg_t offset = syscall_arg6(frame);
    return syscall::Mm::mmap((uintptr_t)addr, (size_t)length, (uint32_t)prot, (uint32_t)flags, 
                    (int32_t)fd, (uint32_t)offset);
}

static syscall_arg_t sys_munmap_wrapper(syscall_arg_t *frame, syscall_arg_t addr, syscall_arg_t length,
                                        syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return syscall::Mm::munmap((uintptr_t)addr, (size_t)length);
}

static syscall_arg_t sys_console_write_wrapper(syscall_arg_t *frame, syscall_arg_t buf, syscall_arg_t len,
                                               syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    if (len > CONSOLE_IO_MAX) len = CONSOLE_IO_MAX;
    if (!user_rd(buf, (size_t)len)) return SYSCALL_FAIL;
    const char *p = (const char *)(uintptr_t)buf;
    for (size_t i = 0; i < (size_t)len; i++) {
        kputchar(p[i]);
    }
    return len;
}

static syscall_arg_t sys_ipc_send_wrapper(syscall_arg_t *frame, syscall_arg_t dest, syscall_arg_t msg,
                                          syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    if (!user_rd(msg, sizeof(ipc_msg))) return SYSCALL_FAIL;
    return sys_ret32((uint32_t)kernel::Ipc::send((uint32_t)dest, (const ipc_msg *)(uintptr_t)msg));
}

static syscall_arg_t sys_ipc_recv_wrapper(syscall_arg_t *frame, syscall_arg_t from, syscall_arg_t msg,
                                          syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    if (!user_wr(msg, sizeof(ipc_msg))) return SYSCALL_FAIL;
    return sys_ret32((uint32_t)kernel::Ipc::recv((uint32_t)from, (ipc_msg *)(uintptr_t)msg));
}

static syscall_arg_t sys_ipc_reply_wrapper(syscall_arg_t *frame, syscall_arg_t dest, syscall_arg_t msg,
                                           syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    if (!user_rd(msg, sizeof(ipc_msg))) return SYSCALL_FAIL;
    return sys_ret32((uint32_t)kernel::Ipc::reply((uint32_t)dest, (const ipc_msg *)(uintptr_t)msg));
}

static syscall_arg_t sys_ipc_call_wrapper(syscall_arg_t *frame, syscall_arg_t dest, syscall_arg_t msg,
                                          syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    if (!user_wr(msg, sizeof(ipc_msg))) return SYSCALL_FAIL;
    return sys_ret32((uint32_t)kernel::Ipc::call((uint32_t)dest, (ipc_msg *)(uintptr_t)msg));
}

/* ============================================================================
 * 硬件访问：只对特权进程开放
 * ============================================================================ */

/** port/width 是否是一次合法的端口访问（只有 x86 有 I/O 端口；设备内存用 map_device） */
static bool io_access_ok(syscall_arg_t port, syscall_arg_t width) {
#if defined(ARCH_I686) || defined(ARCH_X86_64)
    if (!kernel::Scheduler::current_is_privileged()) return false;
    if (width != 1 && width != 2 && width != 4) return false;
    return port <= 0xFFFF && port + width <= 0x10000;
#else
    (void)port; (void)width;
    return false;
#endif
}

static syscall_arg_t sys_io_read_wrapper(syscall_arg_t *frame, syscall_arg_t port, syscall_arg_t width,
                                         syscall_arg_t value_ptr, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p4; (void)p5;
    if (!io_access_ok(port, width) || !user_wr(value_ptr, sizeof(uint32_t))) return SYSCALL_FAIL;
#if defined(ARCH_I686) || defined(ARCH_X86_64)
    uint16_t p = (uint16_t)port;
    *(uint32_t *)(uintptr_t)value_ptr =
        width == 1 ? hal::Port::read8(p) : width == 2 ? hal::Port::read16(p) : hal::Port::read32(p);
#endif
    return 0;
}

static syscall_arg_t sys_io_write_wrapper(syscall_arg_t *frame, syscall_arg_t port, syscall_arg_t width,
                                          syscall_arg_t value, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)value; (void)p4; (void)p5;
    if (!io_access_ok(port, width)) return SYSCALL_FAIL;
#if defined(ARCH_I686) || defined(ARCH_X86_64)
    uint16_t p = (uint16_t)port;
    if (width == 1) hal::Port::write8(p, (uint8_t)value);
    else if (width == 2) hal::Port::write16(p, (uint16_t)value);
    else hal::Port::write32(p, (uint32_t)value);
#endif
    return 0;
}

static syscall_arg_t sys_map_device_wrapper(syscall_arg_t *frame, syscall_arg_t phys, syscall_arg_t length,
                                            syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    if (!kernel::Scheduler::current_is_privileged()) return SYSCALL_FAIL;
    return syscall::Mm::map_device((uint64_t)phys, (size_t)length);
}

static syscall_arg_t sys_mem_grant_wrapper(syscall_arg_t *frame, syscall_arg_t pid, syscall_arg_t addr,
                                           syscall_arg_t length, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p4; (void)p5;
    return syscall::Mm::grant((uint32_t)pid, (uintptr_t)addr, (size_t)length);
}

static syscall_arg_t sys_irq_claim_wrapper(syscall_arg_t *frame, syscall_arg_t irq, syscall_arg_t p2,
                                           syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p2; (void)p3; (void)p4; (void)p5;
    if (!kernel::Scheduler::current_is_privileged()) return SYSCALL_FAIL;
    return sys_ret32((uint32_t)kernel::UserIrq::claim((uint32_t)irq));
}

static syscall_arg_t sys_irq_ack_wrapper(syscall_arg_t *frame, syscall_arg_t irq, syscall_arg_t p2,
                                         syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_ret32((uint32_t)kernel::UserIrq::ack((uint32_t)irq));
}

static syscall_arg_t sys_drop_privilege_wrapper(syscall_arg_t *frame, syscall_arg_t p1, syscall_arg_t p2,
                                                syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    task_t *current = kernel::Scheduler::get_current();
    if (current) {
        current->privileged = false;
    }
    return 0;
}

syscall_arg_t syscall_dispatcher(syscall_arg_t syscall_num, syscall_arg_t p1, syscall_arg_t p2, 
                                 syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5, 
                                 syscall_arg_t *frame) {
    
    /* 检查系统调用号是否在有效范围内 */
    if (syscall_num >= SYS_MAX) {
        LOG_WARN_MSG("Invalid syscall number: %lu (out of range)\n", (unsigned long)syscall_num);
        return (syscall_arg_t)-1;
    }
    
    syscall_handler_t handler = syscall_table[syscall_num];
    if (handler == NULL) {
        LOG_WARN_MSG("Unimplemented syscall: %lu\n", (unsigned long)syscall_num);
        return (syscall_arg_t)-1;
    }
    
    syscall_arg_t ret = handler(frame, p1, p2, p3, p4, p5);

    /* 返回用户态之前处理别的任务发来的 kill：此时本任务不持有任何内核锁，
     * 可以安全地自行退出（有待处理的 kill 时不返回） */
    kernel::Scheduler::deliver_pending_kill();

    return ret;
}

void syscall_init(void) {
    for (uint32_t i = 0; i < SYS_MAX; i++) {
        syscall_table[i] = NULL;
    }

    syscall_table[SYS_EXIT]          = sys_exit_wrapper;
    syscall_table[SYS_FORK]          = sys_fork_wrapper;
    syscall_table[SYS_EXEC]          = sys_exec_wrapper;
    syscall_table[SYS_WAITPID]       = sys_waitpid_wrapper;
    syscall_table[SYS_GETPID]        = sys_getpid_wrapper;
    syscall_table[SYS_GETPPID]       = sys_getppid_wrapper;
    syscall_table[SYS_SCHED_YIELD]   = sys_yield_wrapper;
    syscall_table[SYS_KILL]          = sys_kill_wrapper;
    syscall_table[SYS_NANOSLEEP]     = sys_nanosleep_wrapper;
    syscall_table[SYS_BRK]           = sys_brk_wrapper;
    syscall_table[SYS_MMAP]          = sys_mmap_wrapper;
    syscall_table[SYS_MUNMAP]        = sys_munmap_wrapper;
    syscall_table[SYS_CONSOLE_WRITE] = sys_console_write_wrapper;
    syscall_table[SYS_IPC_SEND]      = sys_ipc_send_wrapper;
    syscall_table[SYS_IPC_RECV]      = sys_ipc_recv_wrapper;
    syscall_table[SYS_IPC_CALL]      = sys_ipc_call_wrapper;
    syscall_table[SYS_IPC_REPLY]     = sys_ipc_reply_wrapper;
    syscall_table[SYS_IO_READ]       = sys_io_read_wrapper;
    syscall_table[SYS_IO_WRITE]      = sys_io_write_wrapper;
    syscall_table[SYS_MAP_DEVICE]    = sys_map_device_wrapper;
    syscall_table[SYS_MEM_GRANT]     = sys_mem_grant_wrapper;
    syscall_table[SYS_IRQ_CLAIM]     = sys_irq_claim_wrapper;
    syscall_table[SYS_IRQ_ACK]       = sys_irq_ack_wrapper;
    syscall_table[SYS_DROP_PRIVILEGE] = sys_drop_privilege_wrapper;

    /* 架构相关的系统调用入口（INT 0x80 / SYSCALL / SVC） */
    hal::Syscall::init(NULL);

    LOG_INFO_MSG("System calls initialized (%d calls)\n", (int)SYS_MAX);
}
