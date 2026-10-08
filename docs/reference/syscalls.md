# 系统调用表

内核一共有 33 个系统调用。编号在 `src/include/kernel/syscall.h`，用户态在 `user/lib/include/syscall.h` 里有一份相同的定义，包装函数在 `user/lib/src/syscall.cpp`。陷入内核的方式、参数校验和返回值的约定见 [概念：系统调用](../concepts/07-system-calls.md)。

标了 ⚡ 的调用不拿内核锁就运行，几个 CPU 上的进程可以同时在里面（见 [多个 CPU](smp.md) 的“不拿内核锁的系统调用”）；其余的调用同一时刻只有一个 CPU 在执行。

| 编号 | 调用 | 说明 |
|------|------|------|
| 0 | `exit(code)` | |
| 1 | `fork()` | 写时复制 |
| 2 | `exec(image, size, args, args_size)` | 用调用者内存里的 ELF 映像替换当前进程，并把参数块交给新程序；内核不认识路径。映像最大 16MB |
| 3 | `waitpid(pid, wstatus, options)` | 阻塞到子进程退出；支持 `WNOHANG` |
| 4 / 5 | `getpid()` / `getppid()` | `getpid` ⚡ |
| 6 | `yield()` | |
| 7 | `kill(pid, signal)` | 没有信号处理函数：非 0 信号终止目标；非特权进程只能发给自己和子孙。信号 0 只探测进程是否存在，谁都可以用 |
| 8 | `nanosleep(req, rem)` | |
| 9 | `brk(addr)` | ⚡ |
| 10 / 11 | `mmap(...)` / `munmap(addr, len)` | ⚡ 只支持匿名映射 |
| 12 | `console_write(buf, len)` | 写内核串口控制台（调试输出）；一次最多 4096 字节，多出来的不写，返回实际写的字节数 |
| 13 | `ipc_send(dest, msg)` | ⚡ 把消息发给 PID `dest`，阻塞到对方收下 |
| 14 | `ipc_recv(from, msg)` | ⚡ 接收消息；`from` 为 `IPC_ANY`、指定 PID，或 `IPC_FROM_KERNEL`（只收内核发来的消息：设备中断和定时器到期） |
| 15 | `ipc_call(dest, msg)` | ⚡ 发送请求并等待 `dest` 的应答，应答写回 `msg` |
| 16 | `ipc_reply(dest, msg)` | ⚡ 应答正在 `call` 自己的进程，从不阻塞 |
| 17 | `mem_grant(pid, addr, len)` | 把自己的一段内存共享给 `pid`，对方收到内核发来的授予通知 |
| 18 / 19 | `io_read(port, width, value*)` / `io_write(port, width, value)` | x86 的 I/O 端口；需要特权，或者端口在许可表里。许可表里的端口用户库直接用指令访问，不经过这两个调用 |
| 20 | `map_device(phys, len)` | 把设备内存映射进自己的地址空间；需要特权，或者这段内存在许可表里 |
| 21 / 22 | `irq_claim(irq)` / `irq_ack(irq)` | 认领设备中断 / 处理完毕后重新打开；认领需要特权，或者这条线在许可表里 |
| 23 | `drop_privilege()` | 放弃特权，不可恢复；许可表留着 |
| 24 | `dma_alloc(len, phys*)` | 物理连续的内存，返回虚拟地址并告知物理地址；只给驱动（有特权，或者许可表不空） |
| 25 | `uptime_ms(ms*)` | 开机以来的毫秒数 |
| 26 | `timer_set(ms)` | 一次性定时器：到期时收到内核发来的 `IPC_LABEL_TIMER` 消息；0 取消 |
| 27 | `mem_free_pages()` | ⚡ 还没有分配出去的物理页数；自检用它检查进程退出后内存全部归还 |
| 28 | `device_find(info*)` | 按型号（设备树的 `compatible`）查平台设备的寄存器地址和中断号，仅特权进程 |
| 29 | `hw_allow(kind, start, count)` | 往自己的许可表里加一条：一段 I/O 端口、一段设备内存或者几条中断线。仅特权进程 |
| 30 | `hw_allowed(index, range*)` | ⚡ 读自己许可表里的第 `index` 条；驱动据此得知自己的设备在哪里 |
| 31 | `cpu_info(count*)` | ⚡ 返回调用者此刻在哪个 CPU 上运行（从 0 开始），`*count` 得到正在运行的 CPU 个数。见 [多个 CPU](smp.md) |
| 32 | `power(action)` | 关机（`POWER_OFF`）或重启（`POWER_REBOOT`），成功不返回；这台机器的固件没有给出办法时返回 -1。仅特权进程 |

各组调用的详细说明：

- 13–17、25–26（IPC、共享内存、定时器）：[进程间通信](ipc.md)
- 18–24、28–30、32（端口、设备内存、中断、DMA、许可表、关机和重启）：[特权与硬件访问](hardware.md)
- 2（`exec` 的参数块怎么到达 `main`）：[启动映像和命令行](shell.md#程序参数)
