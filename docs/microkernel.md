# 微内核结构

CastorOS 的内核只保留四件事：CPU/中断、内存管理、任务调度、系统调用。
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

共 14 个，编号在 `src/include/kernel/syscall.h`，用户态包装在 `user/lib`。

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

内核直接创建的进程（init）带 `privileged` 标志，`fork` 继承，`exec` 之后失去。

## 怎么加模块

模块是普通的用户程序：在 `user/` 下新建目录，链接 `user/lib`，由 init 通过 `fork` + `exec(image, size)` 启动（映像可以 `.incbin` 在 init 里）。

目前内核还缺两样东西，加第一个真正的驱动/服务模块之前需要先补上：

- **进程间通信**：还没有 IPC 调用，模块之间、模块与客户进程之间无法互相请求服务。
- **硬件访问**：用户态还不能映射设备内存、访问 I/O 端口或接收中断。
