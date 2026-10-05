# 微内核结构

CastorOS 的内核只保留五件事：CPU/中断、内存管理、任务调度、系统调用、进程间通信。
文件系统、网络协议栈、设备驱动、shell 都不在内核里，要以用户态程序（模块）的形式加回来。

## 内核里有什么

| 目录 | 内容 |
|------|------|
| `src/arch/<arch>/` | 启动、GDT/IDT 或异常向量、中断控制器、页表、上下文切换、系统调用入口、HAL 实现 |
| `src/mm/` | 物理页帧分配（PMM）、地址空间与写时复制（VMM）、内核堆 |
| `src/kernel/` | 调度器与任务（`task.cpp`）、系统调用分发（`syscall.cpp`、`syscalls/`）、IPC（`ipc.cpp`）、中断转发（`user_irq.cpp`）、ELF 加载、同步原语、用户指针校验 |
| `src/drivers/<x86\|arm>/` | 只有两个：串口（只做调试输出）和时钟（调度 tick） |
| `src/lib/` | `kprintf`/`klog`、字符串库、最小 C++ 运行时 |
| `src/tests/` | 内核测试，只在 `KTEST=1`（`make test`）时编入 |

## 启动流程

1. `kernel_main`（`src/kernel/kernel.cpp`）：串口 → `hal::Cpu::init` → `hal::Interrupt::init` → `syscall_init` → PMM / VMM / 堆 → 时钟。
2. `kernel_start`：初始化调度器，开中断，（`KTEST=1` 时）运行内核测试。
3. `load_init`（`src/kernel/loader.cpp`）：把内嵌的 init 映像加载成 PID 1，进入调度。

三个架构都用 QEMU 的 `-kernel` 直接启动：x86 走 Multiboot1（x86_64 内核另外生成一份 ELF32 外壳 `castor32.elf`），arm64 走 `-M virt`，启动信息来自 DTB。真机上任何支持 Multiboot1 的引导器（如 GRUB 的 `multiboot /boot/castor.bin`）都可以加载 x86 内核。

## init 和模块

`user/init` 是第一个用户进程（PID 1），负责启动模块并充当名字服务。构建内核时先编译出 `user/init/build/<arch>/init.elf`，再由 `src/kernel/init_image.S` 用 `.incbin` 嵌进内核映像，不需要磁盘或文件系统。

init 要启动的模块用同样的办法嵌在 init 自己的映像里（`user/init/modules.S`），启动方式是 `fork` + `exec(image, size)`。目前有两个：

- `user/uart`：串口输入驱动，保留特权启动。
- `user/demo`：示例程序，放弃特权后启动。它演示内存、进程、IPC、特权和名字服务，然后按名字找到 uart 驱动并回显输入。

## 系统调用

共 21 个，编号在 `src/include/kernel/syscall.h`，用户态包装在 `user/lib`。

| 编号 | 调用 | 说明 |
|------|------|------|
| 0 | `exit(code)` | |
| 1 | `fork()` | 写时复制 |
| 2 | `exec(image, size)` | 用调用者内存里的 ELF 映像替换当前进程；内核不认识路径 |
| 3 | `waitpid(pid, wstatus, options)` | 支持 `WNOHANG` |
| 4 / 5 | `getpid()` / `getppid()` | |
| 6 | `yield()` | |
| 7 | `kill(pid, signal)` | 没有信号处理函数：非 0 信号终止目标；非特权进程只能发给自己和子孙 |
| 8 | `nanosleep(req, rem)` | |
| 9 | `brk(addr)` | |
| 10 / 11 | `mmap(...)` / `munmap(addr, len)` | 只支持匿名映射 |
| 12 | `console_write(buf, len)` | 写内核串口控制台（调试输出） |
| 13 | `ipc_send(dest, msg)` | 把消息发给 PID `dest`，阻塞到对方收下 |
| 14 | `ipc_recv(from, msg)` | 接收消息；`from` 为 `IPC_ANY` 或指定 PID |
| 15 | `ipc_call(dest, msg)` | 发送请求并等待 `dest` 的应答，应答写回 `msg` |
| 16 / 17 | `io_read(addr, width, value*)` / `io_write(addr, width, value)` | 读写设备寄存器，仅特权进程 |
| 18 / 19 | `irq_claim(irq)` / `irq_ack(irq)` | 认领设备中断 / 处理完毕后重新打开，仅特权进程 |
| 20 | `drop_privilege()` | 放弃特权，不可恢复 |

## 进程间通信

IPC 是模块之间、模块与客户进程之间唯一的通信方式（`src/kernel/ipc.cpp`）。

- **同步会合，没有缓冲**：`send` 阻塞到对方 `recv`，`recv` 阻塞到有人 `send`。消息只存放在阻塞一方的 PCB 里，内核里没有消息队列。
- **定长消息**：`struct ipc_msg { sender; label; data[6]; }`，共 56 字节，布局在三个架构上相同。`sender` 由内核填写，无法伪造；`label` 和 `data` 的含义由通信双方约定。
- **按 PID 寻址**：PID 不复用，向已退出的进程发送会失败。
- **退出与 kill**：进程退出时，正在向它发送或只等它消息的进程带着 -1 返回；阻塞在 IPC 上的进程可以被 `kill`。

