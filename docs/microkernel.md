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

`user/init` 是第一个用户进程，负责启动模块并充当名字服务。内核保证它的 PID 是 1（普通任务从 2 开始编号），用户态把这个 PID 当作名字服务的固定地址。构建内核时先编译出 `user/init/build/<arch>/init.elf`，再由 `src/kernel/init_image.S` 用 `.incbin` 嵌进内核映像，不需要磁盘或文件系统。

init 要启动的模块用同样的办法嵌在 init 自己的映像里（`user/init/modules.S`），启动方式是 `fork` + `exec`。目前有六个：

- `user/uart`：串口输入驱动，保留特权启动。
- `user/blk`：virtio-blk 块设备驱动，保留特权启动。没有磁盘时它直接退出。
- `user/net`：网络服务（virtio-net 驱动加协议栈：ARP、IPv4、ICMP、UDP、TCP，启动时用 DHCP 取地址），保留特权启动。没有网卡时它直接退出。
- `user/ramfs`：内存文件系统服务，放弃特权后启动。启动映像嵌在它里面。
- `user/diskfs`：磁盘文件系统服务，放弃特权后启动。没有块设备时它直接退出。
- `user/sh`：命令行，放弃特权后启动。输入来自 uart 驱动，文件操作交给文件服务。

其余程序不嵌在 init 里，而是放在启动映像中，由命令行从文件服务里读出来运行：

- `user/selftest`：用户态自检（程序参数、内存、进程、IPC 的各条阻塞和退出路径、特权、共享内存、名字服务、文件服务、块设备、磁盘文件系统），开机时由 `rc` 脚本运行一次，每项打印一行结果。`make test` 要求它全部通过。需要有人敲键盘才能测的行为（后台任务、Ctrl-C、`kill`）不在这里，而是由宿主机上的 `scripts/shell-test.sh` 通过串口输入命令来检查，同样是 `make test` 的一部分。
- `user/ls`、`user/cat`、`user/cp`、`user/rm`、`user/echo`、`user/sleep`：小工具。
- `user/write`：`write <file> <text>` 把参数写成文件的一行；不带文字时从键盘读，每行写进文件，行首 Ctrl-D 结束。
- `user/disk`：显示磁盘容量、直接读写扇区。
- `user/ping`、`user/ifconfig`、`user/dns`、`user/http`、`user/echod`：网络工具。
- `user/hello`：最小的示例程序，打印自己的 PID 和参数。

## 网络

网络的协议和客户端接口在 `user/lib`（`net.h`），服务进程以 `"net"` 登记：

- `net_info`：本机的 MAC、IP、掩码、网关、DNS 服务器，以及地址是否来自 DHCP。IP 为 0 表示还在配置。
- `net_ping(ip, timeout, &rtt)`：发一个 ICMP 回显请求，阻塞到收到应答或超时。
- `net_udp_open` / `net_udp_send` / `net_udp_recv` / `net_udp_close`：UDP 套接字。`recv` 带超时；数据经共享缓冲区传递。
- `net_tcp_connect` / `net_tcp_send` / `net_tcp_recv` / `net_tcp_close`：TCP 连接。`connect` 和 `recv` 带超时；`recv` 返回 0 表示对方已关闭。
- `net_tcp_listen` / `net_tcp_accept`：在一个端口上监听，接受连进来的连接（之后同样用 send / recv / close）。
- `net_resolve(name, &ip)`：把主机名解析成地址（库里用 UDP 向配置的 DNS 服务器发一个 A 记录查询）。

`user/net` 把 virtio-net 驱动和协议栈放在同一个进程里：主循环用一个 `ipc_recv(IPC_ANY)` 同时等网卡中断、定时器和客户请求，收到的帧直接交给协议栈，不需要在驱动和协议栈两个进程之间再传一次。阻塞的请求（ping、recv）不立刻应答，等回显或数据报到了、或者定时器发现它超时了再 `ipc_reply`。

协议栈包括以太网、ARP（带重试，地址解析期间挂起一个待发的包）、IPv4（不分片）、ICMP 回显（也应答别人的 ping）、UDP、TCP。发给自己地址的包不经过网卡：它们进一个回环队列，回到主循环后再当作收到的包处理（不在发送的中途递归处理，否则会重入发送方还没更新完的状态）。

