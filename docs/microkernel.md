# 微内核结构

CastorOS 的内核只保留五件事：CPU/中断、内存管理、任务调度、系统调用、进程间通信。文件系统、网络协议栈、设备驱动、shell 都不在内核里，它们是用户态程序（模块）。

这一篇说明内核的边界和用户态由哪些部分组成；每个部分的细节在 [reference/](reference/) 下各有一篇，列在[最后](#各部分的说明)。

## 内核里有什么

| 目录 | 内容 |
|------|------|
| `src/arch/<arch>/` | 启动、GDT/IDT 或异常向量、中断控制器、页表项的格式、上下文切换、系统调用入口、HAL 实现 |
| `src/mm/` | 物理页帧分配（PMM）、多级页表的通用操作（`pagetable.cpp`：映射、克隆、销毁，三个架构共用一份，表项格式由各架构通过 `hal/pt.h` 提供）、地址空间与写时复制（VMM）、内核堆 |
| `src/kernel/` | 调度器（`sched.cpp`）、任务表和进程的创建与退出（`task.cpp`）、系统调用分发（`syscall.cpp`、`syscalls/`）、IPC（`ipc.cpp`）、中断转发（`user_irq.cpp`）、ELF 加载、自旋锁、用户指针校验（`uaccess.cpp`）、硬件许可表（`hw_access.cpp`） |
| `src/drivers/<x86\|arm>/` | 调试输出（串口；x86 上还有 VGA 文本屏幕，同一份输出两边都写）和时钟（调度 tick） |
| `src/lib/` | `kprintf`/`klog`、字符串库、最小 C++ 运行时 |
| `src/tests/` | 内核测试，只在 `KTEST=1`（`make test`）时编入 |

内核对用户态提供 32 个系统调用：进程、内存、调试输出、同步 IPC、共享内存、时间和定时器，以及给用户态驱动用的硬件访问。完整的表见 [系统调用表](reference/syscalls.md)。

## 启动流程

1. `kernel_main`（`src/kernel/kernel.cpp`）：串口（x86 上还有屏幕） → `hal::Cpu::init` → `hal::Interrupt::init` → `syscall_init` → PMM / VMM / 堆 → 时钟。arm64 上串口之后先解析设备树：内存的范围，以及串口、中断控制器、定时器的位置都从那里来。
2. `kernel_start`：初始化调度器，开中断，（`KTEST=1` 时）运行内核测试。
3. `load_init`（`src/kernel/loader.cpp`）：把内嵌的 init 映像加载成 PID 1，进入调度。

三个架构都用 QEMU 的 `-kernel` 直接启动：x86 走 Multiboot1（x86_64 内核另外生成一份 ELF32 外壳 `castor32.elf`），arm64 走 `-M virt`，启动信息来自 DTB。真机上任何支持 Multiboot1 的引导器都可以加载 x86 内核；`make iso` 做一个带 GRUB 的可引导映像，见 [在真机上运行](testing.md#在真机上运行)。

## init 和模块

`user/init` 是第一个用户进程，负责启动模块并充当名字服务。内核保证它的 PID 是 1（普通任务从 2 开始编号），用户态把这个 PID 当作名字服务的固定地址。构建内核时先编译出 `user/init/build/<arch>/init.elf`，去掉调试信息（`init.stripped.elf`）后由 `src/kernel/init_image.S` 用 `.incbin` 嵌进内核映像，不需要磁盘或文件系统。

init 要启动的模块用同样的办法嵌在 init 自己的映像里（`user/init/modules.S`），启动方式是 `fork` + `exec`。有特权的只有 init 自己：每个模块都是放弃特权之后才启动的。驱动在放弃之前由 init 把它的设备许可给它（见 [特权与硬件访问](reference/hardware.md)），之后只碰得到这一个设备。目前有八个（arm64 上没有 `kbd`，是七个）：

- `user/console`：终端输入服务，不碰硬件。输入设备的驱动把字符送到这里，终端的输入归谁由它管。
- `user/uart`：串口输入驱动，得到串口的寄存器和中断线。收到的字节交给 console。没有串口时直接退出。
- `user/kbd`：PS/2 键盘驱动（只有 x86），得到键盘控制器的两个端口和中断线。敲出来的字符交给 console。
- `user/blk`：块设备驱动（virtio-blk；x86 上没有 virtio 磁盘时是第一个 IDE 通道上的硬盘），得到磁盘的寄存器和中断线。没有磁盘时直接退出。
- `user/net`：网络服务（virtio-net 驱动加协议栈：ARP、IPv4、ICMP、UDP、TCP，启动时用 DHCP 取地址），得到网卡的寄存器和中断线。没有网卡时它直接退出。
- `user/ramfs`：内存文件系统服务，不碰硬件。启动映像嵌在它里面。
- `user/diskfs`：磁盘文件系统服务，不碰硬件（读写磁盘是发给 blk 的消息）。等大约 1 秒还没有块设备就退出。
- `user/sh`：命令行，不碰硬件。输入来自 console 服务，文件操作交给文件服务。

**模块退出了怎么办。** init 每 100 毫秒看一眼有没有子进程退出。工作着的模块退出了（崩溃、被杀）就重启它：再 `fork` + `exec` 一次，驱动重新走一遍许可设备的步骤，服务名转给新的进程。“工作着”指登记过服务名；一启动就发现没有自己的设备、没登记就退出的驱动（没有磁盘时的 `blk`）不重启，再来一次结果也一样。一个模块最多重启 5 次，免得一启动就崩溃的模块没完没了。

重启的是进程，不是状态：新的服务从头开始，原来的连接、打开的文件、套接字都没有了，`ramfs` 只剩启动映像里的内容。客户端库在请求失败时忘掉旧的连接，下一次请求重新按名字找，所以客户不用重启，只是会看到一次失败。命令行重启时带一个参数，据此不再执行开机脚本；console 重启后命令行自己重新登记为终端的主人，串口和键盘的驱动下一次送字符时重新按名字找到它（新的 console 登记之前敲的字符丢了）；串口或键盘的驱动重启不影响 console 记着的状态。

驱动重启有一个没法在这里解决的窗口：驱动死了，设备不知道，还在往驱动原来的内存（已经被内核收回）里读写，直到 init 发现并在重新许可设备时把它复位（`virtio_allow`）。没有 IOMMU 就拦不住这段时间里的写入。

## 启动映像里的程序

其余程序不嵌在 init 里，而是放在启动映像中，由命令行从文件服务里读出来运行（见 [启动映像和命令行](reference/shell.md)）：

- `user/selftest`：用户态自检（程序参数、内存、进程、IPC 的各条阻塞和退出路径、特权、浮点寄存器、共享内存、定时器、名字服务、文件服务、块设备、磁盘文件系统、网络），开机时由 `rc` 脚本运行一次，每项打印一行结果。其中一项反复创建、结束进程（正常退出、`exec` 后退出、阻塞时被杀），比较前后的空闲物理页数，必须完全相等。`make test` 要求它全部通过。需要有人敲键盘才能测的行为（后台任务、Ctrl-C、`kill`）不在这里，而是由宿主机上的 `scripts/shell-test.sh` 通过串口输入命令来检查，同样是 `make test` 的一部分。
- `user/ls`、`user/cat`、`user/cp`、`user/rm`、`user/mv`、`user/mkdir`、`user/echo`、`user/sleep`：小工具。`ls [目录]` 列出一个目录，目录的名字后面带 `/`；`mkdir` 建目录；`rm` 也能删空目录。
- `user/grep`、`user/wc`：过滤器，从标准输入读：留下含指定文字的行、数行数词数字节数。`cat` 不带文件名时也把标准输入抄到标准输出。
- `user/write`：`write <file> <text>` 把参数写成文件的一行；不带文字时从标准输入读，每行写进文件（键盘输入时行首 Ctrl-D 结束）。
- `user/disk`：显示磁盘容量、直接读写扇区。
- `user/ping`、`user/ifconfig`、`user/dns`、`user/http`、`user/echod`：网络工具。
- `user/hello`：最小的示例程序，打印自己的 PID 和参数。

加一个程序只需要：在 `user/` 下建目录写好 Makefile，把名字加进 `user/ramfs/Makefile` 的 `BOOT_PROGRAMS`。不用改 init，也不用改内核。

## 怎么加模块

普通程序放进启动映像即可（见上一节）。下面是加一个开机就要常驻的服务或驱动：

1. 在 `user/` 下新建目录，写一个三行的 Makefile（`TARGET`、`SOURCES`、`include ../program.mk`），链接 `user/lib`。
2. 定义它的 IPC 协议（请求的 `label` 和 `data` 布局；要传大块数据就像文件服务那样用共享缓冲区），主循环按[服务进程的结构](reference/ipc.md#服务进程的结构)写，启动后用 `name_register` 登记自己的名字。
3. 把它加进 `user/init/Makefile` 的 `MODULE_NAMES` 和 `user/init/modules.S`，在 init 的 `main` 里用 `start_module(名字, 服务名, 映像起始, 映像结束, allow)` 启动。服务名是留给这个模块登记用的名字（见 [名字服务](reference/ipc.md#名字服务)），不提供服务的模块传 `NULL`。最后一个参数只有驱动需要：一个 `allow_*` 函数，在子进程放弃特权之前运行，找到驱动的设备并用 `hw_allow` 许可给它；其余模块传 `NULL`。驱动自己用 `hw_find` 取出许可给它的端口（或设备内存）和中断线。
4. 客户用 `name_wait("名字")` 拿到 PID，再用 `ipc_call` 发请求。

## 各部分的说明

| 文档 | 内容 |
|------|------|
| [系统调用表](reference/syscalls.md) | 32 个系统调用的编号、参数和权限要求 |
| [进程间通信](reference/ipc.md) | 同步消息传递、超时、共享内存、名字服务 |
| [特权与硬件访问](reference/hardware.md) | 特权、许可表、I/O 端口、设备内存、DMA、设备中断 |
| [用户态驱动](reference/drivers.md) | 终端输入服务 console、串口驱动 uart、键盘驱动 kbd、virtio 的公共部分、块设备驱动 blk |
| [文件服务](reference/fs.md) | 协议、路径和目录、ramfs 和 diskfs、磁盘格式 |
| [网络](reference/net.md) | 客户端接口、协议栈、DHCP、TCP、在 QEMU 里使用 |
| [启动映像和命令行](reference/shell.md) | 运行程序、后台任务、键盘输入、重定向和管道、脚本、程序参数 |
| [浮点数](reference/floating-point.md) | 用户程序的浮点/SIMD 状态、`printf` 的 `%f`、数学函数 |
| [多个 CPU](reference/smp.md) | 内核锁、每个 CPU 的状态、其余的 CPU 怎么启动（arm64 和 x86） |

这些机制背后的原理（引导、分页、中断、调度、同步）见 [concepts/](concepts/README.md)。