服务进程的典型结构：

```cpp
struct ipc_msg m;
for (;;) {
    ipc_recv(IPC_ANY, &m);        // 等请求
    /* 按 m.label 处理，结果写回 m */
    ipc_send(m.sender, &m);       // 应答
}
```

客户进程用 `ipc_call(server_pid, &m)` 一次完成请求和应答。`user/init/init.cpp` 里有一个完整的例子。

当前的限制：大块数据还不能传递（没有共享内存）；应答用的是普通 `send`，客户若不去 `recv`，服务会阻塞在应答上（init 的名字服务也是如此）。

## 名字服务

客户进程按名字找到服务的 PID。服务端在 init 里（`user/init/init.cpp`），所以地址固定是 PID 1；协议和客户端接口在 `user/lib`（`names.h`）：

- `name_register(name)`：把名字登记到调用者名下。登记的 PID 取自内核填写的 `sender`，不能替别人登记；名字已被占用时失败。
- `name_lookup(name)`：返回 PID，没有登记返回 0。
- `name_wait(name)`：轮询到名字出现为止，用于启动顺序不确定的场合。

名字最长 47 个字符，最多登记 16 个。init 在处理请求时顺便回收已退出的子进程，并注销它们登记的名字。

当前的限制：任何进程都可以登记任何还没被占用的名字，没有访问控制；只有 init 的直接子进程退出后名字才会被注销。

## 特权与硬件访问

用户态驱动需要碰硬件，内核为此提供三样东西，都只对带 `privileged` 标志的进程开放：

- **特权的来源**：内核直接创建的 init 有特权，`fork` 和 `exec` 都保留；进程调用 `drop_privilege()` 之后永久失去。init 启动驱动时保留特权，启动其他模块时先让子进程放弃。
- **设备寄存器**：`io_read` / `io_write`，宽度 1/2/4 字节。x86 上 `addr` 是 I/O 端口号；arm64 上是寄存器的物理地址（限 QEMU virt 的设备区，1GB 以下），由内核代为访问。
- **设备中断**：`irq_claim(irq)` 认领一条内核自己没在用的中断线（x86 是 PIC 的 IRQ 号，arm64 是 GIC 的 SPI 中断号）。中断到来时内核屏蔽这条线，并向属主投递一条 `sender == IPC_KERNEL`、`label == IPC_LABEL_IRQ`、`data[0] == irq` 的消息；属主没在 `recv` 时记为待处理，下一次 `recv(IPC_ANY)` 先收到它。驱动处理完设备后调用 `irq_ack(irq)` 重新打开中断线。进程退出时它认领的中断线被屏蔽并释放。

实现在 `src/kernel/user_irq.cpp` 和 `src/kernel/syscall.cpp`。

当前的限制：设备寄存器每次访问都是一次系统调用，还不能把设备内存映射进用户地址空间（帧缓冲、网卡这类设备需要）；特权是全有或全无的，没有按设备授权。

## 第一个用户态驱动：uart

`user/uart` 是串口输入驱动（x86 的 16550 / arm64 的 PL011）。内核只用串口做输出，接收方向完全在驱动里：

1. 启动时 `irq_claim` 串口中断，打开设备的接收中断。
2. 主循环 `ipc_recv(IPC_ANY)`：收到内核的中断消息就把硬件 FIFO 读进自己的缓冲区并 `irq_ack`；收到 `UART_READ` 请求就记下读者。
3. 缓冲区里有数据且有读者在等时，把数据作为应答发回去。

协议定义在 `user/uart/uart.h`。驱动以 `"uart"` 这个名字登记；`user/demo` 用 `name_wait("uart")` 找到它，再用 `ipc_call(uart, UART_READ)` 取输入并回显。

## 用户态的约束

内核切换任务时不保存浮点/SIMD 寄存器，所以用户程序用 `-mno-sse`（x86_64）/ `-mgeneral-regs-only`（arm64）编译，不能使用浮点数。

## 怎么加模块

1. 在 `user/` 下新建目录，写一个三行的 Makefile（`TARGET`、`SOURCES`、`include ../program.mk`），链接 `user/lib`。
2. 定义它的 IPC 协议（请求的 `label` 和 `data` 布局），主循环按上面服务进程的结构写，启动后用 `name_register` 登记自己的名字。
3. 把它加进 `user/init/Makefile` 的 `MODULE_NAMES` 和 `user/init/modules.S`，在 init 的 `main` 里用 `start_module` 启动；最后一个参数决定它是否保留特权（只有驱动需要）。
4. 客户用 `name_wait("名字")` 拿到 PID，再用 `ipc_call` 发请求。