**地址配置。** 服务启动时用 DHCP 获取地址、掩码、网关和 DNS 服务器：DISCOVER → OFFER → REQUEST → ACK，每 500ms 重发一次。DHCP 客户端在服务内部，直接用协议栈收发（这时还没有地址，只能发广播、收广播），所以不需要一个“配置地址”的对外接口。3 秒内没有应答就退回 QEMU 用户网络的固定配置（10.0.2.15/24，网关 10.0.2.2，DNS 10.0.2.3）。配置完成之前 `net_info` 报告的 IP 是 0，ping 和 UDP 发送会失败。

**TCP**（`user/net/tcp.cpp`）是一个刻意保持简单的实现：

- 发送：每条连接一个 8KB 的环形缓冲区，放着还没被确认的数据。能发多少由对方通告的窗口决定；超时没被确认就从最早未确认处重发，超时时间每次翻倍（300ms 起，最多 4 秒），重试 8 次后放弃连接。对方窗口为 0 时发 1 字节探测。
- 接收：只接受按序到达的数据，放进 8KB 的环形缓冲区，通告窗口就是剩余空间；乱序的段丢掉并重复确认，靠对方重传。
- 关闭：`close` 之后已经交出去的数据仍会发完，再发 FIN，四次挥手在后台完成；支持对方先关、双方同时关。被复位或重试耗尽的连接，等着的请求都以失败返回。
- 监听：监听端口上收到 SYN 就建一条半开的连接并回 SYN+ACK，握手完成后排队等 `accept`（每个监听最多积压 4 条）。握手完成之后、`accept` 之前到的数据照常进接收缓冲区。关掉监听时还没被接受的连接一并复位。
- 没有拥塞控制、选择确认、窗口缩放、延迟确认，没有 TIME_WAIT。

为了能验证重传，服务有一个调试请求 `NET_DEBUG_DROP`（`net_debug_drop(tx, rx)`）：丢掉接下来发出的 tx 个、收到的 rx 个 TCP 帧，并返回至今的重传次数。它没有访问控制，任何进程都能调用。

`make run` 和 `make test` 都给 QEMU 挂一块接用户网络的 virtio-net 网卡，并用 `guestfwd` 把 10.0.2.100:7 接到宿主机的 `cat` 上，得到一个不依赖外网的 TCP 回显服务（自检用它）。工具有 `ifconfig`、`ping <ip>`、`dns <name>`、`http <host> [path]`（对 80 端口做一次 GET，打印应答的开头）和 `echod [port] [connections]`（TCP 回显服务）。`dns` 和 `http` 要经 QEMU 的转发器访问外网，能不能成取决于宿主机能否上网。

要从宿主机连进来，给 QEMU 的用户网络加端口转发，例如 `-netdev user,id=net0,hostfwd=tcp:127.0.0.1:8007-:7`，在命令行里运行 `echod`，然后在宿主机上 `nc 127.0.0.1 8007`。`make run` 默认不加转发（会占用宿主机的端口）。

当前的限制：DHCP 不续租（租约到期后继续用原地址）；不处理 IP 分片；TCP 连接和监听合计最多 8 个，8 个 UDP 套接字（每个最多积压 4 个数据报）、8 个同时进行的 ping；只支持 virtio 的 legacy 接口。

## 启动映像和运行程序

构建 `user/ramfs` 时，`user/bootfs/` 下的文件和 `BOOT_PROGRAMS` 里列出的程序（以去掉 `.elf` 的名字）被打成一个 ustar 归档，用 `.incbin` 嵌进 ramfs 的映像；ramfs 启动时把它展开成普通文件。

命令行有几个内置命令（`help`、`jobs`、`kill <pid>`）。其余的输入，第一个词是程序名：通过文件服务读出整个文件，`fork`，子进程带着这一行按空格切开的参数 `exec`，父进程等它结束并报告非零的退出状态。启动时它先执行文件 `rc` 里的每一行（目前只有 `selftest`）。

**后台任务和 Ctrl-C。** 行尾加 `&` 的程序在后台运行：命令行不等它，记进任务表（最多 8 个），它结束时打印一行 `[pid] done`。前台程序运行期间按 Ctrl-C，命令行用 `kill` 终止它；在提示符下按 Ctrl-C 放弃正在输入的这一行。

做法是命令行从不长时间阻塞在别处。等前台程序时，它循环做两件事：`waitpid(pid, WNOHANG)` 看程序是否结束，以及带 20ms 超时去读串口——读到 Ctrl-C 就 `kill`（这段时间别的输入归前台程序，见下）。空闲时，有后台任务就带 200ms 超时读串口（好及时报告任务结束），没有就一直等输入。被 `kill` 的程序即使正阻塞在某个服务的请求里也会立刻醒来退出；它在服务那边留下的东西（监听、套接字、缓冲区）由各个服务在下次分配时发现属主已退出再收回。

