# 系统调用

系统调用是用户程序进入内核的唯一入口。CastorOS 的系统调用很少（32 个，完整的表在 [系统调用表](../reference/syscalls.md)）：进程、内存、调试输出、IPC，以及给用户态驱动用的硬件访问。文件、网络这些功能不是系统调用，而是发给用户态服务进程的 IPC 消息。

## 陷入机制和调用约定

三个架构用各自的指令陷入内核，参数都放在寄存器里：

| 架构 | 指令 | 调用号 | 参数 1–6 | 返回值 |
|------|------|--------|----------|--------|
| i686 | `int 0x80` | EAX | EBX, ECX, EDX, ESI, EDI, EBP | EAX |
| x86_64 | `syscall` | RAX | RDI, RSI, RDX, R10, R8, R9 | RAX |
| arm64 | `svc #0` | X8 | X0–X5 | X0 |

x86_64 的内核另外保留了一个 `int 0x80` 入口，用户态库不用它。

用户态库把这层差异包在 `syscall0` … `syscall6` 里（`user/lib/src/arch/<arch>/syscall.S`），其上的 C 包装（`user/lib/src/syscall.cpp`）对三个架构是同一份代码：

```cpp
int fork(void) {
    return (int)syscall0(SYS_FORK);
}

int ipc_call(int dest, struct ipc_msg *msg) {
    return (int)syscall2(SYS_IPC_CALL, (syscall_arg_t)dest, PTR_TO_ARG(msg));
}
```

## 系统调用流程

```text
用户空间                               内核空间
--------                              --------
syscallN() ── int 0x80 / syscall / svc ─→  架构相关的入口（汇编）
                                              │  保存用户寄存器，切到内核栈
                                              ▼
                                        syscall_dispatcher(num, p1..p5, frame)
                                              │  查表 syscall_table[num]
                                              ▼
                                        sys_xxx_wrapper：校验用户指针和权限
                                              │
                                              ▼
                                        实现函数（kernel::Ipc、syscall::Process、syscall::Mm …）
                                              │
                                              ▼
                                        返回前投递待处理的 kill
        ←── iret / sysret / eret ──────  恢复用户寄存器，返回值放进约定的寄存器
```

入口的汇编在 `src/arch/<arch>/syscall/`，分发器和各个包装函数在 `src/kernel/syscall.cpp`。`frame` 指向保存的用户寄存器：`fork` 用它复制出子进程的上下文，`exec` 改写它，让这次系统调用“返回”到新程序的入口。

## 分发

调用号是一张小而连续的表的下标：

```cpp
syscall_arg_t syscall_dispatcher(syscall_arg_t syscall_num, syscall_arg_t p1, ...,
                                 syscall_arg_t *frame) {
    if (syscall_num >= SYS_MAX) {
        return (syscall_arg_t)-1;
    }
    syscall_handler_t handler = syscall_table[syscall_num];
    if (handler == NULL) {
        return (syscall_arg_t)-1;
    }

    syscall_arg_t ret = handler(frame, p1, p2, p3, p4, p5);

    /* 返回用户态之前处理别的任务发来的 kill：此时本任务不持有任何内核锁 */
    kernel::Scheduler::deliver_pending_kill();
    return ret;
}
```

调用号定义在 `src/include/kernel/syscall.h`，用户态在 `user/lib/include/syscall.h` 里有一份相同的定义，两边必须一致。

## 参数校验

内核不信任用户传来的任何东西。包装函数在调用实现之前做三类检查：

**用户指针。** 地址和长度都来自用户态，使用前要确认整个区间落在当前进程的用户地址空间内，并且每一页都以用户权限映射（`kernel::UAccess`）：

```cpp
static syscall_arg_t sys_ipc_recv_wrapper(syscall_arg_t *frame, syscall_arg_t from,
                                          syscall_arg_t msg, ...) {
    if (!user_wr(msg, sizeof(ipc_msg))) return SYSCALL_FAIL;
    return sys_ret32((uint32_t)kernel::Ipc::recv((uint32_t)from, (ipc_msg *)(uintptr_t)msg));
}
```

`can_write` 还会把处于写时复制状态的页先复制出来，这样内核随后往里写不会再触发缺页。

**权限。** 访问硬件的调用先问“当前进程可不可以碰这段资源”：有特权（只有 init），或者这段端口、设备内存、这条中断线在进程的许可表里（init 启动驱动时填的，见 [特权与硬件访问](../reference/hardware.md)）：

```cpp
static syscall_arg_t sys_irq_claim_wrapper(syscall_arg_t *frame, syscall_arg_t irq, ...) {
    if (!kernel::HwAccess::current_may(HW_IRQ, irq, 1)) return SYSCALL_FAIL;
    return sys_ret32((uint32_t)kernel::UserIrq::claim((uint32_t)irq));
}
```

**内容。** 例如用户进程发出的 IPC 消息不能使用内核保留的 label（见 [进程间通信](../reference/ipc.md)），设备内存映射只接受设备地址区。

## 返回值

没有 `errno`。约定很简单：失败返回负数（指针类的调用返回 `MAP_FAILED`），成功返回 0 或有意义的值。负数几乎都是 -1，唯一的例外是 `fork` 在内存或任务表不够时返回 -12。实现函数用 32 位值表示结果，分发层统一做符号扩展，保证 64 位架构上用户态看到的也是负数：

```cpp
static inline syscall_arg_t sys_ret32(uint32_t value) {
    return (syscall_arg_t)(intptr_t)(int32_t)value;
}
```

## 会阻塞的系统调用

`ipc_send` / `ipc_recv` / `ipc_call`、`mem_grant`（它要等对方收下授予通知）、`waitpid`、`nanosleep` 会让当前任务阻塞。内核不可抢占，所以阻塞总是发生在明确的点上（`Scheduler::block` / `Scheduler::sleep`）：任务把自己标成 BLOCKED 并切换出去，等别的任务或中断处理函数把它唤醒。被 `kill` 的任务如果正阻塞在这些调用里，会被提前唤醒，然后在系统调用出口处退出，不会再回到用户态。

## 加一个系统调用

1. 在 `src/include/kernel/syscall.h` 和 `user/lib/include/syscall.h` 里各加一个调用号。
2. 在 `src/kernel/syscall.cpp` 里写包装函数（校验参数），登记到 `syscall_table`。
3. 在 `user/lib/src/syscall.cpp` 里写用户态包装。

加之前先想一想它是不是真的需要在内核里：能用一个用户态服务加 IPC 做到的事，就不应该成为系统调用。
