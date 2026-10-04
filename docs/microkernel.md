# 微内核结构

CastorOS 的内核只保留五件事：CPU/中断、内存管理、任务调度、系统调用、进程间通信。
文件系统、网络协议栈、设备驱动、shell 都不在内核里，要以用户态程序（模块）的形式加回来。

## 内核里有什么

| 目录 | 内容 |
|------|------|
| `src/arch/<arch>/` | 启动、GDT/IDT 或异常向量、中断控制器、页表、上下文切换、系统调用入口、HAL 实现 |
| `src/mm/` | 物理页帧分配（PMM）、地址空间与写时复制（VMM）、内核堆 |
| `src/kernel/` | 调度器与任务（`task.cpp`）、系统调用分发（`syscall.cpp`、`syscalls/`）、ELF 加载、同步原语、用户指针校验 |
| `src/drivers/<x86\|arm>/` | 只有两个：串口（内核控制台）和时钟（调度 tick） |
| `src/lib/` | `kprintf`/`klog`、字符串库、最小 C++ 运行时 |
| `src/tests/` | 内核测试，只在 `KTEST=1`（`make test`）时编入 |

## 启动流程

1. `kernel_main`（`src/kernel/kernel.cpp`）：串口 → `hal::Cpu::init` → `hal::Interrupt::init` → `syscall_init` → PMM / VMM / 堆 → 时钟。
2. `kernel_start`：初始化调度器，开中断，（`KTEST=1` 时）运行内核测试。
3. `load_init`（`src/kernel/loader.cpp`）：把内嵌的 init 映像加载成 PID 1，进入调度。

三个架构都用 QEMU 的 `-kernel` 直接启动：x86 走 Multiboot1（x86_64 内核另外生成一份 ELF32 外壳 `castor32.elf`），arm64 走 `-M virt`，启动信息来自 DTB。真机上任何支持 Multiboot1 的引导器（如 GRUB 的 `multiboot /boot/castor.bin`）都可以加载 x86 内核。

## init

`user/init` 是第一个用户进程。构建内核时先编译出 `user/init/build/<arch>/init.elf`，再由 `src/kernel/init_image.S` 用 `.incbin` 嵌进内核映像，不需要磁盘或文件系统。

## 系统调用

共 17 个，编号在 `src/include/kernel/syscall.h`，用户态包装在 `user/lib`。

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
| 12 | `console_write(buf, len)` | 写内核串口控制台 |
| 13 | `console_read(buf, len)` | 非阻塞读串口，返回读到的字节数 |
| 14 | `ipc_send(dest, msg)` | 把消息发给 PID `dest`，阻塞到对方收下 |
| 15 | `ipc_recv(from, msg)` | 接收消息；`from` 为 `IPC_ANY` 或指定 PID |
| 16 | `ipc_call(dest, msg)` | 发送请求并等待 `dest` 的应答，应答写回 `msg` |

内核直接创建的进程（init）带 `privileged` 标志，`fork` 继承，`exec` 之后失去。

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

当前的限制：没有名字服务（客户要通过 fork 的返回值等方式知道服务的 PID）；大块数据还不能传递（没有共享内存）；应答用的是普通 `send`，客户若不去 `recv`，服务会阻塞在应答上。

## 用户态的约束

内核切换任务时不保存浮点/SIMD 寄存器，所以用户程序用 `-mno-sse`（x86_64）/ `-mgeneral-regs-only`（arm64）编译，不能使用浮点数。

## 怎么加模块

模块是普通的用户程序：在 `user/` 下新建目录，链接 `user/lib`，由 init 通过 `fork` + `exec(image, size)` 启动（映像可以 `.incbin` 在 init 里），对外用 IPC 提供服务。

加第一个真正的驱动模块之前，内核还缺一样东西：

- **硬件访问**：用户态还不能映射设备内存、访问 I/O 端口或接收中断。