**键盘输入归谁。** 命令行是"终端的主人"：启动时向 uart 驱动登记（`UART_ATTACH`），每次运行前台程序前后告诉驱动谁在前台（`UART_SET_FOREGROUND`）。驱动据此把输入分成两路：有前台程序时输入归它，只有 Ctrl-C 仍然交给命令行；没有前台程序时输入都归命令行。后台任务读输入会立刻得到"输入结束"。

程序用 `user/lib` 的 `console.h` 读输入：`console_read` 读原始字节，`read_line` 在它之上做回显和退格、按行返回，行首的 Ctrl-D 表示输入结束。回显和行编辑在读的那个进程里做，驱动只多做一件事：给程序的输入一次最多给到行尾为止。这样程序读完它要的几行就退出时，后面已经敲进来的输入还在驱动里，前台清除时还给命令行。反过来，程序启动前就敲进来、命令行已经读走的输入，命令行在设置前台之前退回给驱动（`UART_UNREAD`）。所以一次粘贴 `write f`、几行内容、Ctrl-D、下一条命令，每一段都会到该到的地方。

`rc` 里的程序和提示符下运行的程序走同一条路，所以也可以用 Ctrl-C 终止。

程序参数的传递方式：用户栈区域最顶上的一页是参数页（地址固定，栈从它下面开始）。`exec` 把调用者给的参数块（`"arg0\0arg1\0..."`，最多约 4KB）写进新进程的参数页；启动代码 `crt0` 从这个固定地址取出来切成 `argv`，再调用 `main(argc, argv)`。这样不依赖任何架构的寄存器或栈帧约定。进程在内核里的名字取自 `argv[0]`。

所以加一个程序只需要：在 `user/` 下建目录写好 Makefile，把名字加进 `user/ramfs/Makefile` 的 `BOOT_PROGRAMS`。不用改 init，也不用改内核。

当前的限制：没有环境变量；命令行不支持引号、重定向和管道；程序读输入时的行编辑只有退格，没有方向键和历史；Ctrl-C 只终止前台程序本身，它 fork 出来的子进程不受影响；后台任务的输出会和提示符混在一起。

## 系统调用

共 27 个，编号在 `src/include/kernel/syscall.h`，用户态包装在 `user/lib`。

| 编号 | 调用 | 说明 |
|------|------|------|
| 0 | `exit(code)` | |
| 1 | `fork()` | 写时复制 |
| 2 | `exec(image, size, args, args_size)` | 用调用者内存里的 ELF 映像替换当前进程，并把参数块交给新程序；内核不认识路径 |
| 3 | `waitpid(pid, wstatus, options)` | 阻塞到子进程退出；支持 `WNOHANG` |
| 4 / 5 | `getpid()` / `getppid()` | |
| 6 | `yield()` | |
| 7 | `kill(pid, signal)` | 没有信号处理函数：非 0 信号终止目标；非特权进程只能发给自己和子孙。信号 0 只探测进程是否存在，谁都可以用 |
| 8 | `nanosleep(req, rem)` | |
| 9 | `brk(addr)` | |
| 10 / 11 | `mmap(...)` / `munmap(addr, len)` | 只支持匿名映射 |
| 12 | `console_write(buf, len)` | 写内核串口控制台（调试输出） |
| 13 | `ipc_send(dest, msg)` | 把消息发给 PID `dest`，阻塞到对方收下 |
| 14 | `ipc_recv(from, msg)` | 接收消息；`from` 为 `IPC_ANY`、指定 PID，或 `IPC_FROM_KERNEL`（只收内核发来的中断消息） |
| 15 | `ipc_call(dest, msg)` | 发送请求并等待 `dest` 的应答，应答写回 `msg` |
| 16 | `ipc_reply(dest, msg)` | 应答正在 `call` 自己的进程，从不阻塞 |
| 17 | `mem_grant(pid, addr, len)` | 把自己的一段内存共享给 `pid`，对方收到内核发来的授予通知 |
| 18 / 19 | `io_read(port, width, value*)` / `io_write(port, width, value)` | x86 的 I/O 端口，仅特权进程 |
| 20 | `map_device(phys, len)` | 把设备内存映射进自己的地址空间，仅特权进程 |
| 21 / 22 | `irq_claim(irq)` / `irq_ack(irq)` | 认领设备中断 / 处理完毕后重新打开，仅特权进程 |
| 23 | `drop_privilege()` | 放弃特权，不可恢复 |
| 24 | `dma_alloc(len, phys*)` | 物理连续的内存，返回虚拟地址并告知物理地址，仅特权进程 |
| 25 | `uptime_ms(ms*)` | 开机以来的毫秒数 |
| 26 | `timer_set(ms)` | 一次性定时器：到期时收到内核发来的 `IPC_LABEL_TIMER` 消息；0 取消 |

