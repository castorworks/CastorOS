# CastorOS 文档

| 想做什么 | 看哪里 |
|----------|--------|
| 把环境搭起来，第一次构建和运行 | [开发环境搭建](setup.md) |
| 跑测试、看日志、用 GDB | [测试与调试](testing.md) |
| 了解系统现在的样子：内核的边界、用户态有哪些部分 | [微内核结构](microkernel.md) |
| 查某个部分的细节：接口、协议、限制 | [reference/](#reference各部分的说明) |
| 弄懂背后的原理：分页、中断、调度是怎么回事 | [concepts/](concepts/README.md) |

## reference：各部分的说明

描述当前代码的行为。改了代码，对应的这一篇要跟着改。

| 文档 | 内容 |
|------|------|
| [系统调用表](reference/syscalls.md) | 31 个系统调用的编号、参数和权限要求 |
| [进程间通信](reference/ipc.md) | 同步消息传递、超时、共享内存、名字服务 |
| [特权与硬件访问](reference/hardware.md) | 特权、许可表、I/O 端口、设备内存、DMA、设备中断 |
| [用户态驱动](reference/drivers.md) | 串口驱动 uart、virtio 的公共部分、块设备驱动 blk |
| [文件服务](reference/fs.md) | 协议、路径和目录、ramfs 和 diskfs、磁盘格式 |
| [网络](reference/net.md) | 客户端接口、协议栈、DHCP、TCP、在 QEMU 里使用 |
| [启动映像和命令行](reference/shell.md) | 运行程序、后台任务、键盘输入、重定向和管道、脚本、程序参数 |
| [浮点数](reference/floating-point.md) | 用户程序的浮点/SIMD 状态、`printf` 的 `%f`、数学函数 |
| [多个 CPU](reference/smp.md) | 内核锁、每个 CPU 的状态、其余的 CPU 怎么启动（目前只有 arm64） |

## concepts：概念讲解

解释机制背后的原理，按阅读顺序编号。示例以 i686 为主。

| 文档 | 主题 |
|------|------|
| [01 引导过程](concepts/01-boot-process.md) | Multiboot、进入高半核、`kernel_main`、arm64 的设备树 |
| [02 高半核](concepts/02-higher-half-kernel.md) | 地址空间布局、内核页表共享、物理内存的直接映射 |
| [03 内存管理](concepts/03-memory-management.md) | PMM、页表、地址空间和写时复制、内核堆、用户进程的内存 |
| [04 中断与异常](concepts/04-interrupts.md) | IDT、异常、缺页、PIC、定时器、把设备中断交给用户态 |
| [05 进程管理](concepts/05-process-management.md) | PCB、上下文切换、调度、fork/exec/exit/wait、kill |
| [06 同步](concepts/06-synchronization.md) | 关中断、自旋锁、阻塞与唤醒 |
| [07 系统调用](concepts/07-system-calls.md) | 陷入机制、分发、参数校验、返回值 |

## 文档约定

- 文档用简体中文写；文件名、目录名用英文小写加连字符。给 AI 编码助手和贡献者看的约定在仓库根目录的 `AGENTS.md`，用英文写。
- 文档只描述现在的代码。过时的文档直接删掉，不留“不再维护”的存档：需要时从 git 历史里找（早期单内核阶段的开发记录 `docs/history/` 在 `b3393de` 及之前）。
- 每篇一个一级标题，紧跟一段说明这一篇讲什么，不另设“概述”小节。
- 一个事实只在一个地方写全，别处用链接指过去：现状写在 `microkernel.md` 和 `reference/`，原理写在 `concepts/`，怎么操作写在 `setup.md` 和 `testing.md`。
- 一段话写成一行，不在段落中间硬换行（中文硬换行渲染出来会多出空格）。
- 代码、命令、文件路径、函数名放进反引号；代码块标明语言（`cpp`、`asm`、`bash`，示意图用 `text`）。
- 中文引号用 “ ”，无序列表用 `-`。
- 每一节的已知不足写在这一节的最后，以“当前的限制”开头。