## 进程间通信

IPC 是模块之间、模块与客户进程之间唯一的通信方式（`src/kernel/ipc.cpp`）。

- **同步会合，没有缓冲**：`send` 阻塞到对方 `recv`，`recv` 阻塞到有人 `send`。消息只存放在阻塞一方的 PCB 里，内核里没有消息队列。
- **请求-应答**：`call` 发出请求后原子地转入“等这个服务的应答”；服务用 `reply` 应答，`reply` 只投递给正在等自己的进程，否则立刻返回 -1，从不阻塞。所以客户无法把服务卡住。
- **定长消息**：`struct ipc_msg { sender; label; data[6]; }`，共 56 字节，布局在三个架构上相同。`sender` 由内核填写，无法伪造；`label` 和 `data` 的含义由通信双方约定。
- **内核保留的 label**：最高位为 1 的 label 只有内核能发（设备中断 `IPC_LABEL_IRQ`、内存授予 `IPC_LABEL_GRANT`、定时器到期 `IPC_LABEL_TIMER`），用户进程发这样的消息会被拒绝，所以接收方可以相信它们的内容。
- **按 PID 寻址**：PID 不复用，向已退出的进程发送会失败。
- **退出与 kill**：进程退出时，正在向它发送或只等它消息的进程带着 -1 返回；阻塞在 IPC 上的进程可以被 `kill`。

服务进程的典型结构：

```cpp
struct ipc_msg m;
for (;;) {
    ipc_recv(IPC_ANY, &m);        // 等请求
    /* 按 m.label 处理，结果写回 m */
    ipc_reply(m.sender, &m);      // 应答（不会阻塞）
}
```

客户进程用 `ipc_call(server_pid, &m)` 一次完成请求和应答。`user/init/init.cpp` 里有一个完整的例子。

**超时。** 服务进程在 `recv` 上等请求的同时如果还要处理超时，用 `timer_set(ms)`：到期时内核发来一条 `IPC_LABEL_TIMER` 消息，和设备中断一样排在普通消息前面。每个进程一个一次性定时器，需要周期性的就在收到后重新设置。`uptime_ms()` 读开机以来的时间。

消息只有 48 字节的载荷，大块数据用共享内存传递（见下）。

## 共享内存

`mem_grant(pid, addr, len)` 把调用者自己的一段内存（页对齐、已映射、可写，例如 `mmap` 得到的）同时映射进进程 `pid`。映射在对方地址空间里的位置由内核告诉对方：对方收到一条 `label == IPC_LABEL_GRANT` 的消息，`sender` 是授予者，`data[0]` 是地址，`data[1]` 是长度；`mem_grant` 阻塞到对方收下这条消息为止。之后两个进程读写的是同一批物理页。

地址由内核而不是授予者来告知，是因为接收方无法验证授予者自己报的地址——服务进程如果相信了一个谎报的地址，就会读写自己的任意内存。

- 只能共享自己的内存，所以不需要额外的权限检查；对方是被动接受的一方。
- 共享页带有“共享”标记：之后任何一方 `fork`，子进程也继续共享这些页，而不是得到写时复制的副本。
- 任何一方 `munmap` 或退出只是撤掉自己的映射，物理页在最后一个映射消失时才释放。

当前的限制：不能撤回已经给出去的共享；对方是被动接受的（不想要只能自己 `munmap`）；单次最多 16MB。

## 名字服务

客户进程按名字找到服务的 PID。服务端在 init 里（`user/init/init.cpp`），所以地址固定是 PID 1；协议和客户端接口在 `user/lib`（`names.h`）：

- `name_register(name)`：把名字登记到调用者名下。登记的 PID 取自内核填写的 `sender`，不能替别人登记；名字已被占用时失败。
- `name_lookup(name)`：返回 PID，没有登记返回 0。
- `name_wait(name)`：轮询到名字出现为止，用于启动顺序不确定的场合。

名字最长 47 个字符，最多登记 16 个。init 每次用到一条登记时用 `kill(pid, 0)` 确认登记者还在，已退出进程的名字随即失效。

当前的限制：任何进程都可以登记任何还没被占用的名字，没有访问控制。

## 文件服务

文件服务的协议和客户端接口在 `user/lib`（`fs.h`）。有两个实现同一套协议的服务，客户端库按文件名的前缀选择：

- 没有前缀 → `user/ramfs`（登记为 `"fs"`）：内容放在服务自己 `mmap` 来的内存里，开机时从启动映像装载，之后的改动重启即丢失。
- `disk:` 前缀 → `user/diskfs`（登记为 `"diskfs"`）：内容经块设备服务落在磁盘上，跨重启保留。

两者的命名空间都是平的（没有目录）。协议的服务端部分（客户的缓冲区、句柄、请求分发、回收）是公共的，在 `user/lib` 的 `fs_server` 里；一个文件系统只需要提供一组按文件号操作的函数（`struct fs_backend`）再调用 `fs_serve`。

- **连接**：客户第一次调用 `fs_*` 时 `mmap` 一块 4KB 缓冲区并 `mem_grant` 给服务；服务从授予通知里得知缓冲区的位置，按客户的 PID 记下来。
- **请求**：`FS_OPEN` / `FS_CLOSE` / `FS_READ` / `FS_WRITE` / `FS_SIZE` / `FS_UNLINK` / `FS_LIST`。消息里只有参数（句柄、偏移、长度），文件名和数据都在共享缓冲区里；读写带显式偏移，服务端不记文件位置。超过 4KB 的读写由客户端库拆成多个请求。
- **句柄**属于打开它的进程，别的进程用不了；`fork` 出来的子进程要自己重新连接（库会自动处理）。
- **回收**：服务用 `kill(pid, 0)` 探测客户是否还在，新客户连接时顺便清掉已退出客户的缓冲区和句柄。

当前的限制：没有目录、权限和时间戳；每个服务最多 16 个客户、64 个打开的句柄；已退出客户的资源要等下一个新客户连接时才回收。ramfs 最多 32 个文件、每个 4MB。

### diskfs 的磁盘格式

块大小 4096 字节：块 0 是超级块，之后是 FAT（每个块一个 32 位表项，记录文件的块链），再之后是 2 块目录（定长 128 字节表项，最多 64 个文件），其余是数据块。FAT 和目录在内存里各有一份完整副本，每次修改立刻把涉及的块写回磁盘，没有延迟写，所以不需要 `sync`。磁盘上没有有效的超级块时自动格式化。

当前的限制：没有缓存，每次读写都要走到驱动；没有日志，断电时正在进行的操作可能泄漏几个块（不会损坏其他文件）；最多管理 1GB。

## 块设备

块设备的协议和客户端接口在 `user/lib`（`blk.h`）：`blk_capacity`、`blk_read`、`blk_write`，以 512 字节扇区为单位，数据同样经客户与驱动之间的共享缓冲区传递。驱动以 `"blk"` 登记。

`user/blk` 是 virtio-blk 驱动。virtio 设备的公共部分（legacy 接口：找设备、寄存器访问、队列）在 `user/lib` 的 `virtio.h` 里，块设备和网卡驱动共用。两种接入方式只是寄存器的访问方法不同：

- **x86**：virtio-pci。通过 0xCF8/0xCFC 端口扫描 PCI 配置空间找到设备，寄存器在它的 I/O 端口 BAR 里，中断线从配置空间读出。
- **arm64**：virtio-mmio。QEMU virt 上有 32 个槽位，用 `map_device` 把它们映射进来逐个查看。

队列和请求缓冲区来自 `dma_alloc`（设备只认物理地址）。块设备驱动一次处理一个请求：提交给设备后用 `ipc_recv(IPC_FROM_KERNEL)` 只等中断，这期间其他客户的请求留在各自的 `call` 里排队。

`make run` 给 QEMU 挂上 `disk.img`（第一次运行时创建，16MB，`make clean` 不删，三个架构共用）；`make test` 每次用一块新的 2MB 临时磁盘，小到自检可以把它写满来检查“磁盘满”的处理（大于 4MB 的磁盘上这一项会跳过）。`make test` 的环境里磁盘、网卡、回显服务都在，所以自检里出现任何 skipped 也算失败。

当前的限制：只支持 legacy 接口（QEMU 的默认配置）；一次一个请求，没有并发；每个请求最多 8 个扇区。

## 特权与硬件访问

用户态驱动需要碰硬件，内核为此提供下面几样东西，都只对带 `privileged` 标志的进程开放：

- **特权的来源**：内核直接创建的 init 有特权，`fork` 和 `exec` 都保留；进程调用 `drop_privilege()` 之后永久失去。init 启动驱动时保留特权，启动其他模块时先让子进程放弃。
- **I/O 端口**（仅 x86）：`io_read` / `io_write`，宽度 1/2/4 字节。
- **设备内存**：`map_device(phys, len)` 把设备的寄存器或显存映射进调用者的地址空间（不缓存）。只接受设备地址区：arm64 上是 QEMU virt 的 1GB 以下，x86 上是 640K–1M 的传统空洞和物理内存之上的地址；普通内存一律拒绝。映射同样带“共享”标记，`fork` 后父子都能访问设备。
- **DMA 内存**：`dma_alloc(len, phys*)` 分配物理上连续、已清零的内存并映射进调用者，同时告知物理地址，供驱动把缓冲区交给设备。
- **设备中断**：`irq_claim(irq)` 认领一条内核自己没在用的中断线（x86 是 PIC 的 IRQ 号，arm64 是 GIC 的 SPI 中断号）。中断到来时内核屏蔽这条线，并向属主投递一条 `sender == IPC_KERNEL`、`label == IPC_LABEL_IRQ`、`data[0] == irq` 的消息；属主没在 `recv` 时记为待处理，下一次 `recv(IPC_ANY)` 先收到它。驱动处理完设备后调用 `irq_ack(irq)` 重新打开中断线。进程退出时它的认领被释放。
  一条线可以被多个进程认领（x86 上磁盘和网卡就共用 IRQ 11）：中断到来时每个属主都收到消息，驱动要自己看设备状态、没有事就直接 `irq_ack`；所有属主都应答之后内核才重新打开这条线。

实现在 `src/kernel/user_irq.cpp`、`src/kernel/syscall.cpp` 和 `src/kernel/syscalls/mm.cpp`。

当前的限制：特权是全有或全无的，没有按设备授权；x86 的端口访问每次都是一次系统调用。

## 第一个用户态驱动：uart

`user/uart` 是串口输入驱动：x86 的 16550 通过 I/O 端口访问，arm64 的 PL011 用 `map_device` 把寄存器映射进来直接读写。内核只用串口做输出，接收方向完全在驱动里：

1. 启动时 `irq_claim` 串口中断，打开设备的接收中断。
2. 主循环 `ipc_recv(IPC_ANY)`：收到内核的中断消息就把硬件 FIFO 读进自己的缓冲区并 `irq_ack`；收到 `UART_READ` 请求就记下读者。
3. 缓冲区里有数据且有读者在等时，把数据作为应答发回去。读者有两个（终端的主人和前台进程），各有各的缓冲区，新到的字节按当时有没有前台进程分到其中一个。读请求可以带超时：到时间还没有输入就应答 0 字节（驱动用 `timer_set` 给自己定时）。

协议定义在 `user/lib/include/console.h`，同一个头文件里是程序读输入用的 `console_read` / `read_line`。驱动以 `"uart"` 这个名字登记。

## 用户态的约束

内核切换任务时不保存浮点/SIMD 寄存器，所以用户程序用 `-mno-sse`（x86_64）/ `-mgeneral-regs-only`（arm64）编译，不能使用浮点数。

## 怎么加模块

普通程序放进启动映像即可（见上）。下面是加一个开机就要常驻的服务或驱动：

1. 在 `user/` 下新建目录，写一个三行的 Makefile（`TARGET`、`SOURCES`、`include ../program.mk`），链接 `user/lib`。
2. 定义它的 IPC 协议（请求的 `label` 和 `data` 布局；要传大块数据就像文件服务那样用共享缓冲区），主循环按上面服务进程的结构写，启动后用 `name_register` 登记自己的名字。
3. 把它加进 `user/init/Makefile` 的 `MODULE_NAMES` 和 `user/init/modules.S`，在 init 的 `main` 里用 `start_module` 启动；最后一个参数决定它是否保留特权（只有驱动需要）。
4. 客户用 `name_wait("名字")` 拿到 PID，再用 `ipc_call` 发请求。
