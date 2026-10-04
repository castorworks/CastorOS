# CastorOS 缺陷与设计问题分析报告

- 审计对象：`main` @ `50dc9eb`（内核约 8.3 万行，测试 2.2 万行，用户态 7.5 千行；i686 / x86_64 / arm64）
- 日期：2026-10-04
- 附录（未复核的 medium/low 发现）：[bug-analysis-appendix.md](bug-analysis-appendix.md)

## 0. 修复状态（2026-10-04 更新）

本报告第 1、3 节描述的是审计时（`50dc9eb`）的状态。之后在分支 `fix/audit-critical` 上做了修复，
下面是截至该分支提交 `480c8c1` 的情况。

| | 数量 |
|---|---|
| 已复核的问题 | 124 |
| 已修复 | 115 |
| 未修复 | 9 |
| 未复核的 medium/low（见附录） | 446，其中 1 条已修复，其余未处理 |

修复后的回归结果：内核测试 i686 669/669、x86_64 650/650，arm64 无失败；三个架构都能从用户 shell 运行程序；
x86_64 不再随 exec 泄漏内存；i686 和 x86_64 开机自动通过 DHCP 获得地址，ping 正常。

第 5 节每个条目的标题里标了"已修复"或"未修复"。由后期批量修复处理的条目附有修复说明和验证方式；
其余"已修复"条目是按第 4 节顺序在前七项里修掉的，说明见对应提交。

未修复的 9 项归为四类：

1. **arm64 内核仍运行在低地址恒等映射里**（3 项：V009、V010、V124）。arm64 上 `mmap` 因此不可用。需要把内核重新链接到高半区。
2. **socket 没有接入文件描述符表**（2 项：V092、V093）。`close(sockfd)` 走不到 socket。
3. **内核抢占**（1 项：V052）。用户态死循环仍会冻结系统。
4. 其余 3 项：没有权限模型（V054）；arm64 的 virtio-gpu 设备类型（V115）；`Scheduler::free` 中页目录释放顺序（V038）。

### 端到端验证

在修复之后补做的实测（QEMU）：

| 项目 | 结果 |
|---|---|
| DHCP 对真实服务器 | i686、x86_64 开机都从 QEMU 的 DHCP 服务器获得 10.0.2.15 |
| TCP 对真实对端 | 用户态程序连宿主机上的测试服务器，发 6000 字节、收 12000 字节，逐字节一致，连接正常关闭；两个 x86 架构各两次 |
| x86_64 网卡 | 已启用，ping 网关 3/3 |
| USB | i686、x86_64 开机时挂载的 USB 存储设备能枚举、读出容量并注册为块设备 |
| `mmap`/`munmap`/`brk` | 用户态测试新增"映射互不重叠"和"fork 后写时复制"两项检查，i686、x86_64 通过 |

这轮验证中发现并修复了三个不在已复核清单里的问题：共享中断线导致有网卡和 USB 控制器时启动挂死
（附录中的 `x-concurrency-10`）；内核与用户库的 `ifreq` 硬件地址布局差 2 字节，`ifconfig` 显示错误的 MAC
（第 3 节实测记录过）；用户库的 `usleep` 只有声明没有实现。

### 验证上仍有的限制

- 约 30 项修复只有启动回归和代码审查，没有专门的测试。
- kprintf 模块的 63 个测试断言数仍为 0（只打印、不检查输出）。
- TCP 只实测了内核作为客户端；监听和 accept 只有测试用假网卡上的单元测试。
- USB 只验证了开机时已插入的设备，运行中热插拔没有实测。
- arm64 没有网络和 USB，`mmap` 不可用，上述实测都不适用。
- TCP 实测用的程序和宿主机脚本没有纳入仓库。

## 1. 结论

1. **现有测试全部通过，但没有覆盖任何一个严重问题。** 内核单元测试 i686 624/624、x86_64 604/604、arm64 468/468，x86 用户态测试也全过；每个架构还有一个 63 用例、断言数为 0 的套件。
2. **只有 i686 能算"可用"。** arm64 上从用户 shell 执行任何程序都会让整机停机；x86_64 每次 fork/exec/exit 泄漏约 1 MB，且没有网络和 USB。
3. **用户态与内核之间没有信任边界。** 系统调用层不校验任何用户指针和长度，用户程序可以读写任意内核内存；用户态触发的任何 CPU 异常在三个架构上都会停掉整个内核，而不是只杀掉出错进程。
4. **内核没有成文的并发模型。** 中断上下文里拿互斥锁、持自旋锁再申请同一把锁、不加保护地与线程上下文共享数据，这些在调度器、网络栈、驱动里反复出现。
5. **对象生命周期普遍没有引用计数。** ramfs、shmfs、pipe、FAT32 的节点在仍被打开时就被释放。
6. **网络栈对不可信输入几乎不设防。** 一个畸形 IP 分片或一个发往监听端口的 SYN 就能让内核堆溢出或永久死锁。

## 2. 审计方法与可信度

| 阶段 | 做法 | 结果 |
|---|---|---|
| 实测基线 | 三架构全量构建、QEMU 跑内核与用户态测试、在 shell 里做功能探测 | 见第 3 节 |
| 静态发现 | 30 个子系统审计 + 9 个横切视角 + 5 个补漏任务，逐文件通读 | 645 条原始发现 |
| 对抗式复核 | critical/high 共 199 条，由独立复核者对照源码逐条尝试证伪并重新定级 | 183 确认、15 待确认、1 证伪 |
| 合并 | 同一根因的重复报告合并 | 124 个独立问题（第 5 节） |

需要知道的限制：

- **medium/low 的 446 条没有复核**，只在附录里列出。按 critical/high 的复核通过率推断其中大部分属实，但逐条可信度低于正文。
- 复核是单人复核，且只是静态读码；正文里标"已确认"的条目中，只有第 3 节列出的几项有运行时证据。
- 重复合并只在同一复核批次内进行，正文里仍有少量跨批次的同根因条目（例如 V001/V003、V024/V026、V040/V041、V055/V056/V057）。
- 完整性评审提出的 12 个补漏方向只跑了 5 个。没有跑的 7 个是：阻塞/唤醒与中断状态、arm64 用户进程全流程、汇编与 C++ 调用点契约、kernel shell 命令面、从 FAT32 执行程序的端到端流程、内核缺页与栈安全、定时器与睡眠。
- 用户态→内核边界的问题没有做运行时验证，结论来自读码。

## 3. 实测证据

| 现象 | 架构 | 说明 |
|---|---|---|
| 执行程序导致整机停机 | arm64 | `exec /bin/hello.elf`：fork 出的子进程写 COW 栈页时收到 Access flag fault (level 3) 被 SIGSEGV；随后内核在 `src/kernel/task.cpp:1028` 解引用空 `page_dir`，EL1 Data Abort，System halted。对应 V004、V042 |
| 每次 exec 泄漏 266 页 | x86_64 | `free` 显示已用页 9975 → 10241 → 11039（1 次、4 次之后）；退出时没有 `free_page_directory` 日志。约 85 次后耗尽 128 MB。i686 同样操作无泄漏 |
| 没有网络和 USB | x86_64 | 启动日志 `E1000 driver skipped (x86_64 VMM MMIO not ready)`，USB 同样被跳过 |
| 没有 procfs 和网络栈 | arm64 | `/proc/meminfo` 不存在；`build/arm64` 下没有 `net/` |
| `ifconfig` 显示错误的 MAC | i686 | 用户态显示 `00:12:34:56:00:00`，内核实际为 `52:54:00:12:34:56`（错位 2 字节）；ping 之后 RX/TX 计数仍为 0 |
| 用户 shell 的网络命令是桩 | i686 | `dhcp`、`nslookup`、`arp` 只提示去用 kernel shell；开机后 eth0 为 0.0.0.0。手工配置 IP 后 ping 网关 0% 丢包 |
| shell 空闲时持续占用 CPU | i686 | `ps` 中 shell 的运行时间约等于墙钟时间，键盘读取是忙等 |
| 空闲时内核堆占用 96%~97% | x86 | i686 上 4436 KB 中已用 4304 KB |
| 退出时 PMM 告警 | i686 | 每次进程退出都有 `PMM: Attempted to unprotect unknown frame` |

编译警告中的真实缺陷：`src/kernel/syscalls/mm.cpp:453` 的 `munmap` 把地址存入 `uint32_t`，arm64 上随后的范围检查被编译器判为恒假；`src/drivers/x86/e1000.cpp:579` 把 MMIO 虚拟地址存入 `uint32_t`；arm64 上 `HUGE_PAGE_SIZE`、`PHYS_ADDR_MAX`、`VIRT_ADDR_MAX` 各有两处取值不同的定义；arm64 内核镜像有 RWX 的 LOAD 段。

## 4. 系统性设计问题与修复顺序

按"先修哪个能消掉最多条目"排序：

1. **建立用户指针校验层**（V055–V057、V083、V039、V030）。加 `copy_from_user` / `copy_to_user` / `strncpy_from_user`，所有系统调用只通过它们碰用户内存；内核态访问用户地址出错时返回 `EFAULT`。
2. **用户态异常只杀进程**（V001–V003、V015、V024–V026）。三个架构的异常入口先判断来源特权级，来自用户态的一律终止当前进程。
3. **修好 arm64 的 fork 路径**（V004、V006、V040–V042、V005、V027）。`protect` 不能清掉 AF/AttrIndx/SH；内核栈指针不能经用户寄存器 x28 传递；删掉调度器里对 PID 1 的调试解引用；内核恒等映射不能出现在用户地址范围里。
4. **x86_64 的 SYSCALL 返回路径与地址空间释放**（V016，以及 x86_64 章节中的泄漏条目）。
5. **给 VFS 节点加引用计数**（V066、V069–V071）。unlink 只摘目录项，最后一次 close 才释放。
6. **所有 `offset + size` 改成防回绕写法**（V067、V068、V079–V082、V028）。
7. **定义并发模型**：哪些锁可以在中断里拿、哪些路径可以睡眠，然后按规则逐个子系统改（V084 及"进程、调度与同步""设备驱动"章节）。
8. **修堆分配器**（V029 `split()` 不更新 `last_block`；V058 `normalize_path` 栈帧大于内核栈）。
9. **补测试**：上面每一类至少配一个会失败的用例，并让零断言套件报错。

## 5. 已复核的问题（124 项）

每项标题格式为：编号 [级别·复核结论·受影响架构]。级别为复核者重新评定后的结果。

| 章节 | 数量 |
|---|---|
| 网络协议栈 | 28 |
| 进程、调度与同步 | 16 |
| 设备驱动 | 15 |
| 架构层：arm64 | 14 |
| 文件系统 | 12 |
| 内存管理 | 12 |
| 系统调用与内核初始化 | 11 |
| 架构层：x86_64 | 9 |
| 架构层：i686 | 3 |
| 用户态 | 3 |
| 构建系统与文档 | 1 |

级别分布：critical 36、high 59（其中 5 项待确认）、medium 28（7 项待确认）、low 1（待确认）。

### 架构层：arm64（14 项）

#### V001 [critical·已确认·arm64·已修复] 内核态 (EL1) 访问用户地址出错时直接停机：系统调用传入坏指针即可挂死整机

- 位置：`src/arch/arm64/interrupt/exception.cpp:373`；相关：`src/arch/arm64/interrupt/exception.cpp:300`、`src/arch/arm64/interrupt/exception.cpp:343`、`src/kernel/syscall.cpp:114`
- 证据：DABT 分支：COW 处理失败后只有 `if (!is_user && far >= KERNEL_VIRTUAL_BASE)` 才尝试 handle_kernel_page_fault；对 EC=DABT_CUR 且 far 在用户地址范围的情况，`if (is_user) {terminate}` 不成立，break 后落到 `System halted` 的 `while(1) wfi`。系统调用层没有 copy_from_user/指针校验，例如 sys_execve_wrapper 直接 `path[i] = user_path[i]`（syscall.cpp:114）。IABT 分支同理。
- 触发场景：用户程序调用 exec/open/write 等并传入未映射的用户地址（如 (char*)1）：内核在 EL1 读该地址产生 translation fault → EC=0x25、far<KERNEL_VIRTUAL_BASE、非 COW → 打印后永久 wfi，整机停止。对只读用户页的内核写（read() 到代码段）也走同一路径。
- 修复方向：为内核访问用户内存提供带 fixup 的 copy_from_user/copy_to_user（或至少在 DABT_CUR 且 far 位于用户空间、当前任务是用户进程时终止该进程/让系统调用返回 -EFAULT），并在系统调用入口校验指针范围与映射。
- 复核意见：exception.cpp:314 is_user 仅在 EC=DABT_LOW 时为真；343 行只对 far>=KERNEL_VIRTUAL_BASE 的内核地址尝试 handle_kernel_page_fault；373 行只有 is_user 才终止进程，否则 break 后落入 400 行之后的 `System halted` wfi 死循环。syscall.cpp:114 sys_execve_wrapper 直接 `path[i]=user_path[i]`，仅判空，该文件在 Makefile arm64 源列表中(约 148 行)，src/kernel 中没有任何用户指针校验函数。实测中内核在 task.cpp:1028 发生 data abort 后停机也印证了 EL1 abort 即整机停止的路径。 更正：IABT 分支在 exception.cpp:265-305 同理。修复时还需注意内核本身位于 TTBR0 低半区(见 build-system-2)，仅按“far 在用户范围”判断不足以区分真正的内核 bug，建议用 fixup 表或 copy_from_user 标记。

#### V002 [critical·已确认·arm64·已修复] 来自 EL0 的未处理同步异常（未定义指令、BRK、SP/PC 对齐错误等）会停掉整个系统而不是只终止该进程

- 位置：`src/arch/arm64/interrupt/exception.cpp:396`；相关：`src/arch/arm64/interrupt/exception.cpp:381`、`src/arch/arm64/interrupt/exception.cpp:385`、`src/arch/arm64/interrupt/exception.cpp:389`、`src/arch/arm64/interrupt/exception.cpp:404`
- 证据：handle_sync_exception 的 switch 只有 IABT_LOW/DABT_LOW 分支调用 arm64_terminate_user_process；`case ESR_EC_PC_ALIGN / ESR_EC_SP_ALIGN / ESR_EC_BRK64 / default:` 都只是打印后 break，随后无条件执行 `dump_registers(regs); ... while (1) { wfi }`，完全不看 source 是否为 EXCEPTION_FROM_EL0_64。
- 触发场景：任意用户程序执行到 `__builtin_trap()`（GCC 生成 brk #1000）、未定义指令（EC=0x00，例如在 EL0 访问 EL1 系统寄存器）、或把 SP 设成非 16 字节对齐后访问栈（SA0=1 → EC=0x26），内核打印 SYNCHRONOUS EXCEPTION 后进入死循环，所有进程和 shell 全部停止。普通的用户态程序 bug 即可让整机挂死。
- 修复方向：在 switch 之后（或 default 分支）判断 source==EXCEPTION_FROM_EL0_64：对 EL0 来源一律调用 arm64_terminate_user_process(regs, SIGILL/SIGTRAP/SIGBUS, far)，只有 EL1 来源才 dump+halt（最好改为 panic）。
- 复核意见：exception.cpp:381-398 的 ESR_EC_PC_ALIGN/SP_ALIGN/BRK64/default 分支只打印后 break，随后 400 行起无条件 dump_registers 并 `while(1) wfi`，完全不检查 source 是否 EXCEPTION_FROM_EL0_64；arm64_terminate_user_process 全文件只在 302 和 375 行(IABT_LOW/DABT_LOW)被调用。在 switch 之前只有 SVC64 被提前处理(约 214 行)，没有其它 EL0 兜底，因此用户态一条 brk 或未定义指令即可让整机停止。 更正：SP 对齐场景要注意 start.S 中清除了 SCTLR_A，SA0 是否置位未核实，该子场景仅为可能；BRK 与未定义指令(EC=0x00 走 default)路径是确定的。

#### V003 [critical·已确认·arm64·已修复] EL1 在访问用户地址时发生的未处理 data abort 直接让整机停机（没有按进程失败处理）

- 位置：`src/arch/arm64/interrupt/exception.cpp:406`；相关：`src/arch/arm64/interrupt/exception.cpp:343`、`src/mm/vmm.cpp:413`
- 证据：handle_sync_exception 对 ESR_EC_DABT_CUR 只尝试 COW（权限错误+写）和 handle_kernel_page_fault（arm64 上恒返回 false），之后 `if (is_user) { arm64_terminate_user_process(...) }` 只覆盖 EL0 来源；EL1 来源走到 `serial_puts("\nSystem halted.\n"); while (1) { wfi }`。系统调用层没有 copy_from_user/access_ok 之类的用户指针校验（全仓库 grep 无结果），内核直接解引用用户传入的指针。
- 触发场景：用户程序把一个未映射或只读且非 COW 的地址作为缓冲区传给系统调用（例如 read(fd, 无效指针, n) 这类普通程序 bug）：内核在 EL1 写该地址产生 translation/permission fault，EC=0x25，不是 COW，far < KERNEL_VIRTUAL_BASE，于是打印寄存器后进入死循环，所有进程一起停止。翻译错误、访问标志错误、对齐错误等同样如此。
- 修复方向：对 EL1 来源但 FAR 属于用户地址范围的 abort，应终止当前进程（或通过异常修复表让系统调用返回 -EFAULT），而不是停机；同时在系统调用入口增加用户指针范围/权限校验。
- 复核意见：src/arch/arm64/interrupt/exception.cpp:310-406：data abort 只尝试 COW（仅权限错误）和 handle_kernel_page_fault（src/mm/vmm.cpp:413 在非 i686 上恒返回 false），之后只有 is_user（EC=DABT_LOW）才调用 arm64_terminate_user_process；EL1 来源 break 后落到 dump_registers + "System halted" + wfi 死循环。全仓库 grep copy_from_user/access_ok/copy_to_user/validate_user 无结果，系统调用直接解引用用户指针。QEMU 实测中内核在 task.cpp:1028 的 data abort 后整机停机也印证了这条路径。 更正：除了用户传入坏指针，EL1 对用户页的 Access flag fault（由 protect 的 bug 引起，见 arch-arm64-mm-1）也走同一条停机路径。修复时需区分 FAR 在用户范围与真正的内核 bug（后者仍应 panic）。

#### V004 [critical·已确认·arm64·已修复] hal::Mmu::protect 用整页描述符编码器处理 set/clear 增量，COW 恢复写权限时清掉 AF、AttrIndx、SH 并去掉 UXN/PXN

- **修复**：The arm64 part was already fixed by 0395890. This batch fixes the x86_64 twin named in the reviewer correction: src/arch/x86_64/mm/paging64.cpp protect() now applies a per-flag delta (pte64_apply_flag_delta) at all three levels instead of hal_flags_to_x64 masks, which cleared NX on every COW transition. Added an arm64 regression test that reads and writes the page after the COW protect transitions. 验证方式：unit tests test_x86_64_hal_mmu_protect_keeps_unrelated_flags (x86_64) and test_arm64_hal_mmu_protect_keeps_unrelated_flags (arm64), both pass
- 位置：`src/arch/arm64/mm/mmu.cpp:915`；相关：`src/arch/arm64/mm/mmu.cpp:864`、`src/arch/arm64/mm/mmu.cpp:890`、`src/arch/arm64/mm/mmu.cpp:463`、`src/mm/vmm.cpp:514`、`src/mm/vmm.cpp:523`、`src/arch/arm64/interrupt/exception.cpp:324`
- 证据：`uint64_t arm64_set = hal_flags_to_arm64(set_flags); uint64_t arm64_clear = hal_flags_to_arm64(clear_flags); ... current_flags |= arm64_set; current_flags &= ~arm64_clear;`。hal_flags_to_arm64() 不是“标志位映射”，而是完整描述符编码：无条件带 DESC_AF，未给 NOCACHE 时带 AttrIndx=3 和 SH_INNER，未给 EXEC 时带 UXN|PXN，未给 WRITE 时带 AP bit7。于是 COW 路径 protect(addr, HAL_PAGE_WRITE, HAL_PAGE_COW) 的 arm64_clear = AF | bit7 | AttrIndx(3) | SH(3) | UXN | PXN | COW，结果描述符变成 AF=0、AttrIndx=0(Device-nGnRnE)、SH=0、UXN=PXN=0(可写且可执行)。cow_flag_test 只 query 标志不访问页面，所以测试能通过。TCR_EL1 未开 HA，cortex-a72 也没有硬件 AF 管理，AF=0 必然产生 Access flag fault，而 exception.cpp 只处理 arm64_is_cow_fault()（权限错误），访问标志错误无人处理。
- 触发场景：fork 之后父子共享的可写页都是 RO+COW、refcount=2。先写的一方走复制路径（map 新帧，正常）。后写的一方进入 mm::Vmm::handle_cow_page_fault 的 refcount==1 分支（src/mm/vmm.cpp:523）调用 hal::Mmu::protect，页表项被改成 AF=0。返回后重试写入触发 Access flag fault (DFSC 0x0B)：若在 EL0，进程被 arm64_terminate_user_process 以 SIGSEGV 杀死（例如 shell 在第一次 fork 后写自己的栈就会死）；若是内核在系统调用中写该用户页（如 read() 写入用户缓冲区），handle_sync_exception 走到 “System halted” 死循环，整机停机。即使不发生 fault，该页也被改成 Device 内存属性且可执行。
- 修复方向：protect 不要复用 hal_flags_to_arm64。按标志逐项处理：WRITE 只改 AP[2](bit7)，USER 只改 AP[1](bit6)+nG，EXEC 只改 UXN/PXN，COW 只改 bit56，NOCACHE 只改 AttrIndx/SH；绝不触碰 AF。或者把描述符解码为 HAL 标志、做 (flags & ~clear) | set 后整体重新编码。三处（1GB 块、2MB 块、L3 页）都要改，并补一个 protect 后实际读写该页的测试；异常处理中对 access flag fault 至少应置 AF 后重试。
- 复核意见：src/arch/arm64/mm/mmu.cpp:913-924（以及 864、890 的块分支）用 hal_flags_to_arm64 生成 set/clear 掩码，而该函数（mmu.cpp:463-508）无条件带 DESC_AF，并默认带 AttrIndx=3、SH_INNER、UXN|PXN，无 WRITE 时带 AP bit7。vmm.cpp:514/523 的 protect(WRITE, COW) 因此把 AF、AttrIndx、SH、UXN/PXN 全部清掉，PTE 变成 AF=0。exception.cpp:324 只把权限错误当 COW 处理，Access flag fault 无人处理。这与 QEMU 实测完全吻合：arm64 上 fork 出的子进程在 COW 栈写入时以 "Access flag fault, level 3" 死亡。 更正：实测中死掉的是 fork 出的子进程（父进程先写并走复制路径，子进程后写时 refcount==1 走 protect 路径），而不一定是 shell 本身。x86_64 的 paging64.cpp:822-832 有同样写法，会清掉 NX，应一并修复。
- 被 3 个独立审计者重复报告（arch-arm64-mm-1, mm-vmm-5, x-hal-parity-2）

#### V005 [critical·已确认·arm64·已修复] 内核恒等映射块被放进每个用户地址空间的用户 VA 范围，query 对其返回成功，通用 munmap 因此会把内核/设备页帧释放回 PMM

- 位置：`src/arch/arm64/mm/mmu.cpp:1082`；相关：`src/arch/arm64/mm/mmu.cpp:605`、`src/arch/arm64/mm/mmu.cpp:628`、`src/arch/arm64/mm/mmu.cpp:1074`、`src/mm/vmm.cpp:1242`、`src/mm/vmm.cpp:1247`、`src/kernel/syscalls/mm.cpp:469`
- 证据：create_space(): `new_l1[1] = current_l1[1]; new_l1[2] = current_l1[2]; new_l1[3] = current_l1[3];` 以及 `new_l2[64] = 0x08000000ULL | dev_block_flags | DESC_TYPE_BLOCK; new_l2[72] = ...`。内核链接在物理地址 0x40100000 并通过 TTBR0 恒等映射运行，所以用户 L0[0] 下 VA 0x40000000–0xFFFFFFFF 和两个 2MB 设备窗口都是仅内核可访问的块映射，但它们位于 USER_SPACE_END(0x0000800000000000) 之内。hal::Mmu::query() 对块映射返回 true（mmu.cpp:605 `*phys = desc_get_addr(l1e) | (virt & 0x3FFFFFFF)`），不区分是否用户映射。mm::Vmm::unmap_page_in_directory (src/mm/vmm.cpp:1242-1254) 在 query 成功后忽略 hal::Mmu::unmap 的失败返回值（对块返回 PADDR_INVALID）并把 old_phys 返回；syscall::Mm::munmap (src/kernel/syscalls/mm.cpp:469-471) 只检查 addr < USER_SPACE_END 就 `mm::Pmm::free_frame(phys)`。PMM 初始化把内核和保留帧标记为已用且 refcount=1，arm64 上没有任何 protect_frame 保护。
- 触发场景：用户进程对落在 0x40000000–0x5FFFFFFF 内的任意页对齐地址调用 munmap(addr, 4096)（恒等映射，phys==addr）：query 命中 1GB 块返回成功，unmap 失败被忽略，free_frame 把该帧 refcount 1→0 并清位图。该帧可能是内核代码/数据、页表、内核堆或其他进程的页；之后 alloc_frame 会把它重新分配并清零，造成内核内存损坏或崩溃。对设备窗口地址（如 0x09000000）做同样的调用会把一个 RAM 范围之外的帧标成空闲，此后 alloc_frame 每次都先找到它并因 arm64 的 RAM 范围检查返回 PADDR_INVALID，全系统内存分配持续失败。普通用户程序的一次错误 munmap 即可触发。
- 修复方向：根本修复是让内核真正运行在 TTBR1 高半区，用户 L0 表中不再包含任何内核/设备映射。在此之前：hal::Mmu::query/unmap 提供“仅用户映射”语义或由 unmap_page_in_directory 检查 HAL_PAGE_USER 并以 hal::Mmu::unmap 的返回值为准（失败则返回 0）；munmap/brk 只释放确属该进程的用户页；对内核映像和保留帧在 PMM 中加保护。
- 复核意见：mmu.cpp:1082-1084 把 1GB 内核块拷入每个用户 L1；hal::Mmu::query (mmu.cpp:605-613) 对块映射返回 true 且 phys=恒等地址；mm::Vmm::unmap_page_in_directory (src/mm/vmm.cpp:1242-1254) 忽略 hal::Mmu::unmap 对块返回的 PADDR_INVALID (mmu.cpp:782/799) 并返回 old_phys；syscall::Mm::munmap (src/kernel/syscalls/mm.cpp:459-471) 只检查 < USER_SPACE_END(0x0000800000000000, task.h:32) 就 free_frame。mm::Pmm::free_frame (pmm.cpp:921-973) 只挡 protected 帧，而 protect_frame 只在 vmm.cpp:121 被调用（非内核映像），内核帧 refcount=1 (pmm.cpp:603)，所以会被清位图归还。SYS_MUNMAP 在 syscall.cpp:570 对所有架构注册，用户态一次系统调用即可触发。 更正：即使 refcount 为 0，free_frame (pmm.cpp:956-958) 也只打印警告后继续 clear_frame，所以不依赖 refcount==1。设备窗口场景（0x09000000）的后续 alloc 行为见 pmm.cpp:669-673，未逐行验证其“持续失败”的说法。

#### V006 [critical·已确认·arm64·已修复] fork 子进程的内核栈指针 (SP_EL1) 取自父进程用户态 x28，导致父子共用内核栈/内核栈指针由用户寄存器决定

- 位置：`src/arch/arm64/task/context_asm.S:331`；相关：`src/kernel/syscalls/process.cpp:246`、`src/kernel/task.cpp:680`、`src/arch/arm64/task/context_asm.S:413`、`src/arch/arm64/task/context.cpp:141`
- 证据：`.restore_user` 用上下文里的 X28 作为内核栈：`ldr x2, [x1, #CTX_X28]; cbz x2, .skip_sp_el1_setup; mov sp, x2`，之后又 `ldp x28, x29, [x1, #CTX_X28]` 把同一个值原样恢复到用户态 x28。create_user_process 里 `task->context.x[28] = task->kernel_stack`（task.cpp:680），所以首个 shell 在 EL0 的 x28 就是它自己的内核栈顶。而 fork 中 `for (int i = 1; i < 31; i++) child->context.x[i] = frame[i];`（process.cpp:246-248）把父进程用户态 x28 原样拷给子进程，没有改成 child->kernel_stack；hal_context_set_kernel_stack_ctx() 全仓库无人调用。exec 之后 frame[0..30] 被清零，x28==0 时走 `.skip_sp_el1_setup` 使用全局引导栈 stack_top。
- 触发场景：shell(PID1) 执行外部命令：fork() 后在 waitpid 里 yield（此时父进程内核栈上保存着异常帧和调用链）。子进程首次被调度时 SP_EL1 被设成父进程的内核栈顶，`.restore_user` 里的 `stp x0, x1, [sp, #-16]!` 和 serial_puts 调用立刻覆盖父进程保存的 ELR/SPSR/SP_EL0 等；子进程随后的每个 SVC 都把 272 字节异常帧和 C 调用链压在父进程的栈上。子进程退出后父进程从 schedule() 返回时栈帧已被覆盖，内核跳到垃圾地址崩溃或带着子进程的寄存器返回用户态。exec 过的进程再 fork，所有子进程共用引导栈 stack_top；此外 x28 是用户可写寄存器，内核栈位置等于由用户态决定，属于内核内存破坏类缺陷。子进程自己 kmalloc 的内核栈从未使用。
- 修复方向：不要用通用寄存器槽传递内核栈：在 task_t/上下文中增加独立的 kernel_sp 字段（或在 `.restore_user` 直接用 task->kernel_stack），fork/exec/create_user_process 统一设置；`.restore_user` 恢复 x28 时使用真实的用户值；删除 stack_top 兜底分支（改为 panic）。
- 复核意见：context_asm.S:331-339 的 .restore_user 把 CTX_X28 装入 SP（为 0 时退回 stack_top），413 行又把同一值恢复成用户态 x28；只有 create_user_process 设置它 (task.cpp:680)。fork 的 ARM64 分支 (src/kernel/syscalls/process.cpp:244-248) memset 后把父进程帧的 x1..x30 原样拷给子进程，从不写入 child->kernel_stack；hal_context_set_kernel_stack_ctx (arm64/context.cpp:141) 全仓库无调用者，schedule() 在 ARM64 上也不设置内核栈 (task.cpp:1007-1016)。因此子进程的 SP_EL1 等于父进程用户态 x28（通常是父进程内核栈顶，或用户任意设置的值），与实测中 fork+exec 后内核随即崩溃的现象相符。 更正：“exec 后 x28 被清零走 stack_top”这一分支我未单独核对 exec 的寄存器清零代码，其余链路已逐行确认。
- 被 2 个独立审计者重复报告（arch-arm64-exc-1, x-asm-abi-1）

#### V007 [high·已确认·arm64·已修复] SVC 系统调用全程在 DAIF 全屏蔽下执行，系统调用期间时钟中断永不投递

- **修复**：src/arch/arm64/interrupt/exception.cpp: IRQs are unmasked (daifclr #2) around arm64_syscall_handler and masked again before kernel_exit, so timer ticks are delivered during system calls. The earlier waitpid workaround is left in place. 验证方式：boot regression only (arm64 boot, shell, exec /bin/hello.elf check prints 1)
- 位置：`src/arch/arm64/interrupt/vectors.S:342`；相关：`src/arch/arm64/interrupt/exception.cpp:227`、`src/arch/arm64/syscall/svc.S:122`、`src/kernel/syscalls/process.cpp:1017`、`src/fs/devfs.cpp:152`
- 证据：el0_64_sync_handler: `kernel_entry; ...; bl arm64_exception_handler; kernel_exit`，异常进入时硬件置位 PSTATE.DAIF，vectors.S/exception.cpp/svc.S 的 SVC 路径上没有任何 `msr daifclr`（x86_64 的 syscall 入口有 `sti`，syscall64_asm.asm:149）。kernel::Interrupts::disable() 在这种上下文里返回 false，restore(false) 不会开中断。
- 触发场景：shell fork 出子进程后在 waitpid 中 `while(true){...; Scheduler::yield();}` 轮询（process.cpp:1017）；子进程调用 nanosleep 进入 BLOCKED。就绪队列里只剩 shell，它在 EL1 屏蔽中断的状态下不停 yield 给自己，timer IRQ 一直 pending 但不被接收，Scheduler::timer_tick 不再运行，子进程永远不会被唤醒，系统活锁。同理 devconsole_read 在系统调用里忙等串口（devfs.cpp:152）期间，所有 tick、定时器回调和睡眠唤醒全部停摆。
- 修复方向：在 EL0 同步异常确认为 SVC 后、调用 syscall_dispatcher 之前 `msr daifclr, #2`（或恢复到 SPSR 中用户态的中断掩码），返回前再屏蔽；同时把 waitpid/console read 改为真正阻塞而非忙等。
- 复核意见：src/arch/arm64/interrupt/vectors.S:342 的 el0_64_sync_handler 为 kernel_entry→arm64_exception_handler→kernel_exit，全 arm64 目录 grep daifclr 只有 hal.cpp:138（hal::Interrupt::enable）和 context_asm.S:443（内核线程入口），SVC 路径没有开中断。src/include/kernel/interrupt.h:40-72 的 disable() 在 DAIF 已屏蔽时返回 false，restore(false) 不开中断；context_asm.S 的 .restore_kernel 也不恢复 DAIF。process.cpp:1017 的 waitpid 轮询 yield 时若其他任务都 BLOCKED（子进程 nanosleep），schedule()（task.cpp:986-998）把自己放回队列又取出，next==prev 不切换，timer IRQ 永不投递，睡眠任务永不唤醒。 更正：活锁的前提是就绪队列中没有其他可运行任务：若有内核线程/idle 被切入，它在自己栈上的 schedule() 尾部 restore(true) 会重新打开中断，所以并非所有系统调用期间都彻底无 tick，但 waitpid 自旋 + 子进程睡眠这一场景成立。devconsole_read 的忙等（devfs.cpp:152 起，Serial::getchar 轮询）期间同样无 tick。

#### V008 [high·待确认·arm64·已修复] hal::Mmu::switch_space 切换 TTBR0 时不做 TLB 失效，且从不使用 ASID（恒为 0），用户页却标记为 nG

- 位置：`src/arch/arm64/mm/mmu.cpp:291`；相关：`src/mm/vmm.cpp:1190`、`src/kernel/syscalls/process.cpp:496`、`src/kernel/user.cpp:55`、`src/arch/arm64/task/context_asm.S:210`
- 证据：`void hal::Mmu::switch_space(paddr_t space) { dsb_ish(); write_ttbr0_el1((uint64_t)space); isb(); /* TLB invalidation is typically done by the caller if needed */ }`。写入 TTBR0 的值只有表基址，ASID 字段恒为 0；hal_flags_to_arm64 给所有用户页加 DESC_NG。调用方 mm::Vmm::switch_page_directory (src/mm/vmm.cpp:1190) 及其调用者 execve (src/kernel/syscalls/process.cpp:496)、task_enter_usermode (src/kernel/user.cpp:55) 都没有随后刷新 TLB。只有汇编上下文切换路径 (context_asm.S:210) 自己做了 tlbi vmalle1is。
- 触发场景：进程执行 execve：新地址空间在相同 VA（代码 0x10000000、栈顶 0x00007FFFFF000000 附近）映射了新的物理帧，随后 switch_page_directory 只改 TTBR0。旧映像在这些 VA 上的 nG TLB 项（ASID 同为 0）仍然有效，返回用户态后新程序可能通过陈旧项读/写/执行旧进程的物理帧（这些帧随后被 free_page_directory 释放并可能分配给别人），表现为参数/栈内容错乱、执行旧代码，直到下一次上下文切换的全量刷新后内容又突然变化。未验证的假设：QEMU TCG 在 ASID 不变时写 TTBR0 不会自行清软件 TLB（按体系结构规范，硬件上这一定需要软件失效）。
- 修复方向：在 switch_space 写 TTBR0 之后执行 tlbi vmalle1is + dsb ish + isb（先切换后失效），或为每个地址空间分配 ASID 并写入 TTBR0[63:48]（同时在 TCR 中配置 AS/A1），ASID 回绕时再全量刷新。
- 复核意见：src/arch/arm64/mm/mmu.cpp:291-296 的 switch_space 只有 dsb/写 TTBR0/isb，没有 tlbi；hal_flags_to_arm64（mmu.cpp:504）给用户页加 nG，而 TTBR0 从不带 ASID。调用方 vmm.cpp:1180-1190、process.cpp:496（随后立即 free_page_directory(old)，destroy_space 中无 flush）、user.cpp:55 都不刷新 TLB，只有 context_asm.S:209-214 的切换路径做 tlbi vmalle1is。代码缺陷属实，但实际后果依赖 QEMU TCG 在 ASID 不变时不清软 TLB，且 arm64 上 exec 目前在更早处崩溃，无法由运行结果印证。 更正：execve 完成后返回用户态之前不一定经过上下文切换，所以陈旧项窗口确实存在；最小修复是在 switch_space 写 TTBR0 之后加 tlbi vmalle1is + dsb ish + isb。
- 被 2 个独立审计者重复报告（arch-arm64-mm-4, x-hal-parity-4）

#### V009 [high·已确认·arm64·未修复] 用户地址空间的 1GB–4GB 被内核 1GB 块占据，通用 mmap 区域 0x40000000–0x70000000 在 arm64 上永远无法映射

- **未修复**：mmap on arm64 still cannot work: MMAP_REGION_START/END (0x40000000-0x70000000) in src/kernel/syscalls/mm.cpp lie inside the kernel's 1GB identity block. The fix belongs outside this batch: either move the kernel to TTBR1 (see x-hal-parity-6) or move the arm64 mmap region above 4GB, which also needs the uint32_t address types in syscalls/mm.cpp widened.
- 位置：`src/arch/arm64/mm/mmu.cpp:720`；相关：`src/arch/arm64/mm/mmu.cpp:1082`、`src/arch/arm64/mm/mmu.cpp:734`、`src/kernel/syscalls/mm.cpp:22`、`src/kernel/syscalls/process.cpp:485`、`linker_arm64.ld:13`
- 证据：hal::Mmu::map(): `} else if (desc_is_block(l1[l1_idx])) { LOG_ERROR_MSG("hal::Mmu::map: cannot map 4KB page over 1GB block\n"); return false; }`。create_space 把 L1[1..3]（VA 0x40000000–0xFFFFFFFF）设为内核恒等 1GB 块，并在 L2[64]/L2[72] 放了 2MB 设备块。而 src/kernel/syscalls/mm.cpp:22-23 的 `MMAP_REGION_START 0x40000000 / MMAP_REGION_END 0x70000000` 对所有架构通用，正好整体落在 L1[1] 的内核块内；用户堆上限是 user_stack_base-8MB，也会越过 0x40000000。
- 触发场景：arm64 用户程序调用 mmap(NULL, len, ...)：find_free_vaddr 选出 0x40000000 起的地址，do_mmap_anonymous 调 map_page_in_directory → hal::Mmu::map 命中 1GB 块返回 false，mmap 返回 -1，依赖 mmap 的功能（用户态 malloc 的大块分配、文件映射、共享内存）在 arm64 上全部不可用。同理，brk 堆增长到 0x40000000 或任何映射落到 0x08000000–0x081FFFFF、0x09000000–0x091FFFFF 时也会失败。
- 修复方向：把内核移到 TTBR1（高半区链接与运行），用户 TTBR0 表只放用户映射；短期内可为 arm64 把 mmap 区域和堆上限移到 4GB 以上（例如 0x100000000 起），并在 ELF 加载/brk/mmap 中显式排除被内核块占用的 VA 区间。
- 复核意见：src/arch/arm64/mm/mmu.cpp:1082-1084 的 create_space 把 current_l1[1..3]（start.S:351-377 建立的 0x40000000 起 1GB 块）原样拷进每个用户地址空间；mmu.cpp:720-722 的 map() 遇到 L1 块直接报错返回 false。src/kernel/syscalls/mm.cpp:22-23 的 MMAP_REGION_START/END=0x40000000/0x70000000 无架构区分，find_free_vaddr（mm.cpp:165-185）只在该区间内选址，因此 arm64 上匿名/文件 mmap 必然失败。 更正：brk 部分影响较小：用户程序链接在 0x10000000，堆要增长约 768MB 才会碰到 0x40000000；0x08000000/0x09000000 的 2MB 设备块低于用户加载基址，正常程序不会映射到。主要后果是 mmap 在 arm64 上完全不可用。

#### V010 [high·已确认·arm64·未修复] ARM64 地址空间设计：内核和设备 MMIO 位于 TTBR0（用户半区）恒等映射中，hal::Mmu 的 HAL_ADDR_SPACE_CURRENT 只认 TTBR0

- **未修复**：Design-level: the arm64 kernel is linked and runs in the TTBR0 identity map. A correct fix relinks the kernel into the TTBR1 high half, reworks start.S, every identity-addressed MMIO driver and the hal::Mmu root-table selection. Too large and risky for this batch. The directly exploitable consequences are already mitigated (munmap/brk free only user pages; device blocks are now UXN/PXN).
- 位置：`src/arch/arm64/mm/mmu.cpp:1082`；相关：`src/arch/arm64/mm/mmu.cpp:429`、`src/arch/arm64/mm/mmu.cpp:1118`、`src/arch/arm64/boot/start.S:90`、`linker_arm64.ld:10`、`src/kernel/syscalls/mm.cpp:459`
- 证据：linker_arm64.ld 把内核链接在物理地址 0x40100000，内核代码、GIC(0x08000000)、UART(0x09000000) 都经 TTBR0 恒等映射访问。create_space 因此把内核块拷进每个用户地址空间：`new_l1[1] = current_l1[1]; new_l1[2] = current_l1[2]; new_l1[3] = current_l1[3];`（0x40000000-0xFFFFFFFF 的 1GB 块，start.S 的 BLOCK_NORMAL 为 AP_RW_EL1 且没有 UXN/PXN）。get_l0_table (mmu.cpp:429) 对 CURRENT 一律返回 TTBR0 的表，l0_index 只取 bit[47:39]，所以内核高地址 0xFFFF0000_xxxxxxxx 被当作低地址别名在用户表里查/改，TTBR1 的表在启动后再也不会被更新；`KERNEL_L0_START 256` 的“内核项拷贝”对 TTBR0 表毫无意义。
- 触发场景：(1) 用户虚拟地址 0x40000000-0xFFFFFFFF 和两个设备 2MB 块永久被内核占用，ELF 段/mmap/brk 落入该范围时 hal::Mmu::map 返回 false（cannot map 4KB page over 1GB block）。(2) 内核镜像在每个用户地址空间内，且块描述符 UXN=0、AP=EL1-only，EL0 可以“只执行”方式跳入内核代码；所有以 `addr < KERNEL_VIRTUAL_BASE/USER_SPACE_END` 判定“用户地址”的检查（mm.cpp:459、elf.cpp:191）都把 0x40xxxxxx 的内核内存当作合法用户地址。(3) 进入用户进程后，任何 mm::Vmm::map_page/query/unmap 对内核虚拟地址的操作都落到当前用户页表的低地址别名上，而不是内核页表。
- 修复方向：把内核重定位到 TTBR1 高半区（链接到 KERNEL_VIRTUAL_BASE+物理偏移，设备 MMIO 通过高半区映射访问），用户页表只含用户映射；hal::Mmu 按虚拟地址最高位选择 TTBR1 根表（内核地址）或 space/TTBR0（用户地址），并对内核映射设置 UXN、对用户映射设置 PXN。
- 复核意见：linker_arm64.ld:14-19 把内核链接在物理 0x40100000；mmu.cpp:1082-1084 把 current_l1[1..3] 的 1GB 块拷入用户表，start.S 的 BLOCK_NORMAL 为 AP_RW_EL1 且无 UXN/PXN；get_l0_table (mmu.cpp:429-438) 对 CURRENT 只取 TTBR0 (mmu.cpp:312-314)，l0_index 只用 bit[47:39]，所以高半区地址会被解析到 TTBR0 表的别名；hal::Mmu::map 在块之上返回失败 (mmu.cpp:721/735)。描述属实，是 arch-arm64-mm-2 和 x-hal-parity-7 的共同设计根因，但本条列出的后果不同（VA 占用、EL0 可执行内核代码、别名查表），故不标重复。 更正：后果(1)目前是潜在的：ARM64 用户栈顶在 0x00007FFFFF000000 (task.h:33)，未验证 ELF/mmap/brk 实际会落入 0x40000000-0xFFFFFFFF。后果(2) EL0 以 execute-only 方式执行内核代码仍在 EL0 特权下，危害有限。最直接可利用的后果是 arch-arm64-mm-2。

#### V011 [high·已确认·arm64,x86_64·已修复] destroy_space 用 frame_ref_dec 释放用户页，而 frame_ref_dec 只减计数不归还位图，进程退出/exec 后所有用户页帧永久泄漏

- 位置：`src/arch/arm64/mm/mmu.cpp:1163`；相关：`src/arch/arm64/mm/mmu.cpp:1173`、`src/arch/arm64/mm/mmu.cpp:1325`、`src/arch/x86_64/mm/paging64.cpp:1239`、`src/arch/x86_64/mm/paging64.cpp:1254`、`src/mm/pmm.cpp:1307`
- 证据：free_page_table_recursive(): `uint32_t refcount = mm::Pmm::frame_get_refcount(frame); if (refcount > 0) { mm::Pmm::frame_ref_dec(frame); ...` 日志还写着 “Freed physical page”。但 mm::Pmm::frame_ref_dec (src/mm/pmm.cpp:1307-1338) 只做 `frame_refcount[idx]--`，不调用 clear_frame，也不更新 free_frames/used_frames；真正释放只在 mm::Pmm::free_frame 中进行。i686 路径 (src/mm/vmm.cpp:1115) 用的是 free_frame，所以只有 4 级页表架构泄漏。只有页表页本身通过 free_frame 被归还。
- 触发场景：任一用户进程退出（kernel::Scheduler::free → mm::Vmm::free_page_directory → hal::Mmu::destroy_space）或 execve 释放旧地址空间时，它的代码、数据、堆和预先全部映射的 1MB 用户栈（256 页）引用计数降到 0，但位图仍标记为已用，永远不会再被分配。每运行一个程序至少泄漏 1MB 以上，512MB 内存下运行几百个进程后 alloc_frame 开始失败，fork/exec 无法进行。
- 修复方向：叶子页改为调用 mm::Pmm::free_frame(frame)（它已处理 refcount>1 只减计数、==1 时释放），或者让 frame_ref_dec 在计数归零时真正释放帧并同步 pmm_info 统计。x86_64 的 free_page_table_recursive 有同样的调用（注释还误称 frame_ref_dec 会释放），需一并修改。克隆失败的清理路径同理。
- 复核意见：mmu.cpp:1161-1163 和 paging64.cpp:1237-1239 对叶子页只调 mm::Pmm::frame_ref_dec；pmm.cpp:1307-1338 的 frame_ref_dec 只做 frame_refcount[idx]--，不 clear_frame、不更新 free_frames（paging64.cpp:1240 的注释是错的）。真正释放只在 free_frame (pmm.cpp:965-967)；i686 路径 vmm.cpp:1115 用 free_frame。vmm.cpp:1027-1041 的 64 位分支直接调 destroy_space 后 return，没有日志，这解释了 x86_64 上“无 free_page_directory 日志且每轮泄漏约 266 页（256 页栈+代码/数据）”而 i686 无泄漏的实测结果。 更正：实测数据是 x86_64 的；arm64 上同一代码静态成立但因 exec 后内核崩溃未能动态观察。帧位图仍为已用但 refcount 已为 0，之后不会再被分配。

#### V012 [high·已确认·arm64·已修复] 内核态上下文首次运行时不恢复 PSTATE、也不经过 hal_context_enter_kernel_thread：idle 任务永远在屏蔽中断下运行，入口函数返回后会重新执行自身

- **修复**：src/kernel/task.cpp (arm64 branches of create_kernel_thread and task_create_idle): pc now points at task_enter_kernel_thread (alias of hal_context_enter_kernel_thread) with the real entry in x19, so kernel threads and idle start with interrupts enabled and call task_exit when the entry returns instead of re-running it. 验证方式：boot regression only (arm64 boot with kernel threads and idle, exec check passes)
- 位置：`src/arch/arm64/task/context_asm.S:280`；相关：`src/kernel/task.cpp:534`、`src/kernel/task.cpp:873`、`src/arch/arm64/task/context_asm.S:286`、`src/arch/arm64/task/context_asm.S:441`
- 证据：`.restore_kernel` 只做 `mov sp, x2; ldr x30, [x1, #CTX_PC]; ...; ret`，完全忽略 CTX_PSTATE 的 DAIF，也不从 CTX_X30 恢复 LR。task.cpp 在 ARM64 分支把 `context.pc = (uintptr_t)entry` / `idle_task->context.pc = (uintptr_t)idle_task_loop`（task.cpp:534, 873），绕过了会执行 `msr daifclr, #0xf` 并在返回后调用 task_exit 的 hal_context_enter_kernel_thread。schedule() 总是在 Interrupts::disable() 之后切换，所以新内核上下文继承的是屏蔽状态；idle_task_loop 里 hal::Cpu::halt() 只是 `wfi`。
- 触发场景：唯一的用户进程（shell）调用 sleep/nanosleep：任务置 BLOCKED，schedule() 切到 idle（首次运行，IRQ 屏蔽）。wfi 因有 pending IRQ 立即返回但中断不被接收，idle 循环 yield 发现无就绪任务又回到 wfi；timer_tick 永不执行，睡眠任务永不唤醒，系统永久挂死。另外通过 create_kernel_thread 创建的线程其入口函数一旦 return，x30==入口地址，会无限重入该函数而不是 task_exit。
- 修复方向：ARM64 的内核线程/idle 统一用 hal::Context::init(…, false) 的方式：pc 指向 hal_context_enter_kernel_thread、x19 保存真实入口；或者在 `.restore_kernel` 中按 CTX_PSTATE 恢复 DAIF 并从 CTX_X30 恢复 LR。idle 循环在 wfi 前显式开中断。
- 复核意见：context_asm.S:280-309 的 .restore_kernel 只恢复 SP/x0-x29 并把 CTX_PC 装入 x30 后 ret，不处理 DAIF；task.cpp:873 把 idle 的 pc 直接设为 idle_task_loop、task.cpp:534 把内核线程 pc 设为 entry，都绕过 hal_context_enter_kernel_thread (context_asm.S:441-453，唯一执行 daifclr 和 task_exit 的地方)。schedule() 在 Interrupts::disable() 后切换，idle 首次运行继承屏蔽状态；idle 再调 yield 时 prev_state 为 false，Interrupts::restore (interrupt.h:71-75) 不会开中断，hal::Cpu::halt (arm64/hal.cpp:76) 只是 wfi。因此只要所有任务都 BLOCKED（Scheduler::sleep，task.cpp:1257），timer_tick 就再也不会运行。入口函数返回时 x30==entry 会重入，这点也成立。 更正：是否在日常 shell 交互中触发取决于 shell 读输入是阻塞还是轮询 yield（未核实）；sleep/nanosleep 路径必然触发。arm64 上 create_kernel_thread 的唯一调用点是 kernel.cpp:668 的 kernel_shell，其入口通常不返回，重入问题属于潜在缺陷。

#### V013 [medium·已确认·arm64·已修复] #address-cells/#size-cells 未按节点层级作用域,兄弟/父子节点互相污染导致 memory/设备 reg 误解析甚至死循环

- **修复**：src/arch/arm64/dtb/dtb.cpp: #address-cells/#size-cells are tracked per node depth; a node's reg is decoded with its parent's cells, nodes that declare nothing give their children the devicetree defaults (2/1), and the memory loop stops when the entry size is 0. 验证方式：boot regression only (arm64 boot still reports Total memory = 0x8000000 from the QEMU DTB; no synthetic DTB unit test)
- 位置：`src/arch/arm64/dtb/dtb.cpp:345`；相关：`src/arch/arm64/dtb/dtb.cpp:198`、`src/arch/arm64/dtb/dtb.cpp:200`、`src/arch/arm64/dtb/dtb.cpp:359`、`src/arch/arm64/dtb/dtb.cpp:375`
- 证据：parse_context_t ctx 只在 parse_structure_block 开头初始化一次(345-348: addr_cells=2, size_cells=1),FDT_BEGIN_NODE(359)/FDT_END_NODE(375) 都不保存/恢复/重置 cells。按 DT 规范,一个节点的 reg 必须用其“父节点”的 #address-cells/#size-cells 解释;此处 parse_property 更新的是全局 ctx(185-190),任何节点(如 /cpus 设 #address-cells=1 #size-cells=0)设置的 cells 会泄漏到之后遍历的兄弟节点。memory reg 解析用 `entry_size=(addr_cells+size_cells)*4`(198),若 size_cells 被前面的节点污染为 0,则 size 按 0 个 cell 解析为 0;若 addr_cells+size_cells 都为 0,`entry_size==0` 使 `while(offset+entry_size<=len...)`(200) 的 offset 永不前进 → 无限循环挂死。
- 触发场景：若 QEMU 生成的 DTB 中存在一个在 /memory 之前出现、且本地重定义 #size-cells=0 的节点(如 /cpus),ctx.size_cells 变为 0 并泄漏到 /memory:parse_reg_property 把 size 解析为 0,g_dtb_info.total_memory 累加 0,boot_info_init_dtb 得到 total_memory=0,mm::Pmm::init_boot_info 走到 `No usable memory`/max_addr 异常而 PANIC;若某节点使 addr_cells+size_cells=0,则 parse_property 在 memory 的 reg 上死循环挂起。设备 reg(GIC/UART)同样会用错误 cell 数解析出错误 base/size。
- 修复方向：用深度栈保存/恢复每层的 #address-cells/#size-cells:进入 FDT_BEGIN_NODE 时压入父节点当前值并把子节点继承值作为新默认,退出 FDT_END_NODE 时弹回;用“父节点 cells”解析 reg。并在 memory 解析处断言 entry_size>0,防止 entry_size==0 死循环。
- 复核意见：dtb.cpp:345-348 只初始化一次 ctx，parse_property(185-190) 遇到任意节点的 #address-cells/#size-cells 就覆盖全局值，FDT_BEGIN_NODE(359)/FDT_END_NODE(375) 只改 depth 不保存恢复，代码缺陷属实；198-200 行 entry_size 为 0 时 offset 不前进也属实(但需 num_memory_regions 未满，parse_reg_property 每次成功会递增计数，到 DTB_MAX_MEMORY_REGIONS 即退出，所以不是真正的无限循环)。不过实测 arm64 在 QEMU virt 上正常启动且测试全过，说明 /memory 在该 DTB 中先于污染节点出现；且 GIC/UART 的 DTB 解析结果基本未被驱动使用(serial.cpp:34 用 PL011_DEFAULT_BASE，grep 不到 distributor_base 的使用者)。因此是真实但潜伏的解析缺陷。 更正：“死循环挂死”不成立：entry_size==0 时每轮 num_memory_regions++，到 DTB_MAX_MEMORY_REGIONS 后循环结束，后果是内存区域表被垃圾条目填满而非挂死。在当前 QEMU virt 上不触发(内存节点靠前、设备基址用硬编码)，严重度降为 medium；pcie(#address-cells=3) 之后的 pl011/intc reg 会被解析错，但结果未被使用。修复建议(按深度保存父节点 cells)正确。

#### V014 [medium·待确认·arm64·已修复] ARM64 create_space 只为用户地址空间硬编码映射 GIC 和 UART 两个设备块，virtio-gpu 等其它 MMIO 在用户进程上下文中不可访问

- **修复**：src/arch/arm64/mm/mmu.cpp create_space: adds the 2MB device block for 0x0a000000 (virtio-mmio) next to the GIC and UART blocks, and marks all three device blocks UXN|PXN. 验证方式：unit test test_arm64_create_space_maps_kernel_devices passes; not run with a virtio-gpu-device attached
- 位置：`src/arch/arm64/mm/mmu.cpp:1074`；相关：`src/drivers/arm/virtio_gpu.cpp:23`、`src/drivers/arm/virtio_gpu.cpp:94`、`src/fs/devfs.cpp:203`
- 证据：`new_l2[64] = 0x08000000ULL | dev_block_flags | DESC_TYPE_BLOCK; new_l2[72] = 0x09000000ULL | ...;` 用户地址空间 0~1GB 只保留这两个 2MB 块。而 drivers/arm/virtio_gpu.cpp 通过恒等地址访问 `VIRTIO_MMIO_BASE 0x0a000000`（`virtio_base + offset`，virtio_gpu.cpp:23/94/98），引导页表里它由 L1[0] 的 1GB 设备块覆盖，用户页表里 L2[80] 为空。
- 触发场景：QEMU 带 virtio-gpu 设备启动 ARM64：kernel_main 中 Framebuffer::terminal_init 成功（此时 TTBR0 是引导页表）。shell 运行后在其地址空间内执行 write(1,...) → devconsole_write → Framebuffer::terminal_putchar/flush → virtio-gpu MMIO 访问 0x0a00xxxx，经 TTBR0 查表为未映射，EL1 translation fault，内核停机。Makefile 默认 -nographic 无该设备时 Framebuffer 未初始化，问题被掩盖（未在带设备的配置下动态验证）。
- 修复方向：根因同上一条：设备 MMIO 应映射在 TTBR1 内核半区并通过该虚拟地址访问；短期可在 create_space 中把引导页表 0~1GB 中所有内核设备块（至少 0x0a000000 virtio-mmio 区）一并拷入，而不是硬编码两个索引。
- 复核意见：src/arch/arm64/mm/mmu.cpp:1074-1079 确实只在新建的 L2 表里填了 L2[64](GIC) 和 L2[72](UART)，L2[80](0x0a000000) 为空；src/drivers/arm/virtio_gpu.cpp:23/94/98/200 通过恒等地址 0x0a000000 访问 MMIO，src/fs/devfs.cpp:203-214 在 Framebuffer 已初始化时会走 terminal_putchar/flush -> VirtioGpu::flush。静态链路成立，但 Makefile:346/457 用的是 -device virtio-gpu-pci，find_virtio_gpu (virtio_gpu.cpp:381) 只扫 virtio-mmio 槽位，PCI 设备找不到，Framebuffer 不会初始化，因此默认配置下不可达，未能动态验证。 更正：触发条件应写明为使用 -device virtio-gpu-device (virtio-mmio 传输) 启动；Makefile 现有的 virtio-gpu-pci 目标不会触发。根因与 x-hal-parity-6 相同（设备 MMIO 依赖 TTBR0 恒等映射），严重度降为 medium（非默认配置）。


### 架构层：x86_64（9 项）

#### V015 [critical·已确认·x86_64,i686·已修复] 任何来自用户态的 CPU 异常都会打印 PANIC 并 cli;hlt 停掉整个内核(用户态可触发的内核挂死)

- 位置：`src/arch/x86_64/interrupt/isr64.cpp:112`；相关：`src/arch/x86_64/interrupt/isr64.cpp:168`、`src/arch/x86_64/interrupt/isr64.cpp:207`、`src/arch/x86_64/interrupt/isr64.cpp:236`、`src/arch/i686/interrupt/isr.cpp:105`
- 证据：isr64_handler 对未注册的向量计算了 from_usermode=(regs->cs&3)==3 却只用于打印,随后无条件 __asm__ volatile("cli; hlt"); for(;;)(112-113)。page_fault_handler 在 handle_kernel_page_fault/handle_cow_page_fault 都失败后同样 cli;hlt(168),general_protection_fault_handler(207) 和 double_fault_handler 亦然。没有任何路径对来自 Ring3 的异常调用 task_exit 或杀死当前进程。
- 触发场景：任意用户程序执行 *(int*)0=1(#PF)、整数除零(#DE)、ud2(#UD)、越权 in/out(#GP)等,内核即进入 cli;hlt 永久停机,一个普通用户进程就能让整机宕机(拒绝服务)。正确的操作系统应只终止出错进程并继续调度其它任务。
- 修复方向：在异常处理中区分 from_usermode:若来自 Ring3,打印诊断后调用 task_exit/发送致命信号终止当前进程并重新调度,而非 cli;hlt 整机;仅内核态不可恢复异常才停机。
- 复核意见：src/arch/x86_64/interrupt/isr64.cpp:79-113 对未注册向量无条件 cli;hlt，from_usermode 只用于打印；isr64.cpp:281-283 只注册了 8/13/14 三个处理函数，而 page_fault_handler(168)、general_protection_fault_handler(207)、double_fault_handler 末尾同样 cli;hlt。src/arch/i686/interrupt/isr.cpp:76-107 及其 #PF/#GP 处理函数结构完全相同，两个目录中 grep 不到任何 task_exit 调用。用户态一条 ud2、除零或空指针访问即可让整机停机。
- 被 2 个独立审计者重复报告（arch-x86_64-core-2, arch-x86_64-mm-3）

#### V016 [critical·已确认·x86_64·已修复] SYSCALL 返回路径先 pop rsp 恢复用户栈、后 cli,开中断窗口内中断帧以 Ring0 权限写到用户可控地址

- 位置：`src/arch/x86_64/syscall/syscall64_asm.asm:203`；相关：`src/arch/x86_64/syscall/syscall64_asm.asm:149`、`src/arch/x86_64/syscall/syscall64_asm.asm:210`、`src/arch/x86_64/interrupt/irq64_asm.asm:60`
- 证据：syscall_entry 在第 149 行 sti 后整个系统调用都开着中断;返回序列为 pop rax(200) / pop rsp 恢复用户 RSP(203) / cli(210) / o64 sysret(211)。pop rsp 执行完成后的指令边界上 CPU 仍处于 CPL0 且 IF=1,而 RSP 已是用户在执行 syscall 前自己放入的任意值(入口处 mov [rel user_stack_ptr], rsp 原样保存)。IRQ 门 IST=0 且同特权级中断不换栈,CPU 会直接在该用户地址处压入 SS/RSP/RFLAGS/CS/RIP,随后 irq_common_stub 再压 17 个 qword 并进入 timer_tick(栈帧约 2KB)。无 SMAP、无对 RSP 的任何校验。
- 触发场景：用户程序执行 mov rsp,<内核地址如 task_pool/IDT/页表直映地址>; syscall。当 100Hz 定时器或键盘中断恰好在 pop rsp 与 cli 之间的指令边界被识别时,内核把中断帧与用户可控寄存器值以 Ring0 写入该内核地址,造成任意内核内存覆盖(提权);若 RSP 指向未映射地址则中断投递 #PF→#DF(IST1=0)→三重故障重启。此为真实竞态,长时间运行或高中断负载下可触发。
- 修复方向：在恢复用户 RSP 之前关中断:将 cli 移到恢复寄存器/弹出 rsp 之前,保证 RSP=用户值 时 IF=0;更稳妥的做法是返回路径统一在内核栈上构造 iretq 帧返回用户态,或引入 per-CPU 区 + swapgs 保存用户 RSP。
- 复核意见：src/arch/x86_64/syscall/syscall64_asm.asm:149 执行 sti 后一直开中断，返回序列为 pop rax(200)/pop rsp(203)/cli(210)/o64 sysret(211)，pop rsp 不像 mov ss 那样屏蔽中断，故其后的指令边界上 CPL=0、IF=1、RSP=用户值。入口处 102 行 mov [rel user_stack_ptr], rsp 对用户 RSP 无任何校验；irq64.cpp:236-251 的 IRQ 门全部用 idt64_set_interrupt_gate，即 IDT64_IST_NONE (idt64.cpp:71-74)，同特权级中断不换栈。用户程序 libc 确实使用 syscall 指令 (user/lib/src/arch/x86_64/syscall.S)，路径可达，属于用户可触发的内核内存写/崩溃竞态。 更正：窗口只有一条指令，QEMU TCG 下中断通常只在翻译块边界投递，较难命中，KVM/真机可命中；irq_common_stub 压栈的寄存器数量（15 还是 17）不影响结论。另外 user_stack_ptr/kernel_stack_ptr 是全局变量而非 per-CPU，修复时应一并考虑。最小修复是把 cli 移到 186 行 pop r15 之前。
- 被 5 个独立审计者重复报告（arch-x86_64-core-1, gap6-abi-struct-parity-lp64-3, kernel-proc-syscalls-6, user-lib-2, x-asm-abi-2）

#### V017 [high·已确认·x86_64,arm64·已修复] x86_64/arm64 的 destroy_space 用 frame_ref_dec 代替 free_frame，进程退出时所有用户数据页永久泄漏；测试只断言“至少回收 1 帧”把该泄漏固化

- **修复**：The leak itself was already fixed by 2d693a8. The test that hid it is now strict: src/tests/arch/x86_64/paging64_test.cpp test_pbt_x86_64_destroy_space_frees_memory requires free_frames after destroy_space to equal the count before create_space (it previously accepted one recovered frame). This updates an existing test that asserted the old behaviour. 验证方式：unit test test_pbt_x86_64_destroy_space_frees_memory passes with the strict assertion; the new arm64 test_arm64_create_space_maps_kernel_devices also checks create/destroy returns every frame
- 位置：`src/arch/x86_64/mm/paging64.cpp:1239`；相关：`src/arch/x86_64/mm/paging64.cpp:1254`、`src/arch/x86_64/mm/paging64.cpp:1392`、`src/arch/arm64/mm/mmu.cpp:1163`、`src/arch/arm64/mm/mmu.cpp:1173`、`src/arch/arm64/mm/mmu.cpp:1325`、`src/mm/pmm.cpp:1334`
- 证据：free_page_table_recursive() 在 level==1 时：`mm::Pmm::frame_ref_dec(frame); /* If refcount becomes 0, the frame is freed by mm::Pmm::frame_ref_dec */`。但 src/mm/pmm.cpp:1307-1338 的 frame_ref_dec 只做 `frame_refcount[idx]--`，既不 clear_frame() 也不更新 pmm_info.free_frames。真正释放只发生在 mm::Pmm::free_frame()（pmm.cpp:921）。arm64 的 src/arch/arm64/mm/mmu.cpp:1163 是同样的写法。mm::Vmm::free_page_directory() 在 64 位上直接调用 hal::Mmu::destroy_space（vmm.cpp:1040），task 回收（task.cpp:215）走这条路径。测试 src/tests/arch/x86_64/paging64_test.cpp:904-908 写着 `/* Note: We may not recover all frames due to reference counting */ ASSERT_TRUE(frames_recovered >= 1);`，只要 PML4 被释放就通过，从而掩盖了泄漏。
- 触发场景：x86_64 或 arm64 上任意用户进程 exit 后被父进程回收：free_page_directory -> destroy_space -> 每个用户页 refcount 1->0，但位图仍标记为已用、free_frames 不增加。每次 fork+exit / exec 都泄漏该进程全部私有页（代码、数据、栈、堆），shell 里反复运行程序即可把物理内存耗尽，alloc_frame 返回 PADDR_INVALID。测试自身每次开机也因此泄漏 10x5=50 帧（test_pbt_x86_64_destroy_space_frees_memory）。
- 修复方向：在 x86_64/arm64 的 free_page_table_recursive 中对叶子页改用 mm::Pmm::free_frame(frame)（它自己处理引用计数并在归零时清位图），大页分支同理；clone 失败回滚路径（paging64.cpp:1392、mmu.cpp:1325）也一样。测试改为断言 destroy 之后 free_frames == info_before.free_frames，并加 mm::Pmm::verify_consistency() 检查。
- 复核意见：src/arch/x86_64/mm/paging64.cpp:1236-1246 与 src/arch/arm64/mm/mmu.cpp:1159-1173 在叶子层只调用 mm::Pmm::frame_ref_dec；而 src/mm/pmm.cpp:1307-1338 的 frame_ref_dec 仅做 frame_refcount[idx]--，不清位图也不增加 free_frames，真正释放只在 Pmm::free_frame (pmm.cpp:921) 中。64 位上 Vmm::free_page_directory (vmm.cpp:1028-1042) 直接走 hal::Mmu::destroy_space，因此用户数据页永不归还，只有页表帧被 free_frame 回收。这与实测 x86_64 每次 fork/exec/exit 泄漏约 266 页（256 栈页 + ELF 页）、i686 无泄漏完全吻合；paging64_test.cpp:902-908 只断言 frames_recovered >= 1，确实掩盖了该泄漏。 更正：arm64 上内核专用映射（!desc_is_user）被显式跳过属正确行为，泄漏只涉及用户页。修复时注意 Pmm::free_frame 对 refcount==0 但位图已置位的帧会告警后仍释放；测试断言应改为 destroy 后 free_frames 恢复到映射前的值。
- 被 2 个独立审计者重复报告（tests-mm-arch-1, x-process-lifecycle-2）

#### V018 [high·已确认·x86_64,arm64·已修复] x86_64 销毁地址空间时叶子物理页只递减引用计数、从不释放：进程退出/exec 后所有用户页永久泄漏

- 位置：`src/arch/x86_64/mm/paging64.cpp:1239`；相关：`src/arch/x86_64/mm/paging64.cpp:1254`、`src/arch/x86_64/mm/paging64.cpp:1392`、`src/arch/arm64/mm/mmu.cpp:1163`、`src/arch/arm64/mm/mmu.cpp:1173`、`src/mm/pmm.cpp:1307`、`src/tests/mm/vmm_test.cpp:818`
- 证据：free_page_table_recursive() 的 level==1 分支：`uint32_t refcount = mm::Pmm::frame_get_refcount(frame); if (refcount > 0) { mm::Pmm::frame_ref_dec(frame); /* If refcount becomes 0, the frame is freed by mm::Pmm::frame_ref_dec */`。注释的假设是错的：src/mm/pmm.cpp:1307-1338 的 frame_ref_dec 只做 `frame_refcount[idx]--` 并返回新计数，不调用 clear_frame、不修改 free_frames；真正“递减并在归零时释放”的是 mm::Pmm::free_frame()（pmm.cpp:955-970），i686 路径 vmm.cpp:1115 用的正是 free_frame。结果：页表页本身被 free_frame 释放，但所有叶子帧停留在“位图已用 + refcount=0”状态，再也无法分配。测试也在掩盖它：tests/mm/vmm_test.cpp:818 允许 ±5 帧误差（正好等于映射的 5 页），paging64_test.cpp:905-908 只要求回收 >=1 帧并注明 "may not recover all frames due to reference counting"。arm64 的 src/arch/arm64/mm/mmu.cpp:1161-1173 是同样的写法（仅 grep 核对）。
- 触发场景：用户 shell 执行任意外部命令（fork + exec + exit）：子进程 exec 时建立新地址空间（ELF 页 + setup_user_stack 一次性分配的 256 页/1MiB 用户栈）；子进程退出后父进程 waitpid -> kernel::Scheduler::free -> mm::Vmm::free_page_directory -> hal::Mmu::destroy_space -> free_page_table_recursive：每个叶子帧 refcount 1->0 但位图仍标记已用。每运行一条命令至少泄漏 1MiB 以上物理内存；Makefile 的 run 目标未指定 -m（QEMU 默认 128MiB，其中 32MiB 还被内核堆预留），大约几十条命令后 mm::Pmm::alloc_frame() 返回 PADDR_INVALID，fork/exec/brk/mmap 全部失败。exec 释放旧地址空间（process.cpp:500）和 ELF 加载失败回滚路径同样泄漏。
- 修复方向：叶子帧改为调用 mm::Pmm::free_frame(frame)（它在 refcount>1 时只递减、==1 时真正释放），2MB 大页用 mm::Pmm::free_huge_page；删除错误注释；arm64 mmu.cpp 同步修改；把 vmm_test/paging64_test 的断言收紧为 free_frames 精确恢复。
- 复核意见：src/arch/x86_64/mm/paging64.cpp:1236-1246 的叶子分支只调用 mm::Pmm::frame_ref_dec，而 src/mm/pmm.cpp:1307-1338 的 frame_ref_dec 只做 frame_refcount[idx]-- 并返回，不 clear_frame 也不更新 free_frames；真正归零释放的是 pmm.cpp:921-970 的 free_frame，i686 路径 vmm.cpp:1115 用的就是它。vmm.cpp:1038-1041 在 64 位架构上走 hal::Mmu::destroy_space，arm64 的 mmu.cpp:1158-1175 写法相同。这与实测 x86_64 每次 fork/exec/exit 泄漏约 266 页（256 页用户栈加 ELF 页）完全吻合。 更正：arm64 上代码缺陷相同，但目前用户 shell 的 exec 会先因 COW 栈写 Access flag fault 崩溃，泄漏只在 x86_64 上被实测到。克隆失败回滚处 paging64.cpp:1392 的 frame_ref_dec 针对的是大页共享引用，同样不会释放。
- 被 4 个独立审计者重复报告（arch-x86_64-mm-1, gap2-i686-fork-cow-refcount-1, mm-pmm-1, mm-vmm-4）

#### V019 [high·已确认·x86_64·已修复] x86_64: SFMASK 未屏蔽 NT 标志，用户置 NT 后内核经 iretq 切到新用户任务时在 ring0 触发 #GP

- **修复**：src/arch/x86_64/syscall/syscall64_asm.asm: SFMASK changed from 0x700 to 0x44700 (adds NT and AC); enter_usermode64 loads clean flags (push 2 / popfq) before iretq. src/arch/x86_64/task/context64_asm.asm: .restore_user does the same before building the IRETQ frame. 验证方式：boot regression only (x86_64 boot, user shell reached; setting NT from user mode was not exercised)
- 位置：`src/arch/x86_64/syscall/syscall64_asm.asm:441`；相关：`src/arch/x86_64/task/context64_asm.asm:172`、`src/arch/x86_64/interrupt/isr64.cpp:175`
- 证据：`mov eax, 0x00000700 ; Clear IF, TF, DF` —— SFMASK 没有包含 NT(bit 14)/AC(bit 18)。用户态可用 popfq 置 NT；SYSCALL 不经过中断门，NT 原样带入内核。context64_asm.asm 的 .restore_user 路径(141-172 行)直接 `iretq`，之前没有 popfq/清 NT(只有 .restore_kernel 路径 188-189 行会 popfq)。长模式下 NT=1 时执行 IRETQ 产生 #GP(0)。
- 触发场景：用户进程执行 pushfq/or NT/popfq 后 fork()，再 waitpid()/sched_yield()：父进程在 syscall 上下文(RFLAGS.NT=1)调用 schedule()，选中首次运行的子进程(context.cs=0x23)，hal_context_switch_asm 走 .restore_user，`iretq` 在 ring0 触发 #GP，general_protection_fault_handler 按内核态故障处理，内核崩溃(非特权 DoS)。未验证项：未动态确认内核态 #GP 处理后是 panic/停机(从 isr64.cpp 代码看只打印寄存器)。
- 修复方向：SFMASK 设为至少 0x47700(TF|IF|DF|NT|AC)；并在 .restore_user 的 iretq 之前用 `push 0x2; popfq` 清理当前 RFLAGS。
- 复核意见：src/arch/x86_64/syscall/syscall64_asm.asm:441 的 SFMASK 确为 0x700，不含 NT(bit14)；syscall_entry 到 `sti`/`call syscall_dispatcher` 之间没有任何 popfq 清标志，hal.cpp:151-168 与 interrupt.h 的 pushfq/popfq 只会原样保留 NT。fork 子进程的 context.cs 被设为 0x23（src/kernel/syscalls/process.cpp:285），首次被调度时 context64_asm.asm:139-172 的 .restore_user 直接 iretq，长模式下 NT=1 的 IRETQ 产生 #GP(0)；同样 enter_usermode64（syscall64_asm.asm:483-527）只 cli 不清 NT 就 iretq。isr64.cpp:175-209 的 #GP 处理函数无条件 `cli; hlt; for(;;)`，因此是用户态可触发的整机停机。 更正：内核态 #GP 的后果已可静态确认：isr64.cpp:207 直接 cli;hlt 死机，不是“只打印寄存器”。触发路径不止 fork+waitpid：置 NT 后调用 exec 走 enter_usermode64 的 iretq（syscall64_asm.asm:527）同样会 #GP，修复时该处也要清 NT。因需要用户态故意构造 RFLAGS、正常程序不会触发，评为 high（若按“用户态可致内核停机”的字面标准可视为 critical）。未在 QEMU 上实测。

#### V020 [medium·已确认·x86_64·已修复] x86_64 引导页表硬编码在物理地址 0x200000-0x202FFF，与内核镜像自身（当前为 .lbss 中的 protected_frames[]）重叠

- **修复**：src/arch/x86_64/boot/boot64.asm and linker_x86_64.ld: the boot PML4/PDPT/PD are now three pages in a .boot.pagetables section linked at physical addresses right after .text.boot (0x102000-0x104FFF in the current build) instead of fixed 0x200000-0x202FFF inside .lbss. The linker script also collects .lbss/.ldata/.lrodata into .bss/.data/.rodata. 验证方式：boot regression only (x86_64 boot and full test suite pass; layout checked with objdump/nm)
- 位置：`src/arch/x86_64/boot/boot64.asm:42`；相关：`src/arch/x86_64/boot/boot64.asm:143`、`linker_x86_64.ld:13`、`linker_x86_64.ld:65`、`src/mm/pmm.cpp:33`、`src/mm/vmm.cpp:298`
- 证据：boot64.asm: `BOOT_PML4_PHYS equ 0x200000 / BOOT_PDPT_PHYS equ 0x201000 / BOOT_PD_PHYS equ 0x202000`，setup_page_tables 用 `rep stosd` 清零并写入这 3 页；vmm.cpp:298-311 在 x86_64 上“直接使用引导时的页表”作为内核 PML4。但 linker_x86_64.ld 从 `. = 0x100000` 开始连续放置内核，当前构建产物 (objdump -h build/x86_64/castor.bin) 物理范围为 0x100000-0x2FB000：`.lbss` LMA 0x1d2d80 大小 0x127800，nm 显示 `task_pool` 0x1d2d80-0x1fa580、`protected_frames` 0x1fa580-0x2fa580。即 protected_frames[1448..2215]（每项 16 字节）与活动的 PML4/PDPT/PD 是同一块物理内存。页表所在地址没有任何链接脚本保留/符号约束。
- 触发场景：(1) 现状：当 mm::Pmm::protect_frame() 累计保护的页表帧数达到 1449 时开始写 PML4[0]/[1]（refcount=1 使 PML4[1] 变为 present 并指向物理 0）；达到 1577 项时 `protected_frames[1576].frame = frame` 覆盖 PML4[256]（内核高半区入口，写入的帧地址 P=0），下一次 TLB 未命中即三重故障重启。(2) 内核再增长约 23KB（.text/.rodata/.data/.bss 任一变大）后 task_pool 尾部会落到 0x200000：引导代码写入的页表使这些 BSS 对象不再为 0，内核写 task 结构即破坏活动页表；继续增长到 .data/.text 落入该区间时，`rep stosd` 会在启动时直接清掉 GRUB 已加载的内核代码/数据，表现为随机的启动崩溃。
- 修复方向：不要使用固定物理地址：把引导 PML4/PDPT/PD 作为内核镜像的一部分（在 .bss/.data 中按 4K 对齐保留 3 页并导出符号，32 位代码用 `sym - KERNEL_VMA` 访问），或在链接脚本中显式保留该区域并加 ASSERT(_kernel_end_phys <= BOOT_PML4_PHYS 或页表符号在镜像内)。同时在 linker_x86_64.ld 中显式收集 `.lbss/.ldata/.lrodata`（-mcmodel=large 产生），目前它们是孤儿段且不在 _bss_start/_bss_end 内。
- 复核意见：重叠属实：src/arch/x86_64/boot/boot64.asm:42-44 把页表固定在物理 0x200000-0x202FFF，143-146 行用 rep stosd 清零这 3 页；现有 build/x86_64/castor.bin 中 .lbss 的 LMA 为 0x1d2d80、大小 0x127800，nm 显示 protected_frames 位于 0x1fa580-0x2fa580，_kernel_end 为 0x2fb000。linker_x86_64.ld 没有收集 .lbss，也没有任何 ASSERT，src/mm/vmm.cpp:295-306 确实一直沿用引导页表。但场景 (1) 在当前代码中不可达：protect_frame 的全部调用点（vmm.cpp:142、183、946 及对应的 unprotect）都在 ARCH_I686 分支内，x86_64 的 create/clone/free 走 HAL 路径（vmm.cpp:775-831、1027-1042），protected_frame_count 恒为 0，protected_frames 从不被写入。所以目前是潜伏缺陷，真实风险是场景 (2)：task_pool 结束于 0x1fa580，距 0x200000 仅 0x5a80（约 23KB）。 更正：场景 (1)（protect_frame 累计到 1449/1577 项后覆盖 PML4）应删除或标注为在 x86_64 上当前不可达，因为 x86_64 没有 protect_frame 调用者。实际后果是布局漂移：内核再增长约 23KB，task_pool 尾部就会与活动 PML4 重叠；或将来在 x86_64 上启用 protect_frame 时才触发。严重度因此由 high 下调为 medium。修复建议本身（页表放进镜像、链接脚本显式收集 .lbss 并加 ASSERT）是合理的。
- 被 3 个独立审计者重复报告（build-system-1, kernel-init-shell-2, x-asm-abi-5）

#### V021 [medium·已确认·x86_64·已修复] x86_64：引导页表硬编码在物理地址 0x200000-0x202FFF，落在内核 BSS 内部（当前与 protected_frames 重叠，距 task_pool 只有约 23KB）

- **修复**：Same change as build-system-1 (duplicate report of the hard-coded boot page table location). 验证方式：boot regression only
- 位置：`src/arch/x86_64/boot/boot64.asm:42`；相关：`src/arch/x86_64/boot/boot64.asm:143`、`src/mm/pmm.cpp:33`、`src/mm/vmm.cpp:300`、`linker_x86_64.ld:65`
- 证据：`BOOT_PML4_PHYS equ 0x200000 / BOOT_PDPT_PHYS equ 0x201000 / BOOT_PD_PHYS equ 0x202000`，setup_page_tables 直接对这 3 页 rep stosd 清零并写入页表；链接脚本没有为它们保留空间，PMM 也不知道它们。现有 build/x86_64/castor.bin 的符号表：`_bss_start=0x1b1000`、`task_pool` 结束于物理 0x1fa580、`_ZL16protected_frames` 位于 0x1fa580，大小 0x100000、`_kernel_end=0x2fb000`。也就是说仍在使用的 PML4/PDPT/PD（vmm.cpp:300 '直接使用引导时的页表'，所有进程的内核半区都指向它们）就在 pmm.cpp 的 `static protected_frame_t protected_frames[65536]` 数组内部（下标 1448 起，每项 16 字节）。
- 触发场景：(1) 内核 .text/.rodata/.data 再增长约 23KB，task_pool 就会覆盖 0x200000：Scheduler::init 的 `memset(task_pool, 0, sizeof(task_pool))` 把正在使用的 PML4 清零，立刻 triple fault；增长约 300KB 后 .data 落在该处，引导代码的清零会在进入 C 代码之前破坏 GRUB 加载的已初始化数据。(2) 即使布局不变，protect_frame() 登记的条目数达到 1448 以后，写入的 frame/refcount 就会覆盖 PML4、PDPT、PD 表项。整个启动是否成功取决于链接顺序和内核大小的巧合。
- 修复方向：把引导页表放进内核镜像自己的段里（像 i686 的 boot_page_directory 那样在 .data/.bss 中 align 4096 预留，32 位引导代码用 符号地址-KERNEL_VMA 访问），或在链接脚本中显式保留该区间并在 PMM 中标记为已用。
- 复核意见：src/arch/x86_64/boot/boot64.asm:42-44 确实把 PML4/PDPT/PD 硬编码在物理 0x200000-0x202FFF，setup_page_tables 直接 rep stosd 清零并写入；linker_x86_64.ld 没有为其保留空间。现有 build/x86_64/castor.bin 的 objdump/nm 显示 .lbss 占 0x1d2d80-0x2fa580，protected_frames 位于 0x1fa580（src/mm/pmm.cpp:33，65536 项），task_pool 在 0x1d2d80，即引导页表确实落在 protected_frames 数组内部，而 vmm.cpp:296-300 继续使用这套引导页表。当前能启动只是因为该处是 NOBITS 且 protect_frame 条目数远小于 1448，属于真实但潜伏的布局缺陷。 更正：严重度下调为 medium：目前不会触发，x86_64 上 protect_frame 只经 vmm.cpp:119-183 少量调用，达到 1448 项不现实；真正风险是内核镜像再增长约 23KB 后 task_pool 覆盖 0x200000。task.cpp 中未见整体 memset(task_pool)，只有 task.cpp:166 的逐项 memset，但任何对落在该区间的 task_t 的写入同样会破坏页表。另外 task_pool/protected_frames 实际在 .lbss（-mcmodel=large 产生的孤儿段，位于 _bss_end 之后）而非 .bss。

#### V022 [medium·已确认·x86_64·已修复] boot64 未启用 SSE(CR4.OSFXSR 缺失)且无 per-task FPU/SSE 状态保存,但用户程序按启用 SSE 编译,FP 调用触发 #UD 进而内核 PANIC

- **修复**：PARTIAL. src/arch/x86_64/boot/boot64.asm sets CR4.OSFXSR|OSXMMEXCPT, clears CR0.EM, sets CR0.MP|NE and runs fninit, so SSE/x87 instructions in user programs no longer raise #UD. Per-task FXSAVE/FXRSTOR state was NOT added: it needs cpu_context_t in src/include/kernel/task.h (outside this batch) to grow an aligned 512-byte area. 验证方式：boot regression only (x86_64 boot and user shell; no user program using floating point was run)
- 位置：`src/arch/x86_64/boot/boot64.asm:88`；相关：`user/lib/Makefile:25`、`user/shell/Makefile:24`、`user/helloworld/Makefile:24`、`src/arch/x86_64/include/context64.h:50`
- 证据：boot64.asm 只设置 CR4.PAE(88-90),从不置位 CR4.OSFXSR(bit9)/OSXMMEXCPT(bit10),也不做 CR0.MP 配置或分配 FXSAVE 区;内核其它地方也无 SSE 初始化(全仓仅 boot64.asm 命中 cr4)。而 user/lib、user/shell、user/helloworld 的 x86_64 ARCH_CFLAGS 为 -m64 -mcmodel=large -mno-red-zone,未加 -mno-sse,生成的 printf 变参序言含 movaps %xmm0..%xmm7(hello.elf 中 0x1003ad 起)。OSFXSR=0 时任何 SSE 指令产生 #UD。此外没有任何 per-task 的 FPU/SSE 上下文保存,context64 结构也不含 xmm/FX 区。
- 触发场景：用户调用形如 printf("%f", d) 或任何向变参函数传浮点/向量参数的代码,序言处 movaps %xmm0 立即 #UD;#UD 走 isr64_handler 未注册分支 → cli;hlt 整机停机(见上一条)。即便 OSFXSR 被启用,缺少 per-task xmm 保存也会导致多个使用 FP 的任务相互破坏寄存器状态。
- 修复方向：在 boot64 或 hal::Cpu::init 中设置 CR0.MP=1、CR0.EM=0、CR4.OSFXSR=1、CR4.OSXMMEXCPT=1 并执行 fninit;为任务上下文增加对齐的 FXSAVE 区并在上下文切换时 fxsave/fxrstor(或实现惰性 FPU);或在用户/内核构建中统一加 -mno-sse 并确认不生成向量指令。
- 复核意见：全仓 src 下只有 boot64.asm:88-90 触及 CR4 且只置 PAE，没有 OSFXSR/OSXMMEXCPT、fninit 或 fxsave/fxrstor；内核 Makefile:62 用 -mno-sse，而 user/lib、user/shell、user/helloworld 的 Makefile:24-25 的 x86_64 标志没有 -mno-sse。objdump 显示 user/shell/build/x86_64/shell.elf 与 hello.elf 各含 32 条 xmm 指令（4 个变参函数序言各 8 条 movaps）。这些指令受 test al,al 保护，只有调用方传浮点参数时才执行，所以现有用户程序能跑，但任何使用浮点或被编译器向量化的用户代码会 #UD，并经 isr64.cpp:112 停机。 更正：严重度下调为 medium：当前用户程序不传浮点参数，xmm 指令全部位于 al!=0 才执行的变参序言中，属潜伏问题。证据中的路径应为 user/helloworld/build/x86_64/hello.elf（根目录的 hello.elf 是 i386 版本）。

#### V023 [medium·待确认·x86_64·已修复] x86_64: 用 SYSRET 返回但不校验 RCX 是否规范地址(execve 把未校验的 ELF 入口写入返回帧)

- **修复**：src/arch/x86_64/syscall/syscall64_asm.asm: before SYSRET the saved user RIP is checked (shr 47 must be 0); otherwise the process is terminated with task_exit(128+11) while still on the kernel stack. This covers a non-canonical ELF entry written by execve and a syscall at the top of the user half. The ELF loader's e_entry validation (src/kernel/elf.cpp) was not touched. 验证方式：boot regression only (normal syscall return path exercised by the x86_64 user shell; the bad-RIP branch was not exercised)
- 位置：`src/arch/x86_64/syscall/syscall64_asm.asm:211`；相关：`src/kernel/syscalls/process.cpp:650`、`src/kernel/elf.cpp:250`
- 证据：`pop rcx ; user RIP` ... `pop rsp` ... `o64 sysret`，对 RCX 没有任何检查。process.cpp:650 `frame[12] = entry_point;` 把 ELF 的 e_entry(elf.cpp:250 `*entry_point = (uintptr_t)ehdr->e_entry;` 未做范围校验)直接写入将被 SYSRET 使用的 RCX 槽。Intel CPU 上 RCX 非规范时 SYSRET 在 ring0 产生 #GP，而此时 RSP 已是用户栈。
- 触发场景：用户 execve 一个 e_entry=0x8000000000000000(或任何非规范地址)的 ELF：系统调用返回时 sysret #GP 发生在 ring0、RSP=用户栈，异常帧压到用户栈上并被当作内核态 GPF 处理 → 内核崩溃；若攻击者能控制该时刻的 RSP(同第 2 条)则变成内核内存写。未验证项：ELF 加载器是否在别处拒绝入口不在已加载段内的映像(elf.cpp 中未见)。
- 修复方向：SYSRET 前检查 RCX 为规范的用户地址(否则走 iretq 路径或杀进程)；ELF 加载时校验 e_entry 落在用户空间可执行段内。
- 复核意见：syscall64_asm.asm:198-211 在 o64 sysret 前对 RCX 无任何规范性检查；process.cpp:650 把 entry_point 直接写入 frame[12]，而 x86_64 的 elf_load_impl (elf.cpp:96-147) 和 validate_header (elf.cpp:30-52) 只检查 ELF 类型/机器及段地址，从不校验 e_entry，execve 也只判断 entry_point != 0 (process.cpp:404-405)。因此非规范 e_entry 能到达 SYSRET；但 ring0 #GP 只在 Intel 真机/KVM 上发生（AMD 在 ring3 报错，QEMU TCG 的 sysret 实现也未必做该检查），这一环无法静态验证。 更正：x86_64 加载器写入入口点的位置是 src/kernel/elf.cpp:147，报告引用的 250 行是 ARM64 加载器。除 execve 外，在用户空间最高规范页末尾执行 syscall（RCX=0x0000800000000000）是另一条不依赖恶意 ELF 的触发路径，取决于能否在该地址映射可执行页（arch_types.h:46 的 USER_SPACE_END 为 0x00007FFFFFFFFFFF）。修复：sysret 前检查 RCX 规范性，不满足则走 iretq，并在 ELF 加载时校验 e_entry 落在已加载的可执行段内。


### 架构层：i686（3 项）

#### V024 [critical·已确认·i686·已修复] 用户态触发的任何 CPU 异常（除零/#GP/#PF/#UD 等）都会让整个内核 cli;hlt 停机

- 位置：`src/arch/i686/interrupt/isr.cpp:105`；相关：`src/arch/i686/interrupt/isr.cpp:166`、`src/arch/i686/interrupt/isr.cpp:207`、`src/arch/i686/interrupt/isr.cpp:316`
- 证据：isr_handler 的默认分支在打印 "KERNEL PANIC" 后直接 `__asm__ volatile("cli; hlt"); for(;;);`（105-106 行），虽然计算了 `from_usermode = (regs->cs & 0x3) == 3` 但只用于打印。isr_init 只注册了 8/13/14 三个专用处理函数（316-318 行），其中 page_fault_handler 在 COW 处理失败后同样 `cli; hlt`（166 行），general_protection_fault_handler 也是 `cli; hlt`（207 行）。全仓库没有任何其它 isr_register_handler 调用，也没有“杀死当前进程并调度”的路径。
- 触发场景：任意用户程序执行 `int x = 1/0;`、`*(int*)0 = 1;`（空指针）、`hlt`/`cli`/`in`/`out`（IOPL=0 触发 #GP）、`int3`/`int $0x30`（门 DPL=0 触发 #GP）、非法指令、用户栈越过已映射的 1MB，都会进入上述处理函数，内核关中断并永久 hlt，所有进程和 shell 一起死掉。系统调用里对坏用户指针的解引用（内核态 #PF）走的也是同一条停机路径。
- 修复方向：在 isr_handler / page_fault_handler / general_protection_fault_handler 中区分来源：`(regs->cs & 3) == 3` 时打印诊断信息后把当前任务标记为被信号终止（设置 exit_signaled/exit_code）并调用 task_exit() 切走，只有内核态异常才 panic；对内核态访问用户指针的缺页应通过 copy_from_user/异常表返回 -EFAULT。
- 复核意见：src/arch/i686/interrupt/isr.cpp:76-107 默认分支、:166 page_fault_handler、:207 general_protection_fault_handler 均在打印后无条件 `cli; hlt`，from_usermode 只用于打印。全仓库 i686 上的 isr_register_handler 调用只有 isr.cpp:316-318 三处（hal.cpp:113 只是通用封装，仅用于注册定时器 IRQ 0），不存在杀死当前进程的路径。因此 ring3 的 #DE/#UD/#GP/#PF 都会让整机停机，属于用户态可触发的内核挂死。 更正：“系统调用里对坏用户指针解引用也走同一停机路径”和“栈越过 1MB”两条子场景未逐一追踪，但不影响主结论。x86_64 的 isr64.cpp:112/168/207 存在完全相同的问题，修复应同时覆盖。
- 被 7 个独立审计者重复报告（arch-i686-core-1, x-doc-drift-2, arch-i686-mm-1, gap2-i686-fork-cow-refcount-2, mm-vmm-3, x-error-init-1, x-process-lifecycle-8）

#### V025 [critical·已确认·i686·已修复] 缺页处理不检查错误码就调用 handle_kernel_page_fault：对内核地址的任何保护性缺页都被“假修复”，陷入无限缺页循环（用户态一条指令即可挂死系统）

- **修复**：src/arch/i686/interrupt/isr.cpp: page_fault_handler now calls handle_kernel_page_fault only for kernel-mode, not-present faults ((err_code & 0x5) == 0). src/mm/vmm.cpp: handle_kernel_page_fault returns false when the current directory already holds the same PDE, so a missing PTE under an existing PDE reaches the COW / kill-process / panic paths instead of retrying forever. 验证方式：boot regression only (i686 boot and full test suite pass; the user-mode trigger was not exercised)
- 位置：`src/arch/i686/interrupt/isr.cpp:119`；相关：`src/mm/vmm.cpp:428`、`src/mm/vmm.cpp:430`
- 证据：page_fault_handler 第一步无条件执行 `if (mm::Vmm::handle_kernel_page_fault(faulting_address)) { return; }`，没有看 regs->err_code 的 P/U 位。handle_kernel_page_fault（src/mm/vmm.cpp:421-438）只要 `addr >= KERNEL_VIRTUAL_BASE` 且主内核页目录 `k_dir->entries[pd_idx] & PAGE_PRESENT` 就把 PDE 拷到 current_dir 并 `return true`，既不检查当前页目录里该 PDE 是否本来就存在，也不检查这是“页不存在”还是“权限违规”。Vmm::init 已为全部物理内存建立了内核 PDE，所以几乎所有内核地址都满足该条件。
- 触发场景：用户程序执行 `*(volatile char*)0x80100000;`（或跳转到内核地址）：产生 err_code=0x5（P=1,U=1）的 #PF，handle_kernel_page_fault 拷贝一个本来就相同的 PDE 后返回 true，iret 回到同一条指令再次缺页，如此无限循环；由于没有抢占（schedule_from_irq 为空），其它任务永远得不到运行，系统无任何报错地挂死。内核态同理：访问某个 PDE 存在但 PTE 不存在的内核地址（越界的堆指针、已 unmap 的页）时不会打印 PAGE FAULT 现场，而是关中断无限缺页，问题被伪装成死机。
- 修复方向：仅当 `(err_code & 0x1) == 0`（页不存在）且 `(err_code & 0x4) == 0`（内核态）并且 current_dir 中对应 PDE 确实缺失/与主目录不同步时才做同步；其余情况返回 false，交给后续的 COW/杀进程/panic 逻辑。
- 复核意见：isr.cpp:119 在查看 err_code 之前无条件调用 handle_kernel_page_fault；src/mm/vmm.cpp:421-438 只要 addr>=KERNEL_VIRTUAL_BASE 且 boot_page_directory 对应 PDE present 就复制 PDE 并返回 true，不区分 present/权限违规，也不区分用户态/内核态。用户态读写内核地址产生 P=1,U=1 的 #PF 时会被“假修复”后重试同一指令，形成无限缺页；而 src/kernel/task.cpp:1341-1352 的 schedule_from_irq 为空、task.cpp:1149-1153 时间片到期也不调度，所以没有抢占能打破循环，系统静默挂死。 更正：该分支仅在 ARCH_I686 下编译（vmm.cpp:414），x86_64/arm64 直接返回 false。即使修好此处，在 arch-i686-core-1 未修复前该访问仍会落入 cli;hlt，两者需一并修复；修复时还应判断 current_dir 的 PDE 是否与主目录确实不同。

#### V026 [critical·已确认·i686,x86_64·已修复] 用户态异常在 i686/x86_64 上一律停机，而不是终止出错进程

- 位置：`src/arch/i686/interrupt/isr.cpp:166`；相关：`src/arch/i686/interrupt/isr.cpp:105`、`src/arch/i686/interrupt/isr.cpp:207`、`src/arch/x86_64/interrupt/isr64.cpp:112`、`src/arch/x86_64/interrupt/isr64.cpp:168`、`src/arch/x86_64/interrupt/isr64.cpp:207`、`src/arch/arm64/interrupt/exception.cpp:406`
- 证据：page_fault_handler 在 COW 与内核页同步都未处理时，无论 from_usermode 与否都执行 cli; hlt；general_protection_fault_handler 和未注册异常的默认分支同样停机。from_usermode 只用于打印。arm64 对 EL0 的 data/instruction abort 会调用 arm64_terminate_user_process，但其余来自 EL0 的同步异常仍走到 exception.cpp:406 的停机循环。
- 触发场景：用户程序的普通错误（空指针访问、除零、非法指令等）会让整个系统停止，而不是只结束该进程。
- 修复方向：在各异常处理入口判断来源特权级：来自用户态时设置 exit_signaled/exit_signal 并调用 task_exit(128+sig)，仅内核态不可恢复异常才 panic。arm64 的 default、对齐、BRK 等分支也按来源区分。
- 复核意见：src/arch/i686/interrupt/isr.cpp:68-108 的默认分支、page_fault_handler（约 115-168 行）和 general_protection_fault_handler（约 173-210 行）在打印后都无条件执行 cli; hlt; for(;;)，from_usermode 只用于打印。src/arch/x86_64/interrupt/isr64.cpp 结构相同（默认分支约 112 行、缺页约 168 行、GPF 约 207 行均为 cli; hlt），两个文件里都没有任何 task_exit 调用。arm64 的 src/arch/arm64/interrupt/exception.cpp:300-376 只对 EL0 的指令/数据 abort 调用 arm64_terminate_user_process，PC/SP 对齐、BRK 和 default 分支都落到 404-409 行的 wfi 停机循环。因此任何用户程序的空指针访问、除零或非法指令都会让整机停止，属于用户态可触发的内核挂起。 更正：引用的 isr.cpp:166 是缺页处理函数里的停机点，描述准确。arm64 部分只是次要问题（仅非 abort 类的 EL0 同步异常停机）。修复时注意 x86 缺页处理要先走 COW 和内核页同步，再按 regs->cs & 3 区分来源。


### 内存管理（12 项）

#### V027 [critical·已确认·arm64·已修复] munmap/brk 收缩把用户地址范围内任何存在的映射都当作进程私有页释放，arm64 上会释放内核恒等映射块对应的物理帧

- **修复**：The syscall layer was already fixed (unmap_user_page checks HAL_PAGE_USER). I fixed the root in src/mm/vmm.cpp: unmap_page_in_directory() now returns what hal::Mmu::unmap removed and returns 0 when the HAL refuses (2MB/1GB blocks), instead of returning the query() address for the caller to free. munmap also takes uintptr_t/size_t with a wrap-safe range check. 验证方式：unit test test_vmm_unmap_block_mapping_returns_zero (cow_flag_test.cpp, so it runs on 64-bit): x86_64 against a 2MB mapping it creates, arm64 against the existing 1GB boot block. The munmap syscall path is boot regression only.
- 位置：`src/kernel/syscalls/mm.cpp:469`；相关：`src/kernel/syscalls/mm.cpp:106`、`src/mm/vmm.cpp:1247`、`src/arch/arm64/mm/mmu.cpp:1082`、`src/arch/arm64/mm/mmu.cpp:781`
- 证据：munmap 循环：`uint32_t phys = mm::Vmm::unmap_page_in_directory(current->page_dir_phys, page); if (phys) { mm::Pmm::free_frame(phys); }`。unmap_page_in_directory（src/mm/vmm.cpp:1242-1254）先用 hal::Mmu::query 取得物理地址，随后调用 hal::Mmu::unmap 但忽略其返回值，也不检查 HAL_PAGE_USER。arm64 的 create_space（src/arch/arm64/mm/mmu.cpp:1074-1084）在每个用户 TTBR0 表里放了内核专用映射：L2[64]/L2[72] 设备 2MB 块，以及 L1[1..3] = 0x40000000-0xFFFFFFFF 的 1GB 恒等块。query 对这些块返回 true 和物理地址，unmap 报错“cannot unmap 1GB block”后返回失败，但 phys 仍被返回并交给 free_frame。munmap 的范围检查只要求 < USER_SPACE_END（arm64 为 0x0000800000000000）。arm64 上没有任何帧处于 protect 列表（protect_frame 只在 i686 路径使用）。
- 触发场景：arm64 上任意用户进程对 0x40000000 以上（RAM 恒等映射区）或 0x08000000/0x09000000（设备块）范围调用 munmap：每一页 query 成功、unmap 失败，但 free_frame 仍把该物理帧的引用计数减到 0 并标记为空闲。内核镜像、PMM 位图、页表等帧随后会被 alloc_frame 重新分配并清零，内核崩溃；若被释放的是低于 0x40000000 的帧号，alloc_frame 每次都会先找到它再因范围检查失败返回 PADDR_INVALID，整个系统此后无法分配物理页。brk 收缩路径（mm.cpp:106）有同样的写法。
- 修复方向：unmap_page_in_directory 应以 hal::Mmu::unmap 的返回值为准（PADDR_INVALID 时返回 0），并在 query 时取 flags，只有带 HAL_PAGE_USER 的 4KB 叶子映射才允许解除映射和释放；根本上应为每个进程维护 VMA 列表，munmap/brk 只操作登记过的区域，同时不要把内核映射放进用户 TTBR0 地址范围。
- 复核意见：src/mm/vmm.cpp:1229-1254 的 unmap_page_in_directory 先 hal::Mmu::query 取 old_phys，再调用 hal::Mmu::unmap 但忽略返回值，最后无条件返回 old_phys；arm64 query 对 1GB 块返回 true（src/arch/arm64/mm/mmu.cpp:605-613），unmap 对块只报错返回 PADDR_INVALID（mmu.cpp:781）。create_space 确实把 L1[1..3] 的内核 RAM 块和 L2[64]/L2[72] 设备块放进每个用户 TTBR0（mmu.cpp:1074-1084），而 munmap 只检查 < USER_SPACE_END（mm.cpp:459，arm64 为 0x0000800000000000，task.h:32）。arm64 PMM 初始化把内核/位图帧标记为已用且 refcount=1、未加入 protected 表（pmm.cpp:520-605），所以 free_frame（pmm.cpp:938-968）会把 refcount 减到 0 并 clear_frame，内核镜像帧随后可被 alloc_frame 重新分配。 更正：munmap 的参数是 uint32_t addr/length（mm.cpp:437），phys 也被截成 uint32_t，因此可达范围是 4GB 以内，恰好覆盖 0x40000000-0xFFFFFFFF 的内核恒等块和 0x08000000/0x09000000 设备块。内核帧 refcount 为 1（不是 0），free_frame 走正常递减路径，不会打印告警。brk 收缩路径（mm.cpp:105-110）受 heap_start/heap_end 约束，实际较难指向内核块，主要入口是 munmap。
- 被 2 个独立审计者重复报告（mm-heap-mmap-2, x-doc-drift-4）

#### V028 [critical·已确认·i686·已修复] kmalloc/expand 的大小计算没有溢出检查，i686 上超大请求会绕过 heap_max 检查并破坏内核直接映射

- **修复**：expand() and the kmalloc entry check were already wrap-safe from commit 0db9387. The remaining gap was kmalloc_aligned(): size + extra could wrap and return a block far smaller than requested; it now returns NULL. The ramfs_write size limit suggested in the entry is outside my ownership and was not touched. 验证方式：unit test test_kmalloc_aligned_overflow (heap_test.cpp), all three architectures; the existing test_heap_alloc_huge_size covers the kmalloc part
- 位置：`src/mm/heap.cpp:36`；相关：`src/mm/heap.cpp:35`、`src/mm/heap.cpp:265`、`src/mm/heap.cpp:289`、`src/mm/heap.cpp:295`、`src/mm/heap.cpp:416`、`src/fs/ramfs.cpp:166`
- 证据：`size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE; if (heap_end + pages * PAGE_SIZE > heap_max) return false;` 在 32 位下两处都会回绕：size≈0x80000000 时 heap_end(0x80xxxxxx)+pages*PAGE_SIZE 回绕成很小的值，检查通过；size>=0xFFFFF000 时 size+sizeof(heap_block_t)+4095 回绕使 pages=0，expand 返回 true，kmalloc 随后在未扩展的 heap_end 处写块头并算出 `new_block->size = heap_end - old - sizeof(heap_block_t)`（负数回绕）。另外 265 行 `size = (size + 3) & ~3` 对 size>0xFFFFFFFC 回绕为 0，416 行 kmalloc_aligned 的 `size + extra` 同样未检查。
- 触发场景：用户可达路径（已读 fs.cpp 的 lseek/write、vfs.cpp、ramfs.cpp:166-174）：lseek 只拒绝 (int32_t)offset<0，偏移可到 0x7FFFFFFF；ramfs_write 计算 `new_capacity = (offset + size + 4095) & ~4095` 后直接 kmalloc(new_capacity)。请求约 2GB 时 heap_max 检查因回绕而通过，expand 循环不断 alloc_frame 并把 heap_end 之后的虚拟页重新映射，越过 32MB 保留区后覆盖其它物理帧在直接映射区的别名（PHYS_TO_VIRT 访问到错误的帧）；物理内存耗尽后清理代码只 unmap 而不恢复恒等映射，直接映射区留下空洞，后续内核通过 PHYS_TO_VIRT 访问页表或页帧时缺页崩溃。未验证的部分：ramfs 文件在 i686 默认启动配置下是否对普通用户进程可写。
- 修复方向：在 kmalloc 入口拒绝超过堆上限的请求（例如 `if (size > heap_max - heap_start - sizeof(heap_block_t)) return NULL;`），expand 中改用不回绕的比较 `pages > (heap_max - heap_end) / PAGE_SIZE`，kmalloc_aligned 检查 `size > SIZE_MAX - extra`；ramfs_write 也应限制 offset+size 的上限并检查加法溢出。
- 复核意见：src/mm/heap.cpp:35-36 的 `heap_end + pages * PAGE_SIZE > heap_max` 在 i686（size_t 32 位，heap_end 位于 0x80000000 以上）确实会回绕；265 行的对齐与 289 行的 `size + sizeof(heap_block_t)` 也无溢出检查。i686 分支（98-128 行）随后逐页 alloc_frame 并 map_page，而 src/arch/i686/mm/paging.cpp:624 的 hal::Mmu::map 直接覆盖已有 PTE、不检查是否已映射，因此越过 32MB 堆保留区（kernel.cpp:480-485）后会改写直接映射区的 PTE，OOM 后的回滚（heap.cpp:104-109）只 unmap、不恢复原映射。用户路径成立：syscalls/fs.cpp:376 的 lseek 只拒绝负偏移，ramfs.cpp:166-174 以 `(offset+size+4095)&~4095` 直接 kmalloc，偏移 0x7FFFFFFF 写 1 字节即请求 0x80000000 字节。 更正：可达性取决于根文件系统：fs_bootstrap.cpp:85-104 优先挂 FAT32，仅在无磁盘（如 `make run` 用 -kernel 直接启动）时才回退到 ramfs 作为根，此时用户进程可触发；带磁盘启动时 ramfs 不是根，该路径不可达。size>=0xFFFFF000 使 pages=0 的子场景经 ramfs 无法到达（new_capacity 最大 0x80000000），仅是内核内部调用者的潜在问题。x86_64/arm64 上 size_t 为 64 位且 ramfs 的大小是 uint32_t，不受影响。

#### V029 [critical·已确认·all·已修复] split() 分裂末尾块时不更新 last_block，堆扩展后链表断裂，导致块丢失和块重叠

- **修复**：src/mm/heap.cpp split(): when the block being split is last_block, the remainder becomes last_block, so heap expansion no longer drops the remainder from the list. Added mm::Heap::verify(), which walks the list and checks every block ends exactly where the next begins and the tail ends at heap_end. 验证方式：unit test test_heap_split_tail_then_expand (heap_test.cpp), passes on all three architectures
- 位置：`src/mm/heap.cpp:202`；相关：`src/mm/heap.cpp:299`、`src/mm/heap.cpp:155`、`src/mm/heap.cpp:170`
- 证据：split() 中 `new_block->next = b->next; ... b->next = new_block; b->size = size;`，当 b 就是 last_block 时，新分裂出的 new_block 才是真正的末尾块，但 last_block 没有被更新。kmalloc 扩展路径（299-305 行）却依赖它：`new_block->prev = last_block; last_block->next = new_block; last_block = new_block;`，这会把旧 last_block 的 next 直接改写为扩展块，旧 last_block 与扩展块之间的所有块从正向链表中消失。Heap::init 后的第一次 kmalloc 就会进入这个状态（first_block == last_block 被分裂）。
- 触发场景：1) 末尾块 L 空闲，kmalloc(小) 取 L 并分裂为 L -> R，last_block 仍是 L；2) 后续分配从 R 中切出 B1、B2（在用）；3) 一次较大的分配找不到空闲块，expand 后 E->prev = L，L->next = E，B1/B2/剩余块脱链；4) 之后 L 与 E 都被释放，coalesce 按“相邻”假设执行 `L->size += sizeof(heap_block_t) + E->size`，L 的范围覆盖仍在使用的 B1/B2；5) 下一次 kmalloc 从 L 分配，返回的内存与 B1/B2 重叠，内核对象互相覆盖。即使不发生重叠，脱链的块释放后也永远无法被 first_block 起始的遍历找到，属于永久泄漏。
- 修复方向：在 split() 中加入 `if (b == last_block) last_block = new_block;`；更稳妥的做法是 kmalloc 扩展时不依赖 last_block，而是用遍历得到的真正尾块，并在调试构建中校验 `(uintptr_t)b + sizeof(*b) + b->size == (uintptr_t)b->next`。
- 复核意见：src/mm/heap.cpp:186-205 的 split() 在 b==last_block 时确实不更新 last_block，而 kmalloc 扩展路径 299-305 行用 `last_block->next = new_block` 直接覆盖旧尾块的 next，使 split 出的余块及其后续切分块从正向链表脱链。之后 coalesce（158、173 行）按链表相邻而非地址相邻合并：旧尾块 L 与扩展块 E 都空闲时 `L->size += sizeof(heap_block_t) + E->size`，L 的范围就覆盖了物理上紧随其后、仍在使用的脱链块，下一次从 L 分配即与之重叠。该代码三个架构共用，Heap::init 后第一次 kmalloc 就进入 first_block==last_block 被分裂的状态，且任何被释放后再次以更小尺寸复用的尾块都会重复触发。 更正：触发不限于 init 后的首块：扩展得到的块 E 以整页大小返回且不分裂（295 行），E 被释放后再被较小请求复用时在 281 行分裂，此时 E 仍是 last_block，同样的断链会再次发生，因此在正常运行中会反复出现。修复建议正确：在 split() 中加 `if (b == last_block) last_block = new_block;`。

#### V030 [critical·已确认·i686·已修复] map_page_in_directory / hal::Mmu::map 不校验用户/内核地址范围，带 PAGE_USER 的映射可改写共享内核页表（ELF 段只检查起始地址）

- **修复**：VMM-level enforcement only: mm::Vmm::map_page() and map_page_in_directory() refuse PAGE_USER mappings at or above VMM_USER_VADDR_END (KERNEL_VIRTUAL_BASE on i686, 0x0000800000000000 on x86_64/arm64). An ELF segment crossing the kernel base can no longer replace shared kernel PTEs. The ELF loader's own segment-end and overflow checks (src/kernel/elf.cpp) are outside my ownership and not added. 验证方式：unit test test_vmm_user_mapping_rejected_in_kernel_space (cow_flag_test.cpp), all three architectures; no test loads a crossing ELF
- 位置：`src/mm/vmm.cpp:1203`；相关：`src/arch/i686/mm/paging.cpp:618`、`src/arch/i686/mm/paging.cpp:624`、`src/kernel/elf.cpp:275`、`src/kernel/elf.cpp:283`
- 证据：`bool mm::Vmm::map_page_in_directory(uintptr_t dir_phys, uintptr_t virt, uintptr_t phys, uint32_t flags)` 只检查页对齐。i686 的 hal::Mmu::map（paging.cpp:612-624）在 PDE 已存在时直接 `*pde |= PAGE_USER; table->entries[pt_idx] = (uint32_t)phys | i686_flags;`。内核半区的页表被所有页目录共享，因此对 >=0x80000000 的地址做用户映射会改写所有地址空间的内核映射。调用方 elf.cpp:275 只检查 `ph->p_vaddr >= KERNEL_VIRTUAL_BASE`，elf.cpp:283 的 `vaddr_end = PAGE_ALIGN_UP(ph->p_vaddr + ph->p_memsz)` 没有上界检查。
- 触发场景：execve 加载一个 PT_LOAD 段起始在用户区、但 p_vaddr+p_memsz 越过 0x80000000 的 ELF：加载循环对越界的每一页调用 map_page_in_directory，共享的内核页表项被替换为新分配的用户可访问帧，内核映像/直接映射的页被换掉。下一次 TLB 失效后内核执行或访问到错误内容，系统崩溃；同时用户/内核隔离被破坏。
- 修复方向：在 VMM 层强制策略：带 PAGE_USER 的映射要求 virt < USER_SPACE_END，且用户接口不允许映射内核半区；ELF 加载器同时检查段结束地址和 p_vaddr+p_memsz 溢出；hal::Mmu::map 不应给内核 PDE 加 PAGE_USER。
- 复核意见：elf.cpp:275 只检查 p_vaddr >= KERNEL_VIRTUAL_BASE，elf.cpp:283 的 vaddr_end = PAGE_ALIGN_UP(p_vaddr + p_memsz) 没有上界和溢出检查，循环对每页调用 map_page_in_directory；vmm.cpp:1203-1227 只检查页对齐，paging.cpp:612-624 在 PDE 已存在时给 PDE 加 PAGE_USER 并直接覆盖 PTE，没有任何用户/内核范围校验。i686 内核半区页表由所有页目录共享，因此一个 p_vaddr 略低于 0x80000000、p_memsz 跨界的 ELF 经 execve 即可把内核映像/直接映射的 PTE 换成用户可访问的新帧，导致内核崩溃并破坏隔离，用户态可达。 更正：elf.cpp:120 和 :204 的 64 位加载器同样没有段结束地址检查，应一并修复（在 x86_64/arm64 上的具体后果未逐一追踪）。修复时还应检查 p_filesz <= p_memsz 以及 p_offset + p_filesz 的整数溢出。

#### V031 [high·已确认·arm64·已修复] MMAP_REGION_START/END 硬编码为 i686 布局 (0x40000000-0x70000000)，在 arm64 上正好落在内核 RAM 的 1GB 块映射上，mmap 在 arm64 上必然失败

- **修复**：src/kernel/syscalls/mm.cpp: the mmap region is per-architecture; arm64 uses 0x100000000-0x140000000, clear of the kernel 1GB blocks and device blocks. mmap/munmap signatures changed to uintptr_t addr / size_t length (syscall.cpp already passed those types). 验证方式：boot regression only: no in-kernel test calls mmap and test.sh only execs hello.elf on arm64, so arm64 mmap was never actually run
- 位置：`src/kernel/syscalls/mm.cpp:22`；相关：`src/arch/arm64/mm/mmu.cpp:1082`、`src/arch/arm64/mm/mmu.cpp:720`、`src/include/kernel/task.h:35`
- 证据：`#define MMAP_REGION_START 0x40000000` / `#define MMAP_REGION_END 0x70000000`。arm64 的 hal::Mmu::create_space (src/arch/arm64/mm/mmu.cpp:1082-1084) 把内核 RAM 的 1GB 块原样拷进每个用户地址空间：`new_l1[1] = current_l1[1]; new_l1[2] = ...; new_l1[3] = ...`（0x40000000-0xFFFFFFFF）。hal::Mmu::map 遇到块描述符直接失败 (mmu.cpp:720 `cannot map 4KB page over 1GB block`)。
- 触发场景：arm64 用户程序调用 mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)：find_free_vaddr 返回 0x40000000（见上一条，检查恒为空闲），do_mmap_anonymous 调 map_page_in_directory -> hal::Mmu::map 因 L1[1] 是 1GB 块而返回 false，mmap 返回 -1。arm64 上任何 mmap 都无法成功。未验证项：未实际运行，依据是 create_space 中的代码和注释。
- 修复方向：把 mmap 区域、用户栈顶、USER_SPACE_END 等用户地址空间布局常量移到各架构的 arch_types.h；arm64 需要选择一个不与内核恒等映射冲突的区间（或把内核迁到 TTBR1 高半区后再统一布局）。
- 复核意见：src/kernel/syscalls/mm.cpp:22-23 无架构分支地把 mmap 区硬编码为 0x40000000-0x70000000；arm64 的 start.S:351-359 把 L1[1..3] 建成 1GB 块，mmu.cpp:1082-1084 的 create_space 把它们原样拷进每个用户地址空间。mm::Vmm::map_page_in_directory（vmm.cpp:1203-1219）转发到 hal::Mmu::map，而 mmu.cpp:720 遇到 L1 块描述符直接返回 false，所以该区间内任何地址的 mmap 都会失败并返回 -1。user/tests/tests.cpp:190 的 mmap 测试在 arm64 上会走到这条路径。 更正：无论 find_free_vaddr 返回区间内哪个地址结论都成立，因为整个区间都落在 L1[1] 的 1GB 块内；后果是 arm64 上 mmap 功能不可用（返回 -1），不是崩溃。未实际运行验证。

#### V032 [high·已确认·x86_64,arm64·已修复] mmap 的空闲区查找按 i686 两级页表格式遍历页目录，在 x86_64/arm64 上读到的是无关表项

- **修复**：is_vaddr_range_free()/find_free_vaddr() no longer walk the page directory in i686 format; they call hal::Mmu::query on the current address space. The search skips past the last mapped page in a candidate range instead of advancing one page at a time. 验证方式：boot regression only (i686, x86_64, arm64); the mmap syscall is not exercised by any test in the run
- 位置：`src/kernel/syscalls/mm.cpp:132`；相关：`src/kernel/syscalls/mm.cpp:179`、`src/kernel/syscalls/mm.cpp:22`、`src/arch/arm64/mm/mmu.cpp:1082`
- 证据：is_vaddr_range_free 没有架构分支：`uint32_t pd_idx = addr >> 22; uint32_t pt_idx = (addr >> 12) & 0x3FF; ... pd->entries[pd_idx] ... pt->entries[pt_idx]`。x86_64 上 pd 是 512 项的 PML4，0x40000000>>22=256，读到的是内核半区的 PML4[256]，再把内核 PDPT 当作页表用 0..1023 下标访问（超过 511 时越界读到相邻页）。结果与用户实际映射无关。
- 触发场景：x86_64 进程连续两次 mmap 同样大小的匿名区：find_free_vaddr 两次返回同一地址，第二次 map_page_in_directory 直接覆盖第一次的 PTE，第一次映射的帧泄漏，程序认为是两块内存的区域实际是同一块，用户数据互相覆盖。arm64 上查找总返回 0x40000000，而该地址在每个用户地址空间里是内核 1GB 块映射，hal::Mmu::map 报 “cannot map 4KB page over 1GB block”，mmap 总是失败。
- 修复方向：用 hal::Mmu::query(task->page_dir_phys, addr, ...) 判断是否已映射，或为进程维护 VMA 列表；MMAP 区间按架构选择，避开 arm64 的内核恒等映射范围。
- 复核意见：src/kernel/syscalls/mm.cpp:132-152 的 is_vaddr_range_free 没有任何 #if，固定用 addr>>22 和 (addr>>12)&0x3FF 做两级遍历；而 vmm.h:85-96 在 x86_64/arm64 上 page_directory_t 是 512 项 64 位表。0x40000000>>22=256，在 x86_64 上读的是内核半区 PML4[256]（KERNEL_VIRTUAL_BASE=0xFFFF800000000000），再把 PDPT 当页表用，pt_idx 可达 1023 超出 512 项数组越界读；结果与用户实际映射无关。mm.cpp 在 Makefile:158/175 对所有架构编译，SYS_MMAP 在 syscall.cpp:569 注册。 更正：遍历错误本身已确认；x86_64 上“连续两次 mmap 返回同一地址并互相覆盖”的具体表现取决于内核 PDPT 的内容以及 x86_64 的 map 是否允许覆盖已有 PTE，未静态验证，属于合理推断。arm64 部分的失败原因与 x-64bit-2 重叠（1GB 块），那是另一个根因。修复建议用 hal::Mmu::query 是合适的。

#### V033 [high·已确认·x86_64,arm64·已修复] mmap 的空闲虚拟区间搜索 is_vaddr_range_free 按 i686 两级页表格式遍历，在 x86_64/arm64 上读到的是 PML4/L0 的无关项，从不检查真实映射

- **修复**：Same root cause and same change as mm-vmm-7 and x-64bit-2. 验证方式：boot regression only
- 位置：`src/kernel/syscalls/mm.cpp:134`；相关：`src/kernel/syscalls/mm.cpp:165`、`src/kernel/syscalls/mm.cpp:22`、`src/arch/arm64/mm/mmu.cpp:1082`
- 证据：`page_directory_t *pd = (page_directory_t *)PHYS_TO_VIRT(task->page_dir_phys); uint32_t pd_idx = addr >> 22; uint32_t pt_idx = (addr >> 12) & 0x3FF; if (!(pd->entries[pd_idx] & PAGE_PRESENT)) continue; ... pt = PHYS_TO_VIRT(pd->entries[pd_idx] & PAGE_MASK); if (pt->entries[pt_idx] & PAGE_PRESENT) return false;`。没有任何 ARCH 分支。在 64 位架构上 page_directory_t 是 512 项的顶级表：MMAP_REGION 0x40000000-0x70000000 对应 pd_idx 256..447，即 x86_64 的内核半区 PML4 项（只有 PML4[256] 存在，其“页表”其实是内核 PDPT，且 pt_idx 可达 1023 越过 512 项读到相邻物理页）；arm64 上是 TTBR0 L0 的 256 以后的项（通常为 0）。用户区真实的 PML4[0]/L0[0] 下的映射完全没被查看。
- 触发场景：x86_64：进程连续两次 mmap(NULL, 4096, RW, ANON) 而不 munmap。两次搜索读到同样的内核表内容，返回同一个虚拟地址；第二次 map_page_in_directory -> hal::Mmu::map 不检查已有 PTE，直接覆盖：第一次映射的物理帧泄漏，用户在第一块内存里的数据被换成全零页（静默数据丢失）。arm64：L0[256..] 为空 -> 总是返回 0x40000000，而 create_space 在每个用户地址空间的 L1[1] 放了 0x40000000 起的 1GB 内核块，hal::Mmu::map 报 “cannot map 4KB page over 1GB block”，mmap 永远失败。未验证项：x86_64 上内核 PDPT 及其后一页的具体内容（只影响返回哪个地址，不影响“重复返回同一地址”的结论）。
- 修复方向：用 hal::Mmu::query((hal_addr_space_t)task->page_dir_phys, addr, NULL, NULL) 逐页判断是否已映射，删除手写的两级遍历；arm64 还需要把 MMAP_REGION 移出被内核 1GB 块占用的 0x40000000-0x7FFFFFFF 区间。更稳妥的是为每个进程维护 VMA 列表而不是扫描页表。
- 复核意见：src/kernel/syscalls/mm.cpp:132-156 的 is_vaddr_range_free 无任何架构分支，用 addr>>22 / 0x3FF 遍历，而 src/include/mm/vmm.h:85-96 在 64 位上 page_directory_t 是 512 项 64 位表，0x40000000 对应下标 256，即 x86_64 的 PML4[256]（boot64.asm 中指向内核 PDPT，paging64.cpp create_space 把 PML4[256..511] 拷给每个进程）。判定只取决于内核 PDPT 内容（PDPT[0] present、PDPT[1..] 通常为空），与用户映射无关，所以 4KB 与 16KB 的请求都会得到 0x40001000；map_page_in_directory (vmm.cpp:1203) 直接调用 hal::Mmu::map，paging64.cpp:654 无条件覆盖 PTE，旧帧泄漏、数据变零。arm64 上 create_space (mmu.cpp:1082) 把 L1[1] 设为内核 1GB 块，map 在 mmu.cpp:720 附近返回 'cannot map 4KB page over 1GB block'，mmap 失败。 更正：arm64 的 L0[256..] 是否恒为 0 我没有逐行核对启动页表（未找到 boot.S），但无论其内容如何，结果都与用户映射无关，且 0x40000000-0x7FFFFFFF 被 1GB 内核块占用，mmap 在该区间必然失败或错判。x86_64 上具体返回地址取决于内核 PDPT[1..] 是否被后续内核映射填充。
- 被 7 个独立审计者重复报告（gap2-i686-fork-cow-refcount-3, arch-x86_64-mm-2, mm-heap-mmap-4, user-lib-5, x-64bit-1, x-doc-drift-5, x-user-boundary-10）

#### V034 [high·已确认·i686·已修复] i686：内核堆重映射直接映射区，使 Vmm::init 早期分配的内核页表失去 PHYS_TO_VIRT 别名，堆越过 0x81000000 后页表写入落到堆首页

- **修复**：mm::Heap::init on i686 now starts the heap after the highest frame already in use inside its shadow range (heap_max unchanged), so the direct-map page tables allocated by Vmm::init keep their PHYS_TO_VIRT alias. The boot log shows 'skipping 28 pages whose frames are already in use', matching the audit's PDE 516..543 calculation. Added mm::Heap::get_range(). 验证方式：unit test test_heap_range_excludes_page_tables (i686 only) checks no kernel page table's alias lies inside the heap range. The heap was not grown past 0x81000000 to reproduce the corruption.
- 位置：`src/mm/vmm.cpp:355`；相关：`src/kernel/kernel.cpp:456`、`src/kernel/kernel.cpp:485`、`src/mm/heap.cpp:114`、`src/arch/i686/mm/paging.cpp:615`
- 证据：Vmm::init 在 Heap::init 之前执行 `paddr_t table_phys = mm::Pmm::alloc_frame();` 为 PDE 516.. 分配页表，PMM 从最低空闲帧分配，即 refcount_end_phys（P0）起的连续帧。而 kernel.cpp:456 `heap_start = mm::Pmm::get_bitmap_end()` 正好等于 PHYS_TO_VIRT(P0)，heap.cpp:114 `mm::Vmm::map_page(heap_end + i * PAGE_SIZE, frame, ...)` 把这些直接映射虚拟页改映射到别的帧。kernel.cpp:485 的 set_heap_reserved_range 只阻止之后的分配，之前已分配的页表帧仍在该范围内。之后 paging.cpp:615 `table = (page_table_t*)PHYS_TO_VIRT((uintptr_t)table_phys);` 访问这些页表时实际访问的是堆页。按当前构建（_kernel_end=0x8027af64，128MB）计算：P0=0x28C000，PDE 516..543 的页表在 0x28C000..0x2A7000，堆页 0..27 正是它们的别名。
- 触发场景：内核堆增长到 heap_end 越过 0x81000000（约 13.45MB，例如向 ramfs 写入大文件、启用 framebuffer 双缓冲后再大量 kmalloc）：expand() 调 map_page(0x81000000+j*4K)，hal::Mmu::map 取 PDE 516 的页表物理地址 0x28C000，经 PHYS_TO_VIRT 得到 0x8028C000（堆第一页），把 PTE 写到 first_block 头部（偏移 0 是 size，4 是 is_free，8 是 next，16 是 magic）。结果：首块被标成“空闲且巨大”，后续 kmalloc 把正在使用的内存再次分配出去，或 magic 被破坏后 kmalloc 永久返回 NULL；真正的页表没有更新，分配的帧泄漏；virt_to_phys 对这些堆地址返回垃圾（DMA 地址错误）。未做动态验证；假设没有 multiboot 模块改变最低空闲帧位置。
- 修复方向：不要让堆占用直接映射区的虚拟地址：为堆分配独立的虚拟区间（例如直接映射区之后），或让 i686 堆像 x86_64 那样直接使用保留的物理区间而不重映射；至少在 Vmm::init 之前确定堆范围并先调用 set_heap_reserved_range，保证页表帧不落在堆的影子范围内。
- 复核意见：src/mm/vmm.cpp:355 在 Heap::init 之前用 Pmm::alloc_frame 为 PDE 516 起分配页表，find_free_frame(pmm.cpp:104) 从最低空闲帧即 refcount_end_phys 开始分配(pmm.cpp:349-350 把低于该地址的内存全部跳过)；kernel.cpp:456 的 heap_start=get_bitmap_end()=PHYS_TO_VIRT(refcount_end_phys)，heap.cpp:114 的 i686 expand 用 map_page 把这些直接映射虚拟页改指到新帧，而 set_heap_reserved_range(pmm.cpp:1025) 只对尚未分配的帧置位，之前分出的页表帧仍落在堆影子范围内。之后 paging.cpp:615 通过 PHYS_TO_VIRT 访问 PDE>=516 的页表时实际读写的是堆页，堆越过 0x81000000 时 PTE 会写进堆块。静态链路完整；具体 P0 数值和堆实际增长到约 13MB 未做动态验证，且 Makefile/grub 配置中未发现 multiboot 模块改变起点。 更正：触发条件是堆增长越过 0x81000000（约 13MB，取决于内核映像大小），常规启动与单元测试未达到，因此评为 high 而非 critical；同样受影响的还有对 >=0x81000000 内核地址的任何 map/unmap/query（不仅是堆扩展）。修复建议可行：在 Vmm::init 之前预留堆物理区间，或让 i686 堆像 x86_64 一样直接使用直接映射而不重映射。

#### V035 [high·待确认·arm64·已修复] arm64：switch_page_directory 切换 TTBR0 后不失效 TLB（无 ASID），execve 之后新程序仍可能使用旧地址空间的转换

- 位置：`src/mm/vmm.cpp:1190`；相关：`src/arch/arm64/mm/mmu.cpp:291`、`src/kernel/syscalls/process.cpp:496`、`src/kernel/user.cpp:55`
- 证据：`hal::Mmu::switch_space(dir_phys);`，arm64 实现（mmu.cpp:291-296）只有 `dsb_ish(); write_ttbr0_el1((uint64_t)space); isb(); /* TLB invalidation is typically done by the caller if needed */`，而 VMM 和 execve（process.cpp:496）都没有调用 flush_tlb_all。所有进程的 TTBR0 ASID 字段都是 0，用户页是 nG 项，因此切换 TTBR0 不会自动隔离旧项。上下文切换汇编里有 tlbi vmalle1is，但 execve 路径不经过它。
- 触发场景：fork 出的子进程调用 execve：新旧程序链接在相同的虚拟地址，旧地址空间的代码页/栈页 TLB 项仍然有效。返回用户态后，新程序对这些页的取指或栈访问命中旧转换，读到旧进程的栈内容而不是内核写入新栈的数据，或执行旧映像的代码，随后异常退出。未动态验证，取决于 TLB 中是否仍保留相应项。
- 修复方向：在 arm64 的 hal::Mmu::switch_space（或 Vmm::switch_page_directory）中写 TTBR0 之后执行 tlbi vmalle1is + dsb + isb；长期方案是为每个地址空间分配 ASID。
- 复核意见：arm64 的 hal::Mmu::switch_space(mmu.cpp:291-296) 只写 TTBR0+isb，Vmm::switch_page_directory(vmm.cpp:1181-1190) 与 execve(process.cpp:496-500) 都没有调用 flush_tlb_all，随后的 destroy_space(mmu.cpp:1197-1232) 中也没有任何 tlbi；src/arch/arm64 内没有 ASID 分配，唯一的 tlbi vmalle1is 在 context_asm.S:210 的上下文切换路径，execve 不经过。因此缺少失效这一点属实，旧地址空间的同 VA 转换可能残留。但实际后果取决于 TLB 是否仍保留条目，且目前 arm64 的 exec 在更早处(COW 栈写 Access flag fault)就失败，无法动态确认。 更正：user.cpp:55 的 task_enter_usermode 路径同样只调 switch_page_directory、无 TLB 失效。修复放在 arm64 hal::Mmu::switch_space 内（写 TTBR0 后 tlbi vmalle1is; dsb ish; isb）最简单。

#### V036 [medium·已确认·all·已修复] 共享内存实际上不共享：mmap 从不使用 shmfs 物理页，get_phys_pages/map_ref/map_unref/is_shmfs_node 均为死代码

- **修复**：Minimal fix as the reviewer suggested: a file mapping with MAP_SHARED now fails instead of silently becoming a private copy; the mm.h comment is updated to match. Real shared mappings are not implemented, and the shmfs.h claim of mmap support (outside ownership) is unchanged. 验证方式：boot regression only; nothing in the repo uses MAP_SHARED
- 位置：`src/kernel/syscalls/mm.cpp:255`；相关：`src/fs/shmfs.cpp:561`、`src/fs/shmfs.cpp:591`、`src/fs/shmfs.cpp:609`、`src/fs/shmfs.cpp:629`、`src/include/fs/shmfs.h:16`
- 证据：do_mmap_file 中 `(void)is_private;  // 目前简化实现，所有文件映射都当作私有处理`，随后对每一页 `mm::Pmm::alloc_frame()` 新分配物理页并用 fs::Vfs::read 拷贝内容后映射。全仓库 grep 表明 fs::Shmfs::get_phys_pages / map_ref / map_unref / is_shmfs_node 除定义外没有任何调用点。shmfs.h 的注释却声称“支持通过 mmap() 映射到进程地址空间”“可被多个进程共享”。
- 触发场景：进程 A 与 B 都 open("/shm/buf") 并 mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)。两者各自得到一份私有拷贝：A 写入映射区后 B 永远看不到，文件内容也不更新（没有写回），mmap 返回成功但语义完全错误，基于 /shm 的 IPC 静默失效。同时 map_count 恒为 0，使 shmfs_unlink 的“仍被映射”检查失效；fork/exit/munmap 时也没有任何 shm 页归属/引用计数逻辑。
- 修复方向：在 do_mmap_file 中对 MAP_SHARED 且 fs::Shmfs::is_shmfs_node(node) 的情况走专门路径：用 get_phys_pages 取得页帧，mm::Pmm 增加帧引用计数后映射进用户页表，并调用 map_ref；在 munmap、进程退出（free_page_directory）和 fork（共享而非 COW）时对应处理并 map_unref。若暂不实现，应让 MAP_SHARED 文件映射明确返回失败，而不是静默退化为私有拷贝。
- 复核意见：mm.cpp:255 `(void)is_private;`，do_mmap_file 对每页 alloc_frame 后用 fs::Vfs::read 拷贝，MAP_SHARED 与 MAP_PRIVATE 完全同一路径；全仓库 grep 显示 get_phys_pages/map_ref/map_unref/is_shmfs_node 只有 shmfs.h 的声明和 shmfs.cpp:561-629 的定义，没有调用点，因此 shmfs.cpp:525 的 map_count>0 检查永远不成立。shmfs.h:14-16 却声称可经 mmap 被多进程共享。 更正：这是未实现的功能而非回归：src/include/kernel/syscalls/mm.h:42 的注释已写明“不支持共享映射（MAP_SHARED）”，且仓库内没有用户程序使用 MAP_SHARED 或 /shm，故评为 medium。真正的缺陷是 MAP_SHARED 静默成功并退化为私有拷贝，以及 shmfs.h 文档与实现不符；最小修复是对非匿名 MAP_SHARED 返回失败。

#### V037 [medium·已确认·x86_64·已修复] x86_64 的直接映射只覆盖前 1GB，但 PMM 不限制物理内存上限，且 MMIO 虚拟窗口与 1GB 以上的直接映射地址重叠

- **修复**：Partial: mm::Pmm::init on x86_64 clamps usable memory to the 1GB the boot direct map covers, so alloc_frame never returns a frame it cannot zero through PHYS_TO_VIRT. Not done: extending the direct map, moving the MMIO window, the ACPI PHYS_TO_VIRT access above 1GB (acpi.cpp) and the leftover i686 PDE check in fork (process.cpp). 验证方式：boot regression only at the default 128MB; not booted with more than 1GB (test.sh has no such configuration)
- 位置：`src/mm/pmm.cpp:258`；相关：`src/mm/vmm.cpp:295`、`src/mm/vmm.cpp:1326`、`src/mm/pmm.cpp:697`、`src/kernel/syscalls/process.cpp:172`、`src/drivers/x86/acpi.cpp:337`、`src/mm/heap.cpp:54`
- 证据：Pmm::init 中 i686 有 2GB 截断，x86_64 分支为空：`#elif defined(ARCH_X86_64) // x86_64: 支持完整物理地址空间...`；Vmm::init 的 x86_64 分支明确不扩展映射（vmm.cpp:295-306 `Boot mapping covers first 1GB`）。但 alloc_frame 末尾 `memset((void*)PHYS_TO_VIRT(addr), 0, PAGE_SIZE)`、ACPI 用 `PHYS_TO_VIRT(rsdt_phys)`（drivers/x86/acpi.cpp:337）都假设所有物理地址可经 KERNEL_VIRTUAL_BASE+phys 访问。同时 `#define MMIO_VIRT_BASE 0xFFFF800040000000ULL`（vmm.cpp:1326）正是物理 1GB 的 PHYS_TO_VIRT 地址。fork 里还残留 `uint32_t phys = get_frame(...); if (phys == 0 || phys >= 0x80000000)`（process.cpp:172-173，get_frame 参数是 uint32_t）。
- 触发场景：用 qemu-system-x86_64 -m 2G 启动：ACPI 表位于 RAM 顶部（约 0x7FFE0000），Acpi::init 通过 PHYS_TO_VIRT 访问 0xFFFF80007FFE0000，该地址未映射 => 内核缺页崩溃；即使绕过 ACPI，物理帧分配到 1GB 以上后 alloc_frame 的 memset 也会缺页，或者（已映射帧缓冲时）直接写进帧缓冲/MMIO；页表帧超过 2GB 后 fork 一律报 “Parent PDE corrupted”。未验证项：没有实际运行，默认 QEMU 128MB 内存不会触发。
- 修复方向：在 x86_64 的 Vmm::init 中按 total_frames 用 2MB/1GB 页把全部物理内存映射到直接映射区；把 MMIO 窗口移到独立的高地址区（例如 0xFFFFC00000000000）；在完成前至少在 Pmm::init 里把可用内存截断到 1GB；删掉 fork 中 i686 专用的 PDE 检查。
- 复核意见：boot64.asm:162-179 只填一个 PD（512 个 2MB 页，即 1GiB）；src/mm/vmm.cpp:295-306 的 x86_64 分支明确不扩展映射；src/mm/pmm.cpp:265-268 的 x86_64 分支为空，不截断 max_addr；alloc_frame 在 pmm.cpp:698 无条件 `memset(PHYS_TO_VIRT(addr))`，acpi.cpp:337 用 PHYS_TO_VIRT(rsdt_phys) 且 kernel.cpp:524 无架构守卫地调用 Acpi::init。因此内存超过 1GiB 时访问未映射地址的缺陷真实存在；vmm.cpp:1326-1327 的 MMIO 窗口也确实是物理 1-3GiB 的直接映射地址。 更正：严重度下调为 medium：Makefile 的 QEMU_FLAGS 不带 -m，默认 128MB 不会触发，属于潜在的配置相关缺陷。MMIO 窗口重叠目前更是纯潜在问题：map_mmio 在内核中的唯一调用者是 e1000.cpp:579，而 x86_64 上 e1000 被跳过（kernel.cpp:538-541），所以“memset 写进设备内存”的分支当前不会发生，实际后果是未映射缺页停机。heap.cpp:54 对堆扩展已有 1GiB 的显式检查，堆本身不受影响。-m 2G 下 ACPI 表落在 1GiB 以上这一点依赖固件行为，未动态验证。
- 被 2 个独立审计者重复报告（x-64bit-6, arch-x86_64-mm-4）

#### V038 [medium·已确认·i686·未修复] i686：Scheduler::free 在清空 PCB 之前调用 free_page_directory，被 is_page_directory_in_use 保护拦截，地址空间泄漏

- **未修复**：The fix belongs in kernel::Scheduler::free (src/kernel/task.cpp, outside my ownership): it must mark the PCB unused, or clear page_dir_phys, before calling free_page_directory. The is_page_directory_in_use guard in vmm.cpp is correct as protection, and loosening it would hide the ordering bug and risk freeing a directory another task still uses. The leak on kill-orphan and fork-failure paths on i686 remains.
- 位置：`src/mm/vmm.cpp:1060`；相关：`src/kernel/task.cpp:215`、`src/kernel/syscalls/process.cpp:902`、`src/kernel/syscalls/process.cpp:218`、`src/kernel/task.cpp:1022`
- 证据：free_page_directory(i686 分支)：`if (is_page_directory_in_use(dir_phys)) { LOG_ERROR_MSG("...BLOCKED!..."); return; }`，is_page_directory_in_use 把所有 state 不是 UNUSED/ZOMBIE/TERMINATED 且 page_dir_phys 相同的任务算作在用。而 Scheduler::free(task.cpp:214-222) 先调用 free_page_directory，最后才 `memset(task,0,...); task->state = TASK_UNUSED;`。因此只要被释放的任务状态是 READY/BLOCKED，释放就被拦截并静默返回。另外 schedule() 只在切换到用户进程时才 sync_current_dir，所以切到 idle/内核线程后 current_dir_phys 仍是已死进程的页目录，`dir_phys == current_dir_phys` 检查也会拦截延迟回收。
- 触发场景：(1) syscall kill 杀死一个没有父进程的非当前进程（process.cpp:902，目标状态为 READY/BLOCKED）：Scheduler::free → free_page_directory 打印 BLOCKED 后返回，随后 PCB 被清零，页目录、全部页表和用户页（至少 1MB 栈）永久泄漏。(2) fork 的三条失败路径中 child 状态为 READY，两次 free_page_directory 都被拦截，克隆出的页目录泄漏，且父进程 COW 帧的引用计数永远多 1。反复执行即可耗尽物理内存。
- 修复方向：在 Scheduler::free 中先把需要的字段保存到局部变量并在锁内把 PCB 标记为 UNUSED（schedule() 的 pending cleanup 分支已经这么做），再在锁外释放页目录；切换到内核线程时也同步 current_dir_phys（或让 free_page_directory 以 CR3 实际值判断）。
- 复核意见：vmm.cpp:998-1012 的 is_page_directory_in_use 把 state 非 UNUSED/ZOMBIE/TERMINATED 且 page_dir_phys 相同的任务视为在用，vmm.cpp:1060 据此直接 return；Scheduler::free(task.cpp:214-222) 先调 free_page_directory 再 memset PCB。process.cpp:898-902 kill 无父进程的非当前目标时不改状态(仍为 READY/BLOCKED)直接 Scheduler::free，fork 的内核栈分配失败路径(process.cpp:216-218) child 状态为 alloc 设置的 TASK_READY(task.cpp:168)，两处释放都会被拦截，页目录与用户页泄漏。属于错误路径/孤儿进程 kill 的边缘场景，与 i686 正常 exec 循环无泄漏的实测不矛盾。 更正：严重度降为 medium：只在 kill 孤儿进程和 fork 失败路径触发。关于切到 idle 后 current_dir_phys 陈旧导致延迟回收被拦截的附带说法未验证，且与 i686 exec 循环无泄漏的实测不一致，不应作为主要依据。


### 进程、调度与同步（16 项）

#### V039 [critical·已确认·all·已修复] execve/加载器只校验 ELF 段起始地址：段尾可越过用户空间上界，把用户页映射进共享的内核页表

- **修复**：New kernel::Elf::validate(elf_data, size) in src/kernel/elf.cpp checks every PT_LOAD with wrap-safe 64-bit comparisons: p_filesz <= p_memsz and [p_vaddr, p_vaddr+p_memsz) must lie below the user stack bottom (USER_SPACE_END - USER_STACK_SIZE; ARM64_USER_STACK_TOP - USER_STACK_SIZE on arm64). Elf::load runs it before allocating or mapping anything. The three per-arch loaders were merged into one. The suggested second half (hal::Mmu::map / map_page_in_directory refusing USER mappings of kernel addresses) is in arch/mm code outside my ownership and was not done. 验证方式：unit tests test_elf_segment_address_range, test_elf_load_maps_only_valid_images (pass on all three archs) + boot regression
- 位置：`src/kernel/elf.cpp:275`；相关：`src/kernel/elf.cpp:112`、`src/kernel/elf.cpp:191`、`src/arch/i686/mm/paging.cpp:624`、`src/kernel/syscalls/process.cpp:442`
- 证据：i686 分支只检查 `if (ph->p_vaddr >= KERNEL_VIRTUAL_BASE)`，随后 `vaddr_end = PAGE_ALIGN_UP(ph->p_vaddr + ph->p_memsz)` 并对 [vaddr_start, vaddr_end) 逐页 `mm::Vmm::map_page_in_directory(page_dir_phys, vaddr, phys, PAGE_PRESENT|PAGE_USER|...)`，没有对段尾和整数回绕的检查。i686 的 hal::Mmu::map（arch/i686/mm/paging.cpp:575-626）不拒绝内核地址：PDE 已存在时直接 `table->entries[pt_idx] = phys | flags`，而内核 PDE(512..1023) 指向的页表被所有地址空间共享。x86_64（elf.cpp:112）与 arm64（elf.cpp:191）同样只检查 p_vaddr；这两个架构的 map 对越界地址的行为未逐行核实（因此 confidence=medium），i686 路径已完整追踪。
- 触发场景：用户在可写文件系统上放置一个 ELF，其中某个 PT_LOAD 的 p_vaddr=0x7FFFF000、p_memsz=0x00400000，然后 exec：加载器把 0x80000000 起的内核虚拟地址（含 0x80100000 处的内核映像）对应的共享 PTE 改写为带 PAGE_USER 的新分配页（内容为 0 或 ELF 提供的数据）→ 内核代码/数据映射被替换，所有进程受影响：轻则立即崩溃，重则以 ring0 执行攻击者数据。即使 exec 最终失败，free_page_directory 也只清理用户半区，内核页表的破坏不会回滚。
- 修复方向：校验 `p_memsz >= p_filesz`、`p_vaddr + p_memsz` 不回绕且 ≤ 用户空间上界（并避开用户栈区）；在 map_page_in_directory/hal::Mmu::map 中拒绝带 USER 标志映射内核地址。
- 复核意见：src/kernel/elf.cpp:271-285 的 i686 分支确实只检查 p_vaddr >= KERNEL_VIRTUAL_BASE，随后 vaddr_end = PAGE_ALIGN_UP(p_vaddr + p_memsz) 无上界和回绕检查，逐页调用 map_page_in_directory。src/mm/vmm.cpp:1203-1227 只做对齐检查后直接转给 hal::Mmu::map；src/arch/i686/mm/paging.cpp:575-626 对内核地址无任何拒绝，PDE 已存在时加上 PAGE_USER 并直接覆盖 PTE，而 create_space（paging.cpp:525-529）把 PDE 512..1023 从 boot_page_directory 原样拷贝，页表是全局共享的。execve（src/kernel/syscalls/process.cpp:367-442）对文件内容除 validate_header（只查魔数/类型/机器）外无其他校验，故用户放置的 ELF 可直接触发，改写共享内核 PTE。 更正：i686 路径已完整证实。x86_64 上 hal::Mmu::map（src/arch/x86_64/mm/paging64.cpp:581）会拒绝非规范地址，段尾越过 0x0000800000000000 时加载失败而不是映射进内核半区，所以 64 位上后果主要是加载失败和 num_pages 截断，不是内核映射被改写；arm64 的 map（src/arch/arm64/mm/mmu.cpp:677）没有范围检查但走的是 TTBR0 表，未逐行核实其越界行为。arch 应标为 i686 为主。
- 被 4 个独立审计者重复报告（x-process-lifecycle-14, x-user-boundary-3, kernel-proc-syscalls-2, x-64bit-10）

#### V040 [critical·已确认·arm64·已修复] ARM64 fork 把用户的 X28 原样写进子进程 context.x[28]，而上下文切换把 x[28] 当作内核栈指针装入 SP_EL1

- 位置：`src/kernel/syscalls/process.cpp:246`；相关：`src/arch/arm64/task/context_asm.S:333`、`src/kernel/task.cpp:680`
- 证据：fork: `for (int i = 1; i < 31; i++) child->context.x[i] = frame[i];`，没有像 create_user_process（task.cpp:680 `task->context.x[28] = task->kernel_stack;`）那样设置内核栈。context_asm.S 的 .restore_user：`ldr x2, [x1, #CTX_X28]; cbz x2, .skip_sp_el1_setup; mov sp, x2`，为 0 时退回全局启动栈 `stack_top`。fork 中分配的 child->kernel_stack 在 ARM64 上从未被使用。
- 触发场景：ARM64 用户进程调用 fork：子进程首次被调度时 SP_EL1 被设置为父进程用户态 X28 的值（非 0 时是任意用户值），子进程下一次 SVC/中断时内核在该地址上压异常帧，破坏任意内存或直接崩溃；X28 为 0 时所有 fork 出的子进程共用同一个启动栈，一个子进程在系统调用中 yield/sleep 后另一个进入内核就会互相覆盖内核栈。
- 修复方向：不要复用通用寄存器传递内核栈：在 cpu_context_t 中增加独立的 kernel_sp 字段（或让切换代码直接取 task->kernel_stack），restore_user 用它设置 SP_EL1，X28 按用户值原样恢复；fork/execve/create_user_process 统一设置该字段。
- 复核意见：src/kernel/syscalls/process.cpp:245-247 的 arm64 分支 `for (i=1;i<31;i++) child->context.x[i] = frame[i]` 把父进程陷入帧中的用户 X28 原样写入子上下文，之后没有像 task.cpp:680 那样写入 kernel_stack；context_asm.S:331-338 的 .restore_user 把 CTX_X28 直接 `mov sp, x2` 作为 SP_EL1，为 0 时退回 stack_top。schedule()（task.cpp:1005-1015）在 arm64 上不另设内核栈，vectors.S 的 kernel_entry 直接 `sub sp, sp, #FRAME_SIZE` 信任 SP_EL1，process.cpp:222 分配的 child->kernel_stack 从未被使用。 更正：restore_user 在 context_asm.S:413 还会把 CTX_X28 原样恢复给用户态，所以由 create_user_process 创建的 shell 用户态 X28 就是自己的内核栈顶地址（内核地址泄漏）；用户代码以 -O0 编译基本不动 X28，因此实际最常见的情形是 fork 出的子进程与父进程共用父进程的内核栈，而不是 X28==0 共用启动栈。这与 arm64 上 fork+exec 后内核崩溃的实测现象方向一致，但实测首个故障是 COW 写的 Access flag fault，不能直接归因于此。
- 被 3 个独立审计者重复报告（kernel-proc-syscalls-4, x-hal-parity-1, x-process-lifecycle-3）

#### V041 [critical·已确认·arm64·已修复] arm64: 用 context.x[28] 传递内核栈指针，fork 的子进程拿到的是用户态 x28（通常是父进程的内核栈顶，也可被用户任意控制）

- 位置：`src/kernel/task.cpp:680`；相关：`src/kernel/syscalls/process.cpp:247`、`src/arch/arm64/task/context.cpp:147`、`src/arch/arm64/task/context_asm.S:1`
- 证据：create_user_process: `task->context.x[28] = task->kernel_stack;`。context_asm.S 的 .restore_user: `ldr x2,[x1,#CTX_X28]; cbz x2,...; mov sp,x2`（设置 SP_EL1），随后 `ldp x28,x29,[x1,#CTX_X28]` 又把这个值原样恢复进用户态 x28。fork(process.cpp:246-248) 对 i=1..30 执行 `child->context.x[i] = frame[i];`，从不把 x[28] 改成子进程自己的 kernel_stack。vectors.S 的 kernel_entry 直接使用当前 SP_EL1，不会重新加载每任务内核栈。
- 触发场景：(1) 正常情况：shell(PID 1) 以 x28=自己的内核栈顶进入用户态，用户代码不动 x28；shell fork 后子进程首次 eret 前 SP_EL1 被设为父进程的内核栈顶。父进程在 waitpid→yield 中挂起，其内核栈上保存着陷阱帧和调用帧；子进程第一次系统调用/中断从同一个栈顶压入陷阱帧，覆盖父进程的栈内容，父进程恢复时使用被破坏的返回地址/寄存器 → 内核崩溃。子进程自己 kmalloc 的内核栈从未被使用。(2) 恶意情况：用户程序把 x28 设为任意内核地址后 fork，子进程下一次异常时内核把 ~272 字节陷阱帧写到该地址 → 任意内核内存写。另外首个进程的 x28 还向用户态泄露了内核栈地址。
- 修复方向：不要复用通用寄存器传递内核栈：在 schedule() 中（像 x86 的 tss_set_kernel_stack 一样）按 next_task->kernel_stack 设置 SP_EL1，或在 context 中增加独立的 kernel_sp 字段；fork 必须为子进程设置自己的内核栈，并保证用户态 x28 的初值为 0。
- 复核意见：src/kernel/task.cpp:680 把 kernel_stack 写入 context.x[28]；src/arch/arm64/task/context_asm.S 的 .restore_user 先 `ldr x2,[x1,#CTX_X28]; mov sp,x2` 设置 SP_EL1，随后 `ldp x28,x29,[x1,#CTX_X28]` 又把同一个值原样带进 EL0。fork（src/kernel/syscalls/process.cpp:246-248）对 i=1..30 直接 `child->context.x[i]=frame[i]`，虽然为子进程 kmalloc 了 kernel_stack 但从未写入 x[28]；schedule() 的 arm64 分支也不设置内核栈（只有注释），vectors.S 的 kernel_entry 直接 `sub sp,sp,#FRAME_SIZE`。因此子进程首次 eret 前的 SP_EL1 完全取自父进程用户态 x28：未改动时是父进程内核栈顶（两个任务共用一条内核栈），用户改写后即为任意内核地址写。 更正：x28 为 0 时走 .skip_sp_el1_setup 退回启动栈 stack_top，同样是多个任务共用的栈。实测中子进程很快因 COW 故障退出、随后在 task.cpp:1028 崩溃，掩盖了父进程内核栈被覆盖的后果，但缺陷本身成立。修复方向正确：按 task->kernel_stack 设置 SP_EL1，不要经过用户可见寄存器。

#### V042 [critical·已确认·arm64·已修复] arm64: schedule() 中针对 PID 1 的调试代码解引用 page_dir==NULL

- 位置：`src/kernel/task.cpp:1028`；相关：`src/kernel/task.cpp:646`、`src/kernel/task.cpp:30`
- 证据：`if (next_task->pid == 1) { page_directory_t *shell_dir = next_task->page_dir; if (is_present(shell_dir->entries[1])) {...` 没有架构保护。arm64 的 create_user_process 明确设置 `task->page_dir = NULL;  // ARM64 doesn't use page_directory_t*`（第 646 行），而 shell 是第一个分配的任务，PID 恰好为 1（测试不创建任务）。
- 触发场景：arm64 上每次从别的任务切换到 shell(PID 1) 时都会在 EL1 读取虚拟地址 0x8。首次调度时 TTBR0 可能还是启动恒等映射，读取碰巧成功；但当 prev_task 是 fork 出来的子进程（TTBR0 为该子进程的用户页表，低地址未映射）时，读取产生 EL1 数据中止 → 内核崩溃。未验证：各时刻 TTBR0 下地址 0x8 是否有映射。同一段代码在 x86_64 上把 64 位 PML4 项截断成 uint32_t 判断，结果无意义。
- 修复方向：删除这段 i686 专用的调试代码，或用 `#if defined(ARCH_I686)` 包住并检查 page_dir 非空。
- 复核意见：src/kernel/task.cpp:1026-1028 的调试块无架构保护，直接读 `next_task->page_dir->entries[1]`；arm64 的 create_user_process 在 task.cpp:646 设 page_dir=NULL，next_pid 从 1 开始（task.cpp:51），arm64 上 shell 是第一个分配的任务。src/arch/arm64/mm/mmu.cpp:1072-1076 的用户地址空间在 0-1GB 只映射 0x08000000/0x09000000 两个 2MB 块，VA 0x8 未映射。真实 QEMU 运行也观察到内核在 task.cpp:1028 因 page_dir 为空发生 data abort 并停机，完全吻合。 更正：严重度上调为 critical：在 arm64 上从 shell 执行任何程序（fork 子进程退出后切回 PID 1）即导致内核停机。x86_64 上该块把 64 位表项截断为 uint32_t 只影响调试判断，无实际后果。
- 被 4 个独立审计者重复报告（kernel-task-sync-6, x-64bit-8, x-doc-drift-9, x-hal-parity-5）

#### V043 [high·已确认·all·已修复] ELF 头与程序头表没有任何边界检查（文件小于 ELF 头、e_phoff/e_phnum 越界、p_offset+p_filesz 溢出），导致内核堆越界读

- **修复**：validate_header and get_entry now take the buffer size and reject files smaller than the ELF header. validate() checks e_phentsize, that e_phoff + e_phnum*phentsize lies inside the file, and each segment's file range as 'offset <= size && filesz <= size - offset'. execve calls validate() before creating the new address space and rejects non-regular files, size 0 and size > 16 MB. loader.cpp passes the size. 验证方式：unit tests test_elf_valid_image_accepted, test_elf_truncated_header_rejected, test_elf_phdr_table_bounds, test_elf_segment_file_range + boot regression (shell and hello.elf still load)
- 位置：`src/kernel/elf.cpp:271`；相关：`src/kernel/elf.cpp:99`、`src/kernel/elf.cpp:108`、`src/kernel/elf.cpp:167`、`src/kernel/elf.cpp:185`、`src/kernel/elf.cpp:262`、`src/kernel/syscalls/process.cpp:375`
- 证据：validate_header 只收到 elf_data 指针、不知道 size，直接读 e_type/e_machine；execve 中 `kmalloc(file_size)` 后即调用（process.cpp:376/396），file_size 可以是 0 或 4。elf_load_impl 里 `phdr = elf_data + ehdr->e_phoff` 与 `i < ehdr->e_phnum` 均未与 size 比较，e_phentsize 被忽略。`if (ph->p_offset + ph->p_filesz > size)` 在 i686 是 32 位加法、在 64 位是 uint64 加法，都可回绕后通过检查，随后 `memcpy(phys_ptr + pg_off, elf_data + ph->p_offset + seg_off, cpy)` 从堆外读取。
- 触发场景：exec 一个只有几十字节但魔数正确的文件，或 e_phoff 指向文件之外、或 p_offset=0xFFFFF000 且 p_filesz=0x2000 的 ELF：内核从 kmalloc 缓冲区之外读取程序头或段数据，读到未映射地址时内核缺页挂机，否则把相邻内核堆内容复制进用户可读的页面（内核信息泄漏）。
- 修复方向：validate_header/load 增加 size 参数：size >= sizeof(ehdr)；e_phentsize == sizeof(phdr)；e_phoff + e_phnum*e_phentsize 不溢出且 <= size；每个段用 `p_offset <= size && p_filesz <= size - p_offset` 的无溢出写法检查；execve 对 file_size 设置上下限并拒绝非普通文件。
- 复核意见：src/kernel/elf.cpp:23 的 validate_header 只接收指针不知长度，直接读 e_type/e_machine；execve（syscalls/process.cpp:375-396）用 kmalloc(file->size) 后即调用，文件只有几字节时即越界读堆。三个架构的 elf_load_impl（elf.cpp:99/167/262）都用 elf_data + e_phoff 和 e_phnum 而不与 size 比较，段检查 `p_offset + p_filesz > size`（elf.cpp:108/185/271）在 32 位和 64 位下都可回绕，之后 memcpy 从缓冲区外拷入用户页。任何能在 ramfs 上创建文件的用户进程都能 exec 这样的文件，造成内核信息泄漏或内核缺页崩溃。 更正：还应注意 p_vaddr + p_memsz 同样未做溢出检查（elf.cpp:118 等），以及加载失败路径上已分配/映射的页没有回滚；修复时建议一并处理。
- 被 2 个独立审计者重复报告（kernel-proc-syscalls-3, x-64bit-11）

#### V044 [high·已确认·all·已修复] fork/create_user_process 错误路径手动释放后又调用 Scheduler::free，导致内核栈、fd_table、地址空间被二次释放

- **修复**：fork error paths now call only Scheduler::free(child); the manual kfree of the kernel stack / fd table and the extra free_page_directory are removed. create_user_process no longer kfrees the kernel stack before Scheduler::free and clears page_dir_phys on failure so the address space stays with the caller (loader.cpp and task_create_user_process_arm64 already destroy it). Scheduler::free marks the task TERMINATED before freeing the page directory, otherwise the i686 is_page_directory_in_use check refused to free a never-run child's directory. 验证方式：unit test test_create_user_process_failure_keeps_address_space (create_user_process path). The fork error paths need a kmalloc failure and are untested beyond boot regression.
- 位置：`src/kernel/syscalls/process.cpp:217`；相关：`src/kernel/syscalls/process.cpp:306`、`src/kernel/syscalls/process.cpp:318`、`src/kernel/task.cpp:656`、`src/kernel/task.cpp:723`、`src/kernel/task.cpp:808`、`src/kernel/loader.cpp:103`
- 证据：kernel::Scheduler::free(task) 自己会 `kfree((void*)kernel_stack_base)`、关闭并 `kfree(task->fd_table)`、以及 `if (is_user && page_dir_phys) mm::Vmm::free_page_directory(page_dir_phys)`（task.cpp:197-215）。但调用者在调用它之前又手动释放了同样的资源：process.cpp:217-218 `mm::Vmm::free_page_directory(child->page_dir_phys); kernel::Scheduler::free(child);`；306-308 `kfree((void*)child->kernel_stack_base); mm::Vmm::free_page_directory(...); kernel::Scheduler::free(child);`；318-321 还多了 `kfree(child->fd_table)`（此后 Scheduler::free 还会遍历已释放的 fd_table->entries）；task.cpp:656-657 与 723-724 `kfree((void*)task->kernel_stack_base); kernel::Scheduler::free(task);`。此外 create_user_process 失败后 loader.cpp:103/141 与 task.cpp:808 的调用者还会再销毁一次地址空间。x86_64/arm64 的 hal::Mmu::destroy_space 不清空页表项（paging64.cpp:1225-1260），第二次遍历会对每个叶子帧再做一次 frame_ref_dec。
- 触发场景：内存紧张时（内核堆上限 32MB，用户可通过 ramfs/管道/套接字耗尽）某进程 fork：clone_page_directory 成功（与父进程 COW 共享帧，refcount=2），随后 kmalloc(KERNEL_STACK_SIZE) 或 kmalloc(sizeof(FdTable)) 失败。x86_64/arm64 上地址空间被 destroy 两次，共享帧 refcount 2→1→0，父进程仍在映射使用的物理页被 PMM 回收并分配给别人，造成跨进程内存破坏；同时内核栈被 kfree 两次，heap.cpp 的 coalesce 在已被合并的陈旧块头上再次合并，破坏堆链表。create_user_process 在 setup_user_stack 失败时同样 double free 内核栈。
- 修复方向：统一所有权：PCB 上记录的资源只由 Scheduler::free 释放，错误路径直接调用 Scheduler::free(child) 即可，删除其前面的手动 kfree/free_page_directory；create_user_process 明确约定失败时是否已消费 page_dir，并让 loader/task_create_user_process_arm64 据此决定是否再销毁。
- 复核意见：Scheduler::free（src/kernel/task.cpp:185-225）自己会 kfree(kernel_stack_base)、遍历并 kfree(fd_table)、对用户进程调用 free_page_directory(page_dir_phys)；而 fork 的错误路径在调用它之前又手动释放了同样的资源且未清空 PCB 字段（process.cpp:217-218、305-308、317-321），create_user_process 同样在 task.cpp:656-657、723-724 先 kfree 内核栈再 Scheduler::free。kfree（src/mm/heap.cpp:317-331）只校验 magic、没有 is_free 检查，二次释放会在陈旧块头上再次 coalesce；x86_64 的 free_page_table_recursive（paging64.cpp:1218-1262）不清表项，第二次遍历会对 COW 共享帧再做一次 frame_ref_dec。 更正：触发条件是 kmalloc 失败或 FdTable::copy 失败的错误路径，正常运行不会走到，所以定级 high 而非 critical。process.cpp:217 处 kernel_stack_base 仍为 0，只有页目录被二次销毁；内核栈二次释放发生在 306 和 318 两处。loader.cpp:103/141 与 task.cpp:808 的调用者在 create_user_process 返回 0 后还会再 destroy_space/free_page_directory 一次，构成第三次销毁。第二次销毁时页表帧本身已被 free_frame 释放，若期间被重新分配则会遍历到无关内容。

#### V045 [high·待确认·x86_64·已修复] x86_64 execve 未校验 ELF 入口地址，非规范地址经 SYSRET 返回会在 ring0 触发 #GP

- **修复**：ELF side only: validate() requires e_entry to lie inside a PF_X PT_LOAD segment, and all segments lie below 0x7FF00000 on x86_64, so execve can no longer put a non-canonical address into the SYSRET RCX. The generic hardening the reviewer asked for (check RCX before sysret, fall back to iretq) is in src/arch/x86_64/syscall/syscall64_asm.asm, outside my ownership, and was not done. 验证方式：unit test test_elf_entry_point_checked (includes the non-canonical 0x0000800000000000 case on x86_64)
- 位置：`src/kernel/syscalls/process.cpp:650`；相关：`src/kernel/elf.cpp:147`、`src/kernel/elf.cpp:112`、`src/arch/x86_64/syscall/syscall64_asm.asm:211`
- 证据：`frame[12] = entry_point; // RCX = 新程序入口点（SYSRET 返回地址）`，entry_point 直接来自 `ehdr->e_entry`（src/kernel/elf.cpp:147），x86_64 的 elf_load_impl 只检查 `ph->p_vaddr >= KERNEL_VIRTUAL_BASE`，从不检查 e_entry。syscall_entry 最后执行 `pop rsp; cli; o64 sysret`。Intel CPU 上 RCX 非规范时 SYSRET 在 ring0 抛 #GP，而此时 RSP 已是用户栈。
- 触发场景：用户在 FAT32 上写入一个 e_entry=0x0000800000000000 的 ELF 并 exec：execve 成功返回，sysret 在内核态 #GP，异常处理在用户控制的栈上运行，导致内核崩溃或被利用。未验证的假设：QEMU TCG 不在 ring0 报错（表现为用户态取指 #GP，进程被杀），真实 Intel 硬件/KVM 上才是内核态异常。
- 修复方向：在 ELF 加载/execve 中校验 e_entry 落在已加载的用户可执行段内（至少 < USER_SPACE_END 且规范）；syscall 返回路径在 RCX 非规范或帧被修改（execve）时改用 iretq。
- 复核意见：x86_64 的 elf_load_impl（src/kernel/elf.cpp:100-150）只检查 p_vaddr >= KERNEL_VIRTUAL_BASE，e_entry 未经任何校验就写入 *entry_point（elf.cpp:147），execve 再原样写入 frame[12]（process.cpp:650），syscall_entry 在 `pop rsp; cli; o64 sysret`（syscall64_asm.asm:203-211）时 RSP 已是用户值。Intel CPU 上 RCX 非规范时 SYSRET 在 ring0 抛 #GP 是已知行为，但这一环依赖真实硬件/KVM，QEMU TCG 下无法静态确认，且未核实 #GP 处理是否使用 IST 栈。 更正：问题不限于 execve：段地址检查只要求 < KERNEL_VIRTUAL_BASE，用户可以把代码映射到规范地址空间最后一页，在 0x7FFFFFFFFFFE 处执行 syscall 指令，使任何系统调用的返回 RCX 都等于非规范的 0x0000800000000000。因此修复应放在通用的 syscall 返回路径（sysret 前检查 RCX 是否规范，否则改用 iretq），同时在 ELF 加载时校验 e_entry 和段的上界，而不仅仅是校验 e_entry。

#### V046 [high·已确认·all·已修复] kill 对任意 PID 无任何权限/类型检查，并在目标处于 READY/BLOCKED（位于内核执行中途）时就地标记僵尸或直接释放

- **修复**：kill rewritten: it no longer changes the target's state or frees it. It sets task->kill_pending / kill_signal (Scheduler::request_kill), wakes a target sleeping in nanosleep, and the target exits by itself in Scheduler::deliver_pending_kill, called from syscall_dispatcher after the handler returns. Kernel threads and PID 0 are refused, signal 0 only probes, signals >= 64 are rejected, kill(self) exits through exit_current, waitpid encodes the signal as & 0x7F and returns -1 when the waiter itself has a kill pending. 验证方式：boot regression only; no kill was issued in the test runs, so the new path is untested at runtime
- 位置：`src/kernel/syscalls/process.cpp:902`；相关：`src/kernel/syscalls/process.cpp:780`、`src/kernel/syscalls/process.cpp:831`、`src/kernel/syscalls/process.cpp:879`、`src/kernel/syscalls/process.cpp:976`
- 证据：只排除了 pid==0；`kernel::Scheduler::get_by_pid(pid)` 找到任何任务（包括内核线程、PID 1、其他进程）后，无父进程时 `kernel::Scheduler::free(target)`，有父进程时 `target->state = TASK_ZOMBIE`。调度是纯协作式的（schedule_from_irq 为空），所以非当前任务必然停在内核里的某个 yield/block/sleep 点，可能正持有 sync::Mutex（mutex.cpp 以 owner_pid_ 记录持有者）。signal 参数完全未解释：`kill(pid, 0)` 也会终止目标；`target->exit_signal = signal` 在 waitpid 中 `& 0xFF`，signal 为 0 或 256 的倍数时状态被报告为“正常退出 0”。目标是当前进程且有父进程时只置 ZOMBIE 就返回用户态继续运行，之后调用 sleep/block 会把状态改回 BLOCKED 而“复活”。i686 上 free_page_directory 因 is_page_directory_in_use 拦截（目标状态仍为 READY/BLOCKED）而泄漏地址空间。
- 触发场景：普通用户程序对内核线程的 PID 或 shell 的 PID 调用 kill：内核线程的 PCB 和内核栈被立即释放，内核功能（如网络/后台线程）消失；被终止的任务若正持有内核互斥锁，则该锁永不释放，之后所有需要它的系统调用永久阻塞。
- 修复方向：kill 只设置目标的 pending-signal 标志，由目标自己在返回用户态前（或被唤醒后）调用 task_exit 完成退出；禁止对内核线程和 idle 发送；实现 signal==0 仅探测、区分 SIGKILL/SIGTERM；退出状态编码用 `signal & 0x7F`。
- 复核意见：process.cpp:779-903：除 pid==0 外没有任何类型或属主检查，signal 完全不解释（806 行注释“所有信号都直接终止进程”，所以 kill(pid,0) 也会杀）；无父进程的目标（所有内核线程 parent==NULL）在 902 行被 Scheduler::free，task.cpp:184-224 立即 kfree 内核栈并 memset PCB，而不从信号量/互斥锁等待队列中摘除，也不释放其持有的锁。目标为 BLOCKED 时只有 READY 分支做 ready_queue_remove（861 行），因此会留下悬空的等待队列引用；waitpid 975 行 `exit_signal & 0xFF` 的编码问题也属实。 更正：与 x-user-boundary-7 的 kill 部分重叠，但根因不同（这里是异步就地终止的实现方式）。“自杀后经 sleep 复活”和 i686 的 is_page_directory_in_use 泄漏两点未逐行验证，只能算可能。修复方向（pending 标志 + 目标自行 task_exit，拒绝内核线程，signal==0 仅探测）合理。

#### V047 [high·已确认·arm64·已修复] arm64: 内核线程初始上下文绕过 hal_context_enter_kernel_thread——首次运行时 IRQ 保持屏蔽，入口函数返回后被无限重入

- **修复**：arm64 create_kernel_thread and the idle task now set context.pc = task_enter_kernel_thread and context.x[19] = entry, so the trampoline unmasks interrupts and calls task_exit when the entry returns. 验证方式：boot regression only (arm64: 486 ok lines, shell reached, exec check 1)
- 位置：`src/kernel/task.cpp:534`；相关：`src/kernel/task.cpp:873`、`src/arch/arm64/task/context_asm.S:286`、`src/arch/arm64/task/context_asm.S:441`、`src/arch/arm64/task/context.cpp:99`
- 证据：task.cpp:531-540 arm64 分支：`task->context.pc = (uintptr_t)entry;`(idle 同样，873 行 `idle_task->context.pc = (uintptr_t)idle_task_loop;`)，没有设置 x19/x30，也没有像 x86 那样经 task_enter_kernel_thread。context_asm.S:286 `.restore_kernel` 中 `ldr x30, [x1, #CTX_PC]` 后 `ret`，既不恢复 x30 槽也不恢复 PSTATE/DAIF。而 context_asm.S:441-453 的 hal_context_enter_kernel_thread(`msr daifclr,#0xf; mov x0,x19; blr x0; bl task_exit`)在 arm64 上从未被使用。x86 版本则有 `sti` + `call entry` + `call task_exit`。
- 触发场景：(a) schedule() 在 Interrupts::disable()(DAIF 全屏蔽)后切到新内核线程，新线程从 entry 开始运行时 DAIF 仍屏蔽，之后它每次 schedule 的 save/restore 都保持屏蔽——该线程永远不可抢占、其运行期间不处理任何 IRQ。idle 线程即如此：当所有其他任务都阻塞(sleep、阻塞式 read——arm64 的 SVC 路径本身也全程屏蔽 IRQ)时，idle 的 wfi 被挂起的定时器 IRQ 唤醒但处理函数永不执行，timer_tick 不再推进，睡眠/阻塞任务永远不被唤醒，系统挂死。(b) 入口函数返回时 LR==entry，函数被从头再次执行(死循环)，而不是调用 task_exit。
- 修复方向：arm64 创建内核线程/idle 时设置 context.pc = hal_context_enter_kernel_thread、context.x[19] = entry(即直接复用 hal::Context::init 的逻辑)，由该蹦床开中断并在返回后调用 task_exit；消除 task.cpp 与 arch/*/context.cpp 两份重复的上下文初始化代码。
- 复核意见：task.cpp:528-540 和 870-875 的 arm64 分支只设 context.pc=entry/idle_task_loop，未设 x19/x30；context_asm.S:280-309 的 .restore_kernel 把 CTX_PC 载入 x30 后 ret，不触碰 DAIF，而 schedule()（task.cpp:922）在切换前已 daifset 屏蔽。新线程不会执行 schedule 末尾的 restore，之后每次 yield 的 Interrupts::disable() 返回 false，restore(false) 不开中断（interrupt.h:41-44、66-70），所以 idle 的 wfi/yield 循环永远在 IRQ 屏蔽下运行；睡眠任务只在 timer_tick（task.cpp:1129）被唤醒，故 Scheduler::sleep 后只剩 idle 时系统挂死。hal_context_enter_kernel_thread（context_asm.S:441）仅被 hal::Context::init 和测试引用，task.cpp 从未使用。 更正：(b) 入口返回后因 LR==entry 重入属实，但 idle 从不返回，目前只影响 arm64 上入口会返回的内核线程（如测试线程）。挂死需要“所有其他任务都阻塞”这一前提，arm64 用户 shell 调用 nanosleep 即可触发；该运行时现象本次未在 QEMU 中实测。
- 被 2 个独立审计者重复报告（x-asm-abi-6, x-process-lifecycle-13）

#### V048 [high·已确认·arm64·已修复] arm64: 内核线程/idle 不经过 task_enter_kernel_thread，首次运行时中断保持屏蔽，入口函数返回后会无限重入

- **修复**：Same change as x-asm-abi-6 (idle task starts through the trampoline with IRQs enabled). 验证方式：boot regression only
- 位置：`src/kernel/task.cpp:873`；相关：`src/kernel/task.cpp:534`、`src/kernel/task.cpp:537`、`src/arch/arm64/task/context_asm.S:441`
- 证据：arm64 分支: `idle_task->context.pc = (uintptr_t)idle_task_loop; idle_task->context.pstate = ARM64_PSTATE_EL1h;`（create_kernel_thread 第 534 行同样 `context.pc = entry`）。context_asm.S 的 .restore_kernel 只做 `mov sp,x2; ldr x30,[x1,#CTX_PC]; ...; ret`，完全不恢复 PSTATE/DAIF，而 schedule() 进入时已 `Interrupts::disable()`（msr daifset,#0xf）。x86 分支则使用 task_enter_kernel_thread（先 sti，返回后调用 task_exit）并通过 popf 恢复 eflags=0x202。另外 x30 被设置为 pc=entry，入口函数 ret 时会跳回自己的入口。
- 触发场景：arm64 上所有任务都阻塞时（例如 shell 调用 nanosleep，或启动时没有 shell）：schedule() 关中断后首次切到 idle，idle_task_loop 在 DAIF 全屏蔽下执行 wfi；有挂起的定时器 IRQ 时 wfi 立即返回但中断不会被响应，随后 yield→schedule（prev_state=false，restore 不开中断）→ 仍只有 idle。timer_tick 永远不运行，睡眠任务永不被唤醒，UART 接收中断也收不到，系统死循环空转。未验证的假设：没有其他代码在 idle 首次运行前为它打开中断。此外 arm64 上任何会返回的内核线程入口都会被无限重复执行而不是 task_exit。
- 修复方向：arm64 也使用 task_enter_kernel_thread 蹦床（context.pc 指向蹦床，x19 存入口函数，蹦床先 `msr daifclr`，返回后调用 task_exit）；或让 .restore_kernel 按保存的 pstate 恢复 DAIF。
- 复核意见：src/kernel/task.cpp:871-875 的 idle 上下文 pc 直接指向 idle_task_loop，而 context_asm.S 的 .restore_kernel 只恢复 SP/x30/通用寄存器后 ret，不触碰 DAIF；schedule() 入口已 Interrupts::disable()。全仓库 arm64 的 daifclr 只在 hal::Interrupt::enable（仅 kernel.cpp:319 调用一次）和未被使用的 hal_context_enter_kernel_thread 中，svc.S 路径也不开中断。因此用户进程经 nanosleep（process.cpp:752 → Scheduler::sleep）或信号量阻塞后首次切到 idle 时中断全屏蔽，idle 的 wfi→yield 循环中 prev_state 恒为 false，定时器 IRQ 不再被响应，timer_tick 无法唤醒睡眠任务。 更正：“入口函数返回后无限重入”部分目前是潜在问题：arm64 上 create_kernel_thread 只在 kernel.cpp:668 的 x86 分支被调用，idle_task_loop 不返回。waitpid 用的是 yield 轮询（process.cpp:1018），shell 始终就绪，所以普通 exec 场景不触发；触发条件是所有任务都 BLOCKED（nanosleep、管道/信号量等待）。
- 被 2 个独立审计者重复报告（kernel-task-sync-5, x-hal-parity-3）

#### V049 [high·已确认·all·已修复] 进程退出变成僵尸时不关闭 fd，管道写端要到被 waitpid 回收才关闭，读端永远等不到 EOF

- **修复**：Scheduler::exit_current (called by task_exit) closes every fd and frees the fd table in the exiting task's own context before it becomes ZOMBIE/TERMINATED. It also drains the interrupt-depth count left by the x86 exception stub when a faulting user task exits, so closing fds may take a Mutex. 验证方式：boot regression only (arm64 run execs hello.elf, which exits through this path and is reaped by the shell)
- 位置：`src/kernel/task.cpp:1225`；相关：`src/kernel/task.cpp:202`、`src/kernel/task.cpp:952`、`src/kernel/syscalls/process.cpp:37`
- 证据：exit 路径只做 `current_task->state = TASK_ZOMBIE;`，fd 表的关闭只出现在 `kernel::Scheduler::free`（task.cpp:202-210，由 waitpid 回收僵尸时调用）和调度器的 pending_cleanup 分支（task.cpp:952-959）。syscall::Process::exit（process.cpp:37）也没有关闭 fd。管道 EOF 依赖 pipe_close 把 writers 减到 0。
- 触发场景：标准写法：父进程 `pipe(p); fork();` 子进程写完数据后 exit；父进程 `close(p[1]); while (read(p[0],...) > 0) ...; waitpid(child)`。子进程已成僵尸但其 p[1] 仍计入 pipe->writers，父进程读完数据后在 pipe_read 的 `read_sem.wait()` 永久阻塞，而回收子进程（才会关闭写端）的 waitpid 永远执行不到 -> 死锁。同理，写端进程向已退出但未被回收的读者写满 4KB 管道后永久阻塞而不是得到 broken pipe。未验证的假设：仅通过 grep 确认退出路径中没有其他关闭 fd 的调用，未逐行读完 task_exit。
- 修复方向：在 exit（进入 ZOMBIE 之前、仍在该进程上下文且可睡眠时）关闭全部 fd 并释放 fd 表；僵尸只保留 pid/exit_code 等回收所需信息。
- 复核意见：逐行读完 task_exit（src/kernel/task.cpp:1163-1247）：只设置 TASK_ZOMBIE/TASK_TERMINATED 后调用 schedule，没有关闭任何 fd；syscall::Process::exit（process.cpp:37）也只是转调 task_exit。fd 关闭只在 Scheduler::free（task.cpp:202-210，waitpid 回收时）和 schedule 的 pending_cleanup 分支。fork 复制 fd 表时 Pipe::on_dup 使 writers++（src/fs/pipe.cpp:46），而 EOF 依赖 pipe_close 把 writers 减到 0 才置 write_closed（pipe.cpp:360-364），pipe_read 否则在 read_sem.wait() 阻塞（pipe.cpp:210），所以僵尸子进程持有的写端会让先 read 到 EOF 再 waitpid 的父进程永久阻塞。 更正：行号应为 task_exit 中设置 TASK_ZOMBIE 处（约 task.cpp:1226）。无父进程的孤儿任务走 TERMINATED，在下一次 schedule 的延迟清理中会关闭 fd，不受影响；问题只出现在有存活父进程的僵尸上。user/shell/shell.cpp:2206 的管道实现可能受影响。arm64 上阻塞后还会叠加 idle 中断屏蔽问题（kernel-task-sync-5）。

#### V050 [high·已确认·all·已修复] task_exit 在调用 schedule() 前把 current_task 置 NULL，TERMINATED 任务永远不会进入延迟清理，PCB/内核栈/地址空间永久泄漏

- **修复**：task_exit no longer sets current_task = NULL before schedule(), so TERMINATED tasks reach the deferred cleanup. The single pending_cleanup_task pointer is now a list linked through task->next and is drained with Scheduler::free at the top of schedule(). schedule() now calls mm::Vmm::sync_current_dir for kernel threads too (with context.cr3), otherwise free_page_directory refused the dead task's directory as 'current'. 验证方式：boot regression only (unit tests run with no current task, so the scheduler cannot be exercised there)
- 位置：`src/kernel/task.cpp:1238`；相关：`src/kernel/task.cpp:970`、`src/kernel/task.cpp:1229`、`src/kernel/syscalls/process.cpp:48`
- 证据：task_exit: `current_task->state = TASK_TERMINATED; ... current_task = NULL; kernel::Scheduler::schedule();`。schedule() 中 `task_t *prev_task = current_task;`（970 行）得到 NULL，`should_free_prev_task` 的条件 `prev_task && ... state == TASK_TERMINATED` 永不成立，pending_cleanup_task 不会被设置。只有 kill 自己的路径（process.cpp:895，保留 current_task）才能走到清理。
- 触发场景：任何没有父进程的任务退出（返回的内核线程、父进程已退出的孤儿进程、PID 1 shell 本身）：状态停在 TERMINATED，task_pool 槽位、8KB 内核栈、fd 表中打开的文件/管道引用、整个用户地址空间都不释放；累计 MAX_TASKS(256) 次后 Scheduler::alloc 永久失败，fork 全部返回 -12。
- 修复方向：task_exit 不要清空 current_task，而是在本地记录待清理任务（或直接设置 pending_cleanup_task = current_task）后调用 schedule()，由下一个任务在自己的栈上回收；同时在 exit 时就关闭 fd 表。
- 复核意见：src/kernel/task.cpp:1229 把无父任务置为 TASK_TERMINATED，随后 1238 行 `current_task = NULL` 再调用 schedule()；schedule() 在 970 行取 prev_task=current_task(NULL)，974 行条件 `prev_task && ... == TASK_TERMINATED` 永不成立，1052 行的 pending_cleanup_task 也就不会被赋值。全仓库 grep TASK_TERMINATED/pending_cleanup_task 没有其他回收点（waitpid 只回收 ZOMBIE，alloc 只复用 UNUSED），syscall exit (process.cpp:48) 也没有提前关闭 fd，因此 PCB 槽、内核栈、fd 表、地址空间全部泄漏。该文件三个架构共用。 更正：唯一能走到延迟清理的是 process.cpp:895 的 kill(self) 且无父进程分支。常规 shell 运行的子进程有父进程会走 ZOMBIE 路径，不受影响；受影响的是孤儿进程、返回的内核线程、PID 1 以及 arm64 exception.cpp:621 杀掉的孤儿。修复时 pending_cleanup_task 是单指针，应改为队列，否则连续两个 TERMINATED 任务会互相覆盖。
- 被 5 个独立审计者重复报告（kernel-proc-syscalls-9, kernel-task-sync-3, x-concurrency-7, x-error-init-4, x-process-lifecycle-1）

#### V051 [high·已确认·all·已修复] Scheduler::wakeup 忽略 wait_object，总是唤醒任务池里第一个 BLOCKED 任务，造成丢失唤醒

- 位置：`src/kernel/task.cpp:1305`；相关：`src/kernel/task.cpp:1280`、`src/kernel/sync/mutex.cpp:114`、`src/kernel/sync/semaphore.cpp:78`、`src/fs/pipe.cpp:210`
- 证据：`void kernel::Scheduler::wakeup(void *wait_object) { (void)wait_object;  // 暂不使用 ... for (i...) if (task->state == TASK_BLOCKED && task->sleep_until_ms == 0) { task->state = TASK_READY; ... break; }`（注释：“简化实现：唤醒第一个阻塞的任务，完整实现应该维护每个等待对象的等待队列”）。Mutex::unlock / Semaphore::signal 都依赖它（mutex.cpp:114, semaphore.cpp:78），阻塞方被唤醒后发现条件不满足会再次阻塞，但真正的等待者没人再唤醒。docs/10-sync.md 把互斥锁/信号量标为“✅ 已实现”。
- 触发场景：shell 执行三级管道 `a | b | c`：b 阻塞在 pipe1.read_sem，c 阻塞在 pipe2.read_sem，且 c 在 task_pool 中下标更小。a 写 pipe1 → read_sem.signal → wakeup 唤醒的是 c；c 发现 pipe2 仍为空重新阻塞；b 的信号量计数已为 1 却永远不被唤醒 → 管道死锁。FAT32 fs_lock/ATA 互斥锁与管道信号量同时有等待者时同理。
- 修复方向：在 Mutex/Semaphore 内维护各自的等待队列（或在 task_t 里记录 wait_object 并在 wakeup 时按对象匹配），unlock/signal 只唤醒本对象的等待者；block() 把 wait_object 写入任务结构。
- 复核意见：src/kernel/task.cpp:1305-1332 的 wakeup 确实 `(void)wait_object`，并在 1318 行按 task_pool 下标唤醒第一个 `TASK_BLOCKED && sleep_until_ms==0` 的任务后 break；Mutex::lock (mutex.cpp:73) 与 Semaphore::wait (semaphore.cpp:41) 只置 TASK_BLOCKED，不记录等待对象，task_t 中也没有 wait_object 字段。被错唤醒的任务在 while(1) 中重试失败后再次阻塞，而 signal/unlock 只调用一次 wakeup（semaphore.cpp:78、mutex.cpp:114），真正的等待者无人再唤醒。pipe.cpp:210/298 的读写阻塞、FAT32/ATA/VFS 的 Mutex 都依赖此路径，两个以上不同对象同时有等待者即可触发。 更正：丢失的唤醒不一定永久：之后任何一次无关的 signal/unlock 都可能碰巧唤醒真正的等待者，所以表现为挂起或严重延迟而非必然死锁；但在没有后续唤醒源时确实永久挂起。Scheduler::block (1280) 同样忽略 wait_object，需一并修改。
- 被 3 个独立审计者重复报告（x-doc-drift-8, kernel-task-sync-1, x-concurrency-3）

#### V052 [high·已确认·all·未修复] IRQ 返回路径上的 schedule_from_irq 是空函数：系统完全没有抢占，用户态死循环即可独占 CPU 挂死整机

- **未修复**：Preemption is an unimplemented feature, not a local bug. A correct fix needs changes to the IRQ return stubs of all three architectures, per-task interrupt-depth accounting, the restore paths in the context-switch assembly and FPU state saving, all outside my ownership and too large for this batch. schedule_from_irq is still empty.
- 位置：`src/kernel/task.cpp:1341`；相关：`src/arch/i686/interrupt/irq.cpp:131`、`src/kernel/task.cpp:1149`、`src/arch/i686/task/task_asm.asm:184`
- 证据：irq_handler 在 EOI 之后调用 `schedule_from_irq(regs);`（src/arch/i686/interrupt/irq.cpp:131），但其实现只有 `(void)regs;` 和一段注释“当前暂时禁用从 IRQ 的调度”。Scheduler::timer_tick 里时间片到期分支也只是 `tick_count = 0;`（task.cpp:1149-1153），不设置任何 need_resched 标志；syscall_handler 返回用户态前也不检查重调度。任务切换只发生在任务主动调用 schedule()/yield()/sleep()/block() 时。
- 触发场景：用户程序执行 `for(;;);`（或任何不做阻塞系统调用的长计算）：定时器中断照常进入 timer_tick，把到期的睡眠任务放回就绪队列，但中断返回后仍回到该进程；shell、idle、其它进程永远不再运行，键盘输入无人读取，也没有信号机制可以杀掉它，只能重启。DEFAULT_TIME_SLICE/priority 等字段形同虚设。
- 修复方向：实现真正的抢占：timer_tick 时间片用完时置 need_resched；在 irq_common_stub 返回前（EOI 之后、且被打断的是用户态或可抢占的内核态时）调用 schedule()。注意配套修改：.restore_kernel 中 popfd 先于寄存器恢复开中断的窗口、.restore_user 在旧任务栈上构造 iret 帧、interrupt_enter/exit 计数需按任务平衡，以及上下文切换需要保存 FPU 状态。
- 复核意见：src/kernel/task.cpp:1341-1352 的 schedule_from_irq 函数体只有 (void)regs 和注释；task.cpp:1149-1153 时间片到期分支只做 tick_count = 0。全仓库没有 need_resched 标志，Scheduler::schedule() 的调用点只有 yield/sleep/block/exit、Mutex、Semaphore、键盘读取和启动代码（grep 结果），IRQ 汇编出口也没有调度调用。因此用户态不做系统调用的死循环确实会永久占住 CPU，三个架构都受影响。 更正：这是未实现的功能（协作式调度），不是内存破坏；中断本身仍会执行（定时器计数、网卡收包照常），所以定为 high 而非 critical。
- 被 7 个独立审计者重复报告（arch-i686-core-4, arch-x86_64-core-4, kernel-task-sync-2, x-concurrency-1, x-doc-drift-3, x-process-lifecycle-9, x-user-boundary-8）

#### V053 [medium·已确认·x86_64·已修复] x86_64 fork 不复制 R8-R15，子进程从 fork 返回后被调用者保存寄存器 R12-R15 全为 0

- **修复**：x86_64 fork copies r15..r8 from frame[0..7] into the child context. 验证方式：boot regression only (user programs are built -O0 and do not keep values in r12-r15, so nothing in the tree exercises it)
- 位置：`src/kernel/syscalls/process.cpp:262`；相关：`src/include/kernel/task.h:71`
- 证据：`memset(&child->context, 0, sizeof(cpu_context_t));` 之后只赋值 eax/ebx/ecx/edx/esi/edi/ebp/esp/eip/eflags（即 rax..rbp 别名），而 x86_64 的 cpu_context_t（task.h:71-106）还包含 r8-r15，系统调用帧 frame[0..7] 中保存着用户的 r15..r8，却从未被读取。
- 触发场景：x86_64 用户程序在调用 fork() 的函数（或其调用者）中把局部变量/指针放在 r12-r15（任何开启优化的编译结果或手写汇编都会如此）：父进程返回后正常，子进程返回后这些寄存器为 0，随后解引用空指针或使用错误的值。
- 修复方向：x86_64 分支按帧布局完整复制：r15=frame[0] … r8=frame[7]，rbp=frame[8]，rdi=frame[9]，rsi=frame[10]，rdx=frame[11]，rbx=frame[13]，rax=0；最好让 fork 直接以统一的 trap frame 结构拷贝而不是逐字段手写。
- 复核意见：process.cpp:236 先 memset 清零 child->context，x86 分支（process.cpp:262-272）只赋值 eax/ebx/ecx/edx/esi/edi/ebp/esp/eip/eflags，x86_64 的 cpu_context_t（src/include/kernel/task.h:71-106）另有 r8-r15，而陷入帧 frame[0..7] 中保存的用户 r15..r8（syscall64_asm.asm:36-43）从未被读取；context64_asm.asm:192-195 恢复用户上下文时会把这些 0 装入寄存器。子进程从 fork 返回后被调用者保存的 r12-r15 确实为 0，违反 SysV ABI。 更正：仓库内所有用户程序都以 -O0 编译（user/*/Makefile:42-45），编译器基本不把局部变量放进 r12-r15，实测 x86_64 上 fork/exec 可正常工作，所以当前是潜伏的 ABI 缺陷，定级 medium；一旦用户态开启优化或使用手写汇编就会出错。另外子进程的 rcx 被设为 user_ecx=frame[12]（即用户 RIP）、r11 为 0，这两个按 SYSCALL 约定本就是被破坏的寄存器，无影响。

#### V054 [medium·已确认·all·未修复] 没有任何权限模型：重启、关机、kill 任意 PID、网络配置对所有进程开放

- **未修复**：Only the kill part is addressed (kernel threads refused, deferred self-exit; see kernel-proc-syscalls-10). There is still no credential model: reboot/poweroff wrappers (src/kernel/syscall.cpp) and the net ioctl (src/kernel/syscalls/net.cpp) remain open to every process and belong to other areas. Any user process can still kill any other user process, including the shell.
- 位置：`src/kernel/syscalls/process.cpp:786`；相关：`src/kernel/syscall.cpp:281`、`src/kernel/syscall.cpp:288`、`src/kernel/syscalls/process.cpp:902`、`src/kernel/syscalls/net.cpp:290`、`src/kernel/syscalls/net.cpp:135`
- 证据：kill 只排除 pid == 0，随后对 get_by_pid(pid) 得到的任何任务（包括内核线程和其他进程）修改状态或直接 Scheduler::free。sys_reboot_wrapper / sys_poweroff_wrapper 无条件调用。syscall::Net::ioctl 忽略 fd，直接修改 IP、掩码、网关、接口状态和 ARP 缓存。task_t 中没有 uid 或能力位。
- 触发场景：任意用户进程都能结束内核线程或其他进程、重启或关闭系统、改写网络配置；被 kill 的任务若正处于内核路径中（持锁或阻塞），其内核栈被立即释放。
- 修复方向：在 task_t 中加入凭据（至少 uid 或 is_privileged），对 reboot/poweroff/设置类 ioctl 做特权检查；kill 只允许作用于同属主的用户进程并拒绝内核线程；被 kill 的任务改为设置待处理标志，在其返回用户态前自行走 task_exit。
- 复核意见：src/kernel/syscalls/process.cpp:779-796 的 kill 只排除 pid==0，之后对 get_by_pid 得到的任何任务（含 is_user_process=false 的内核线程）置 ZOMBIE 或直接 Scheduler::free（902 行）；src/kernel/syscall.cpp:281-292 的 reboot/poweroff 包装器无条件调用；src/kernel/syscalls/net.cpp:289-300 的 ioctl 明确 (void)fd 后直接改接口和 ARP 表。include/kernel/task.h 中 grep 不到 uid/privilege 字段，确实没有任何凭据模型。 更正：对一个单用户的业余内核而言“无权限模型”主要是设计缺失，故降为 medium；真正危险的部分（kill 就地释放处于内核路径中的任务）与 kernel-proc-syscalls-10 重叠，应在那一条里修。arm64 未编入网络栈，net ioctl 部分只适用于 i686/x86_64。


### 系统调用与内核初始化（11 项）

#### V055 [critical·已确认·all·已修复] 系统调用层完全没有用户指针校验（execve 路径、waitpid 的 wstatus、uname 的 buf、nanosleep 的 req/rem）

- 位置：`src/kernel/syscall.cpp:113`；相关：`src/kernel/syscalls/process.cpp:984`、`src/kernel/syscalls/process.cpp:735`、`src/kernel/syscalls/process.cpp:756`、`src/kernel/syscalls/system.cpp:30`
- 证据：sys_execve_wrapper: `path[i] = user_path[i];` 直接解引用用户传入地址；process.cpp:984 `*wstatus = status;`；syscalls/system.cpp:30 `memset(buf, 0, sizeof(struct utsname));`；process.cpp:735/756 直接读 req、写 rem。全仓库 grep 不到任何 copy_from_user/copy_to_user/access_ok 类辅助函数，唯一的检查是 `!= NULL`。同时 i686/x86_64 的 page_fault_handler（arch/i686/interrupt/isr.cpp:115 起）对无法处理的缺页一律 `cli; hlt` 挂起整机。
- 触发场景：用户程序把一个未映射的地址（或内核区地址）作为 exec 的 path、waitpid 的 wstatus、uname 的 buf 传入：未映射地址在内核态触发缺页，整机被挂起；内核区地址则被内核以 ring0 权限读取或写入（uname 会对该地址 memset 325 字节并写入字符串，waitpid 会写入 4 字节状态值），造成内核内存被破坏。
- 修复方向：引入统一的 copy_from_user / copy_to_user / strncpy_from_user：先检查 [ptr, ptr+len) 完全落在用户地址范围内且已映射（或带异常修复表），所有 syscall 包装器先把参数复制到内核缓冲区再使用；缺页处理对来自用户指针访问的内核态异常返回 -EFAULT 而不是挂机。
- 复核意见：src/kernel/syscall.cpp:113 直接执行 path[i] = user_path[i]，syscalls/system.cpp:30 对用户传入的 buf 直接 memset，syscalls/process.cpp:728-756 直接读 req、写 rem，process.cpp:983-984 直接 *wstatus = status，均只有非 NULL 检查。全 src 目录 grep copy_from_user/copy_to_user/access_ok/validate_user 无任何结果。i686 的 page_fault_handler（arch/i686/interrupt/isr.cpp:115-166）在内核缺页同步和 COW 处理都失败后无条件 cli; hlt，x86_64 的 isr64.cpp 同样以 cli; hlt 结尾，因此用户态传入坏指针即可挂死整机或让内核以 ring0 写任意内核地址。 更正：arm64 上的挂机路径（exception.cpp 中的 while(1)）我未逐行追踪，指针无校验这一点三架构共用同一份代码；i686 的缺页处理即使故障来自用户态也同样挂机，这是另一个独立问题。

#### V056 [critical·已确认·all·已修复] 系统调用层缺少统一的用户指针与长度校验

- 位置：`src/kernel/syscall.cpp:508`；相关：`src/kernel/syscalls/fs.cpp:258`、`src/kernel/syscalls/fs.cpp:317`、`src/kernel/syscalls/fs.cpp:637`、`src/kernel/syscalls/fs.cpp:721`、`src/kernel/syscalls/fs.cpp:780`、`src/kernel/syscalls/process.cpp:756`
- 证据：仓库中不存在 access_ok / copy_from_user / copy_to_user / strncpy_from_user 一类的辅助函数；syscall_dispatcher 直接调用 handler，各 wrapper 把参数强转为指针后原样传给实现，实现里只判空。fs.cpp:720 的注释也承认此处应做用户空间内存检查。缺页处理中没有异常修复表。
- 触发场景：用户态传入的指针、长度和字符串未经校验即在内核态使用，用户与内核之间的隔离在所有带指针参数的系统调用上都不成立；无效地址还会导致内核态缺页并停机。
- 修复方向：引入统一的用户访问层：按架构实现 access_ok（检查 ptr+len 不溢出且位于用户地址范围内）以及 copy_from_user / copy_to_user / strncpy_from_user，并在缺页处理中加入异常修复表以返回 -EFAULT。所有 wrapper 先把用户数据拷入内核缓冲再调用 VFS、网络等子系统；路径统一拷入定长缓冲。之后再启用 SMEP/SMAP（x86）与 PAN（arm64）。
- 复核意见：在 src 全目录 grep access_ok/copy_from_user/copy_to_user/validate_user/fixup/extable 无任何命中；syscall_dispatcher（src/kernel/syscall.cpp:495-507）只检查调用号后直接调用 handler，包装器（syscall.cpp:132-162）把参数强转为指针原样传入。实现中如 Fs::read 直接把用户 buf 交给 Vfs::read 写入、getdents 在 fs.cpp:720-721 注释承认未做用户内存检查后直接 memcpy，仅有判空。用户传入内核地址即可让内核向任意内核内存写入文件内容，属于可从用户态触发的内存破坏。 更正：mm.cpp:459 的 munmap 有 USER_SPACE_END 范围检查，属个别例外，但不是通用的用户指针校验；结论不变。

#### V057 [critical·已确认·all·已修复] 所有文件/网络系统调用都不校验用户指针，用户态可任意读写内核内存

- 位置：`src/kernel/syscalls/fs.cpp:258`；相关：`src/kernel/syscalls/fs.cpp:92`、`src/kernel/syscalls/fs.cpp:125`、`src/kernel/syscalls/fs.cpp:317`、`src/kernel/syscalls/fs.cpp:637`、`src/kernel/syscalls/fs.cpp:721`、`src/kernel/syscalls/fs.cpp:780`
- 证据：`uint32_t bytes_read = fs::Vfs::read(entry->node, entry->offset, count, (uint8_t *)buf);` buf 直接来自 sys_read_wrapper 的 `(void *)(uintptr_t)buffer`，只检查了 `!buf`。整个 src/kernel/syscall.cpp 与 syscalls/fs.cpp、syscalls/net.cpp、net/socket.cpp 中没有任何 copy_from_user/copy_to_user/地址范围检查（grep 无结果）。同样的模式：stat/fstat 的 `memset(buf,0,sizeof(struct stat))`、getcwd 的 `strcpy(buffer, current->cwd)`、getdents 的 `memcpy(dirent, dir_entry, sizeof(struct dirent))`（源码注释自己写着“这里应该进行用户空间内存检查，简化实现直接复制”）、pipe 的 `fds[0] = read_fd`、write 中逐字节读取 user_ptr、路径字符串直接 strlen/strcpy、ioctl 的 argp、socket 的 addr/addrlen/optval/fd_set。
- 触发场景：用户程序执行 `read(fd, (void*)0x80100000 /*内核代码或数据地址*/, n)`（i686 内核基址 0x80000000），内核以 ring0 把文件内容写入内核自身 -> 任意内核写，提权或崩溃；`write(1, (void*)内核地址, n)` 把内核内存打印出来 -> 任意内核读；`pipe((int*)内核地址)`、`fstat(0, 内核地址)`、`getcwd(内核地址, 256)` 同理。传入未映射的用户地址（如 `read(0,(void*)4,10)`）则在内核态缺页，可能在持有 FS 锁/互斥锁时触发 panic。
- 修复方向：在 syscall 层引入统一的 user access 接口：校验 [ptr, ptr+len) 完全位于 USER_SPACE_END 以下且已映射/可写（或带 fixup 的 copy_from_user/copy_to_user/strncpy_from_user），路径先拷入内核缓冲区（限长并保证 NUL 结尾），输出结构先在内核栈上构造再 copy_to_user；所有 wrapper 统一经过该接口。
- 复核意见：src/kernel/syscalls/fs.cpp:258 把用户传入的 buf 仅做判空后直接交给 fs::Vfs::read 作为内核写目标。在 src 下 grep copy_from_user/copy_to_user/access_ok/validate_user 无任何实现，USER_SPACE_END 在 syscalls 目录只被 mm.cpp:459 (munmap) 使用，x86_64_is_user_address 只有定义无调用者。i686 内核位于 0x80000000 且映射在每个用户页目录里，因此 read/write/stat/getcwd/pipe 等可被用户态用来任意读写内核内存。 更正：未逐一核对 related_locations 中 net/socket.cpp 的各行，但根因（全局缺少用户指针校验层）已确认；arm64 未编译网络栈，net 部分只适用于 i686/x86_64。
- 被 2 个独立审计者重复报告（kernel-fs-net-syscalls-1, x-doc-drift-1）

#### V058 [critical·已确认·all·已修复] normalize_path 的栈帧（8372 字节）大于整个内核栈（8192 字节），chdir 系统调用必然写穿内核栈破坏内核堆

- **修复**：Removed normalize_path and its 64x128 on-stack component array from src/kernel/syscalls/fs.cpp. chdir now uses the new path_resolve() in src/lib/string.cpp, which normalises directly in the output buffer; the resolved path sits in a kmalloc'd buffer (ResolvedPath RAII class), not on the kernel stack. The suggested guard page / larger kernel stack was not done (task.cpp is outside this batch). 验证方式：unit tests test_path_normalize_basic/_dotdot/_deep/_overflow and test_path_resolve (path_tests suite in src/tests/lib/string_test.cpp, pass on all three archs); chdir itself boot regression only
- 位置：`src/kernel/syscalls/fs.cpp:436`；相关：`src/kernel/kernel_shell.cpp:197`、`src/include/kernel/task.h:23`、`src/kernel/syscalls/fs.cpp:592`
- 证据：`const size_t MAX_COMPONENTS = 64; char components[MAX_COMPONENTS][128];`（8192 字节）加上 `char current_component[128]`。已构建的 build/i686/castor.bin 反汇编中该函数序言为 `sub $0x20b4,%esp`（8372 字节；x86_64 为 8432，arm64 为 8448）。而每个任务的内核栈是 `kmalloc(KERNEL_STACK_SIZE)`，`#define KERNEL_STACK_SIZE (8 * 1024)`（src/include/kernel/task.h:23），没有保护页。调用链 int 0x80 → syscall_dispatcher → sys_chdir_wrapper → syscall::Fs::chdir（自身帧 1064 字节）→ normalize_path。current_component 位于 ebp-0x20ac，components 位于 ebp-0x202c。
- 触发场景：用户 shell 执行 `cd /foo`（user/shell/shell.cpp:767 调 chdir）。进入 normalize_path 时 esp 已在栈顶下约 1.3KB，再减 8372 字节后 current_component 落在 kernel_stack_base 之下约 1450 字节处，components[k] 落在 base-1322+128k 处；随后的写入（current_component 逐字节写、strncpy 把每个 components[k] 填满 127 字节、strcmp/strncpy/strlen 的调用帧）全部写进内核栈 kmalloc 块之前的相邻堆块（其数据或 heap_block_t 头）；路径有 ≥11 个分量时还会覆盖栈块自身的堆头 magic。结果是静默的内核堆破坏：相邻对象数据被改写、后续 kmalloc/kfree 的 magic 校验失败或链表损坏。内核 shell 的 `cd` 走 shell_normalize_path（同样 8372 字节帧，8KB 内核线程栈）有同样问题。
- 修复方向：不要在栈上放 64×128 的二维数组：改为在输出缓冲区内原地规范化（遇到 `..` 回退到上一个 '/'），或用 kmalloc 分配 components；同时把内核栈加大并在栈底加未映射的保护页/栈金丝雀，使溢出能被检测而不是静默破坏堆。
- 复核意见：src/kernel/syscalls/fs.cpp:436-440 的 normalize_path 局部变量为 components[64][128]（8192 字节）加 current_component[128]，单个栈帧已超过 KERNEL_STACK_SIZE=8*1024（src/include/kernel/task.h:23）。内核栈由 kmalloc 分配（task.cpp:511/634、process.cpp:214），无保护页，Makefile:88 为 -O0 不会消除该数组。chdir（fs.cpp:545/592，自身还有两个 512 字节缓冲区）经 SYS_CHDIR（syscall.cpp:555）由用户 shell 的 cd 直接到达，current_component 的逐字节写入必然落在栈块之下的堆内存。kernel_shell.cpp:197 的 shell_normalize_path 有同样的数组。 更正：反汇编得到的具体帧大小（8372/8432/8448）未复核，但仅凭源码中数组大小即可证明溢出；具体破坏哪个相邻堆块取决于分配顺序。
- 被 4 个独立审计者重复报告（arch-i686-core-2, gap6-abi-struct-parity-lp64-2, kernel-fs-net-syscalls-3, user-shell-2）

#### V059 [high·已确认·i686·已修复] i686 内核堆的虚拟区间取自直接映射窗口，且 set_heap_reserved_range 在 VMM 已占用该区间帧之后才调用：内核页表的直接映射别名被堆覆盖，堆超过约 13.4MB 后 PTE 被写进堆首页

- **修复**：Added heap_start_after_used_frames() in kernel.cpp: before Heap::init on i686/x86_64, heap_start is moved past every frame in the candidate 32MB range that already has a refcount (the page tables Vmm::init allocated). The heap's reserved physical range therefore no longer contains live page-table frames, so the i686 heap remap cannot break their PHYS_TO_VIRT alias. This is the in-ownership fix, not the reviewer's preferred redesign (see notes). 验证方式：boot regression only: the i686 log shows 'Heap start moved past allocated frames: 0x80290000 -> 0x802ac000' (28 frames); growing the heap past 13.4MB was not exercised
- 位置：`src/kernel/kernel.cpp:456`；相关：`src/kernel/kernel.cpp:485`、`src/mm/pmm.cpp:1035`、`src/mm/vmm.cpp:355`、`src/mm/heap.cpp:114`、`src/arch/i686/mm/paging.cpp:615`、`src/arch/i686/mm/paging.cpp:624`
- 证据：kernel.cpp:456 `uintptr_t heap_start = mm::Pmm::get_bitmap_end();`（= PHYS_TO_VIRT(refcount_end_phys)，即第一个空闲帧的直接映射地址），:481 Heap::init，:485 才调用 `mm::Pmm::set_heap_reserved_range(heap_start, heap_start + heap_size)`。而此前 Vmm::init (src/mm/vmm.cpp:355) 已经用 alloc_frame() 为 PDE 516.. 分配了页表，first-fit 使它们正好是 refcount_end_phys 起的连续帧 P0..P(N-1)（128MB 时 N=28，当前构建 P0=0x28c000）。pmm.cpp:1035 `if (!test_frame(f))` 直接跳过这些已分配帧。i686 的 expand() (src/mm/heap.cpp:99-114) 再为堆 VA 映射新帧，于是 PHYS_TO_VIRT(Pk)=heap_start+k*4K 不再指向页表 Pk。hal::Mmu::map (src/arch/i686/mm/paging.cpp:615,624) 访问已有页表用的正是 `table = (page_table_t*)PHYS_TO_VIRT(table_phys); table->entries[pt_idx] = ...`。
- 触发场景：默认 128MB：heap_start=0x8028c000。内核堆增长到 heap_end>=0x81000000（约 13.45MB，例如 ramfs 写入数 MB 文件触发 kmalloc(new_capacity) 倍增、execve 的 kmalloc(file_size)、帧缓冲 back buffer 加上大量任务内核栈）时，expand() 调 Vmm::map_page(0x81000000, F)：PDE 516 存在，table_phys=P0，PHYS_TO_VIRT(P0)=0x8028c000 即堆第一页，`table->entries[0]=F|flags` 覆盖 first_block->size，后续页依次覆盖 is_free/next/prev/magic。真正的页表没有被修改（新帧 F 泄漏）。结果：first_block 被标成“空闲且巨大”导致在用内存被再次分配，或 magic 被破坏后所有 kmalloc 返回 NULL，内核堆永久损坏。另外该 32MB 物理区间被标为已用却从不使用（128MB 机器浪费 25% 内存；RAM 小于约 35MB 时保留后 free_frames 归零无法启动）。
- 修复方向：不要让 i686 堆与直接映射窗口重叠：给堆单独划一段不属于直接映射的内核虚拟区间；或像 x86_64/arm64 那样让堆直接使用直接映射内存（堆 VA 恒等于 PHYS_TO_VIRT(phys)，不再 map_page 重映射），并且在 Pmm::init 内、任何 alloc_frame 之前就保留堆的物理区间。set_heap_reserved_range 遇到区间内已被占用的帧应 PANIC 而不是静默跳过。
- 复核意见：已核对：kernel.cpp:442-485 顺序确为 Pmm::init → Vmm::init → Heap::init(heap_start=get_bitmap_end()) → set_heap_reserved_range；grub.cfg 无 multiboot 模块，PMM 只释放 refcount_end_phys 之后的帧（pmm.cpp:349-350）且首次适应，故 vmm.cpp:355 为 PDE 516+ 分配的页表帧正是 heap_start 对应的物理帧，pmm.cpp:1035 的 `if (!test_frame(f))` 对它们静默跳过。i686 expand()（heap.cpp:99-114）把堆 VA 重映射到新帧（前 16MB 由 boot.asm 的 4K 引导页表映射，可被改写），此后 paging.cpp:614-624 通过 PHYS_TO_VIRT(table_phys) 访问 PDE 516 的页表时实际写到堆首页，entries[0] 覆盖 first_block 头部。堆越过 0x81000000 后每扩一页就破坏堆首页 4 字节，机制成立。 更正：触发条件是内核堆增长超过约 13.4MB（128MB 配置），未动态验证日常负载能否达到，但 32MB 上限内属设计允许范围。补充：越过 0x81000000 的堆页本身仍可用（真实 PTE 未改，仍直映到被保留的物理帧），expand 分配的帧泄漏；实际损害是堆首页被 PTE 值逐字覆盖。P0=0x28c000 等具体数值未核实，取决于构建。
- 被 2 个独立审计者重复报告（mm-pmm-2, x-error-init-6）

#### V060 [high·已确认·i686·已修复] net::Stack::init() 从未被调用：协议栈初始化被跳过，TCP 定时器永远不注册（无重传、无超时、无 TIME_WAIT 回收）

- **修复**：src/kernel/kernel.cpp step 4.7 now calls net::Stack::init() (which includes Netdev::init and registers the TCP timer through kernel::Deferred) followed by net::Dns::init(), after Timer::init and before E1000::init. Applies to i686 and x86_64. 验证方式：boot regression only: the boot log shows 'net: Network stack initialized' on i686 and x86_64 and the existing net tests still pass; TCP retransmission was not exercised
- 位置：`src/kernel/kernel.cpp:534`；相关：`src/net/net.cpp:22`、`src/net/net.cpp:51`、`src/net/tcp.cpp:1316`、`src/net/dns.cpp:532`
- 证据：kernel_main 只调用 `net::Netdev::init();`（4.7）和 `drivers::E1000::init()`。全仓库搜索 `Stack::init(`、`net_init` 只有 src/net/net.cpp:22 的定义，没有任何调用点（C->C++ 迁移之前的 net_init 同样没人调用）。net::Stack::init 负责 Arp/Ip/Icmp/Udp/Tcp/Socket 的 init，并且是唯一注册 TCP 定时器的地方：`tcp_timer_id = drivers::Timer::register_callback(net_tcp_timer_callback, NULL, 100, true);`（net.cpp:51）。net::Tcp::timer() 只被这个回调调用，它承担重传定时器、重传次数超限后中止连接、TIME_WAIT 2MSL 回收。协议栈目前能工作只是因为各模块的静态变量恰好是零/默认值（Spinlock 的零值等于未上锁）。
- 触发场景：用户程序 connect() 到一个不可达或丢包的对端：SYN 发出后丢失，由于 Tcp::timer 从不运行，既不会重传也不会在 TCP_MAX_RETRIES 后调用 error_callback，connect()/recv() 永远阻塞；任何丢失的数据段都不会重传，连接卡死；关闭后的连接停在 TIME_WAIT，PCB 永不释放，反复建连会耗尽 PCB 和端口。另外 tcp_isn 初值为 0，ISN 可预测。
- 修复方向：在 kernel_main 的 4.7 处调用 net::Stack::init()（它内部已包含 Netdev::init），并且要放在 drivers::Timer::init 之后、E1000::init 之前；net::Dns::init() 同样没有调用点，一并补上。
- 复核意见：全仓库 grep `Stack::init`、`net_init` 只有 src/net/net.cpp:22 的定义，无调用点；kernel.cpp:534 只调用 net::Netdev::init()。TCP 定时器只在 net.cpp:50 注册，net::Tcp::timer()（tcp.cpp:1316，负责重传、超限中止）唯一调用者是 net.cpp:19 的回调，因此重传与超时逻辑在运行时永不执行。net::Dns::init（dns.cpp:532）同样无调用者。i686 上 ping 能工作与此不矛盾（ICMP 不依赖定时器）。 更正：实际受影响的只有 i686：x86_64 上 e1000 被跳过、arm64 不编译网络栈。Arp/Ip/Udp/Socket 各 init 被跳过的具体后果未逐一核对，主要确定的后果是 TCP 定时器缺失。
- 被 2 个独立审计者重复报告（kernel-init-shell-3, x-error-init-8）

#### V061 [high·已确认·x86_64,arm64·已修复] 64 位架构上系统调用返回值由 uint32_t 零扩展到 syscall_arg_t，错误码 -1 变成 0x00000000FFFFFFFF

- **修复**：Added sys_ret32() in src/kernel/syscall.cpp and routed every wrapper whose implementation returns uint32_t through it (fork, execve, open, close, read, write, lseek, mkdir, unlink, chdir, getdents, stat, fstat, ftruncate, pipe, dup, dup2, rename, getpid, getppid, yield, nanosleep, kill, waitpid, uname). time is left unextended on purpose. read/write lengths are capped at INT32_MAX before validation, so a byte count cannot look negative and the 64-bit length is no longer silently truncated. 验证方式：unit test test_arm64_syscall_error_sign_extended (arm64 only); x86_64 boot regression only
- 位置：`src/kernel/syscall.cpp:149`；相关：`src/kernel/syscall.cpp:136`、`src/kernel/syscall.cpp:155`、`src/kernel/syscall.cpp:161`、`src/kernel/syscall.cpp:93`、`src/include/kernel/syscalls/fs.h:56`、`src/include/kernel/syscalls/mm.h:27`
- 证据：`return syscall::Fs::read(...)`，而 Fs::read/write/lseek 等声明为 `static uint32_t ...`（include/kernel/syscalls/fs.h:56,68,102），失败时返回 `(uint32_t)-1`；包装器返回类型是 64 位的 syscall_arg_t，发生零扩展。只有 sys_close_wrapper 做了 `(syscall_arg_t)(int32_t)` 符号扩展（143 行）。用户库 64 位下 `typedef int64_t ssize_t/off_t`（user/lib/include/types.h:25-26），`read()` 直接 `(ssize_t)syscall3(...)`。
- 触发场景：x86_64/arm64 用户程序 `ssize_t n = read(fd, buf, len); if (n < 0) ...`：read 失败时内核在 RAX/X0 中返回 0xFFFFFFFF，用户得到 n = 4294967295（正数），错误检查失效，随后按 n 处理缓冲区造成越界；lseek 失败同理，fork 的 -12（ENOMEM）在以 64 位类型接收时也变成巨大的正 pid。
- 修复方向：所有 syscall 实现统一返回有符号的机器字（intptr_t/long），失败返回负 errno；分发器/包装器对 32 位返回值统一做符号扩展；同时把 mm.h 中 brk/mmap/munmap 的 `uint32_t addr/length` 参数改为 uintptr_t/size_t（当前在 64 位上地址被截断）。
- 复核意见：include/kernel/syscalls/fs.h:56/68/102 声明 read/write/lseek 返回 uint32_t，fs.cpp:79 等处失败时返回 (uint32_t)-1；syscall.cpp:147-162 的包装器直接 return 到 syscall_arg_t（syscall.h:11 为 uintptr_t），在 64 位上是零扩展，只有 close 包装器（syscall.cpp:143）做了 (int32_t) 符号扩展。用户库 64 位下 ssize_t 为 int64_t（user/lib/include/types.h:25），read() 直接 (ssize_t)syscall3(...)（user/lib/src/syscall.cpp:84-86），所以失败时得到 4294967295。fork 返回 (uint32_t)-12（process.cpp:92）经 sys_fork_wrapper（syscall.cpp:93）同样零扩展。 更正：返回 int 的用户库函数（如 close、以及以 int/pid_t 接收的 fork）截断后仍为负值，实际受影响的是以 64 位类型接收返回值的调用，如 read/write/lseek。mm.h 中 brk/mmap 参数截断属于另一个问题，本次未核对。
- 被 2 个独立审计者重复报告（kernel-proc-syscalls-7, x-user-boundary-9）

#### V062 [high·已确认·x86_64,arm64·已修复] 64 位上 read/write/lseek 的错误返回值 (uint32_t)-1 未符号扩展，用户态得到 +4294967295；arm64 测试显式把 0xFFFFFFFF 当作合法错误

- **修复**：Same sign-extension fix. src/tests/arch/arm64/arm64_syscall_test.cpp asserted the old behaviour (accepted result == 0xFFFFFFFF as a valid error): test_arm64_syscall_write_dispatch now requires (intptr_t)result < 0, and a new case does lseek on a bad fd so the error comes from the implementation. 验证方式：unit tests test_arm64_syscall_write_dispatch (updated) and test_arm64_syscall_error_sign_extended, pass on arm64
- 位置：`src/kernel/syscall.cpp:155`；相关：`src/kernel/syscall.cpp:149`、`src/kernel/syscall.cpp:161`、`src/kernel/syscalls/fs.cpp:272`、`src/include/kernel/syscalls/fs.h:68`、`src/tests/arch/arm64/arm64_syscall_test.cpp:134`、`user/lib/src/syscall.cpp:89`
- 证据：`return syscall::Fs::write((int32_t)fd, (const void *)(uintptr_t)buffer, (size_t)size);`，而 syscall::Fs::write/read/lseek 返回 uint32_t 且错误时 `return (uint32_t)-1;`（src/kernel/syscalls/fs.cpp:272 等几十处）。syscall_arg_t 是 uintptr_t（64 位），所以返回 0x00000000FFFFFFFF。只有 close 的包装器做了 `(syscall_arg_t)(int32_t)` 符号扩展（syscall.cpp:143）。用户库 user/lib/src/syscall.cpp:89 `return (ssize_t)syscall3(SYS_WRITE,...)`，ssize_t 为 64 位。测试 src/tests/arch/arm64/arm64_syscall_test.cpp:133-135 写明 `bool is_error = (signed_result < 0) || (result == 0xFFFFFFFF);` 并注释“This is a valid error indication”，把该 ABI 错误固化为预期行为。
- 触发场景：x86_64/arm64 用户程序执行 `ssize_t n = read(bad_fd, buf, sizeof buf); if (n < 0) 出错处理;` —— n == 4294967295 > 0，被当成成功读到 4GB 数据，随后按 n 处理缓冲区导致用户态越界/死循环；write 失败同样被当作成功。
- 修复方向：让 Fs::read/write/lseek 返回有符号类型（ssize_t / int64_t）或在所有包装器里统一 `(syscall_arg_t)(intptr_t)(int32_t)ret` 符号扩展；测试改为只接受 `(intptr_t)result < 0`。
- 复核意见：src/kernel/syscall.cpp:146-162 的 read/write/lseek 包装器直接把 uint32_t 返回值隐式转成 syscall_arg_t（src/include/kernel/syscall.h:11 为 uintptr_t），而 src/include/kernel/syscalls/fs.h:56/68/102 声明返回 uint32_t、错误路径 return (uint32_t)-1，只有 close 包装器(syscall.cpp:143)做了 (int32_t) 符号扩展。用户库 user/lib/src/syscall.cpp:84-96 把 64 位 syscall3 结果直接转 ssize_t/off_t，64 位下得到 +4294967295，n<0 判断失效。src/tests/arch/arm64/arm64_syscall_test.cpp:133-135 确实把 0xFFFFFFFF 当作合法错误。 更正：返回 int 的用户封装（open/mkdir 等，(int) 截断）不受影响，受影响的是返回 ssize_t/off_t 的 read/write/lseek 等；另外 Fs::read/write 的 count 形参也是 uint32_t，64 位上长度被截断，修复时应一并改为 size_t/ssize_t。

#### V063 [high·已确认·all·已修复] 路径类系统调用不按进程 cwd 解析相对路径，相对路径一律相对根目录

- **修复**：open, stat, mkdir, unlink, rename and chdir in fs.cpp, plus the execve wrapper in syscall.cpp, now resolve the user path against current->cwd via path_resolve before calling the VFS. Empty paths and results over 512 bytes fail with -1. 验证方式：unit test test_path_resolve covers the resolution logic; the syscalls themselves are boot regression only (the test context has no task with an fd table, so no in-kernel syscall test was added)
- 位置：`src/kernel/syscalls/fs.cpp:147`；相关：`src/kernel/syscalls/fs.cpp:85`、`src/kernel/syscalls/fs.cpp:396`、`src/kernel/syscalls/fs.cpp:414`、`src/kernel/syscalls/fs.cpp:919`、`src/kernel/syscall.cpp:122`、`src/fs/vfs.cpp:317`
- 证据：open: `fs_node_t *node = fs::Vfs::path_to_node(path);`，path 原样传入；path_to_node 对不以 '/' 开头的路径直接 `fs_node_t *current = fs_root;` 开始解析（vfs.cpp:317-321）。stat/mkdir/unlink/rename/execve 也都把用户路径原样交给 VFS，只有 chdir 自己拼接了 current->cwd。用户 shell 不得不在用户态用 shell_resolve_path + getcwd 自己拼绝对路径来绕过。
- 触发场景：用户程序先 `chdir("/home")`，再 `open("a.txt", O_CREAT|O_WRONLY)` / `mkdir("d")` / `unlink("a.txt")` / `stat("a.txt")`：内核实际操作的是 /a.txt、/d，而不是 /home/a.txt。结果是文件建错位置、unlink 删除了根目录下的同名文件（数据丢失）、stat 报告不存在。另外相对路径如 "dev/null" 不匹配挂载表前缀（挂载匹配要求以 "/dev" 开头），会解析到根文件系统上空的 dev 目录而不是 devfs。
- 修复方向：在 syscall 层增加统一的路径解析函数：把用户路径拷入内核缓冲区，若不以 '/' 开头则与 current->cwd 拼接并规范化（处理 . 和 ..），所有路径类系统调用（open/stat/mkdir/unlink/rename/chdir/execve）都使用它。
- 复核意见：src/kernel/syscalls/fs.cpp:85（stat）、:147（open）、:396（mkdir）、:414（unlink）、:919（rename）都把用户 path 原样交给 fs::Vfs，grep cwd 显示 fs.cpp 中只有 chdir(555-605) 和 getcwd 使用 current->cwd。src/fs/vfs.cpp:315-320 对不以 '/' 开头的路径只是不跳过前导斜杠，仍从 fs_root 开始解析，因此相对路径一律相对根目录。chdir 后 open/unlink 相对路径会操作根目录下的同名文件。 更正：自带 shell 在用户态拼绝对路径规避了该问题，实际影响主要是其他用户程序直接使用相对路径时；严重度 high 偏上限，可视为 medium-high。

#### V064 [medium·已确认·i686,x86_64·已修复] 内核 Shell 的 shell_normalize_path 栈帧 (约 8.4KB) 大于内核线程栈 (8KB)，任何带路径参数的命令都会越界写堆

- **修复**：Deleted shell_normalize_path (same 8KB array) from src/kernel/kernel_shell.cpp; shell_resolve_path is now a one-line call to path_resolve(shell_state.cwd, ...), which also drops its two 256-byte temporaries. 验证方式：same path_tests unit tests; the kernel shell is a fallback path and was not exercised at boot
- 位置：`src/kernel/kernel_shell.cpp:197`；相关：`src/include/kernel/task.h:23`、`src/kernel/task.cpp:511`、`src/kernel/kernel.cpp:668`、`src/kernel/kernel_shell.cpp:303`
- 证据：`const size_t MAX_COMPONENTS = 64; char components[MAX_COMPONENTS][128];`（8192 字节）再加 `char current_component[128]`。已在现有构建产物中核对函数序言：i686 为 `sub $0x20b4,%esp`（8372 字节），x86_64 为 `sub $0x20f0,%rsp`（8432 字节）。而内核 Shell 以内核线程运行（kernel.cpp:668 `create_kernel_thread(kernel::Shell::run, "kernel_shell")`），其栈是 `kmalloc(KERNEL_STACK_SIZE)`，KERNEL_STACK_SIZE = 8*1024（task.h:23, task.cpp:511）。调用链 Shell::run -> execute_command -> cmd_xxx(abs_path[256]) -> shell_resolve_path(normalized[256]+temp_path[256]) -> shell_normalize_path，单是最后一帧就超过整个栈。x86_64 上入口处立即把参数写到 -0x20d8(%rbp)，已位于栈底之下。
- 触发场景：用户 Shell 加载失败（磁盘上没有 /bin/shell.elf）后进入内核 Shell，输入 `cd /bin`、`ls /dev`、`cat x`、`touch x`、`mkdir x`、`rm x`、`write x y` 中任意一个：shell_normalize_path 的栈帧越过 kernel_stack_base，写坏该栈 kmalloc 块的 heap_block_t 头部（magic/size/next/prev）以及堆中在它之前的块（idle 任务栈、ramfs 数据等）；期间到来的定时器/键盘中断还会在更低地址压入寄存器帧。随后的 kmalloc/kfree 报 magic 损坏或沿着损坏的链表访问野指针，内核堆永久损坏或直接崩溃。
- 修复方向：不要在栈上放 8KB 数组：改为在输出缓冲区内原地规范化（遇到 `..` 回退到上一个 '/'），或把 components 改成 static/kmalloc；同时给内核线程栈加保护页或栈底 canary，并考虑把 KERNEL_STACK_SIZE 提高到 16KB。
- 复核意见：src/kernel/kernel_shell.cpp:196-201 确实在栈上声明 components[64][128]（8192 字节）加 current_component[128]，而内核线程栈为 kmalloc(KERNEL_STACK_SIZE)=8KB（task.h:23、task.cpp:511），无保护页；数组在循环中被运行时下标读写，编译器无法消除。调用链 cmd_* -> shell_resolve_path(298 行，另有 512 字节局部缓冲) -> shell_normalize_path(308/353 行) 成立，单帧已超过整个栈，必然越过 kernel_stack_base 写入相邻堆内存。但 kernel.cpp:659-668 表明内核 Shell 仅在 load_user_shell() 失败时才启动，属于回退路径，故降为 medium。反汇编的序言数值我未独立核对，但源码层面的帧大小已足以证明。 更正：触发条件是用户 Shell 加载失败的回退路径，不是正常运行路径。路径只有 1-2 个组件时实际被写的是 components[0..1]（位于栈底之下约 1KB 处的相邻堆块数据）以及被调函数和中断压入的帧，不一定直接命中本栈块的 heap 头部；具体破坏对象取决于堆布局。仅 i686/x86_64 编译（kernel.cpp:57 的 #if）。

#### V065 [medium·已确认·all·已修复] open(O_CREAT|O_EXCL) 永远失败：O_EXCL 检查放在创建之后，新建的文件也被当成“已存在”

- **修复**：Fs::open now checks O_CREAT|O_EXCL against the node found before creation, so only a pre-existing file fails; a file created by this call is opened normally. 验证方式：boot regression only
- 位置：`src/kernel/syscalls/fs.cpp:164`；相关：`src/kernel/syscalls/fs.cpp:150`
- 证据：先 `if (!node && (flags & O_CREAT)) { fs::Vfs::create(path); node = fs::Vfs::path_to_node(path); }`，之后才 `if ((flags & O_CREAT) && (flags & O_EXCL)) { ... release_node(node); return (uint32_t)-1; }`，该判断不区分文件是原本存在还是刚刚由本次调用创建。
- 触发场景：用户程序 `open("/lock", O_CREAT|O_EXCL|O_WRONLY)`（文件原本不存在）：内核创建了文件，随即命中 O_EXCL 分支返回 -1，并打印 “exists but O_EXCL specified”。调用者得到失败但磁盘上留下了空文件；下次重试同样失败。任何基于 O_EXCL 的锁文件/临时文件逻辑都不可用。
- 修复方向：在第一次 path_to_node 之后记录 `bool existed = (node != NULL)`，只有 `existed && (flags & O_CREAT) && (flags & O_EXCL)` 时才返回 EEXIST。
- 复核意见：src/kernel/syscalls/fs.cpp:147-158 在文件不存在且带 O_CREAT 时先 Vfs::create 再重新 path_to_node，随后 :164-168 的 O_EXCL 判断只看 flags，不区分节点是原有还是刚创建，必然 release_node 并返回 -1。因此 O_CREAT|O_EXCL 在任何情况下都失败，且新建时会留下空文件。 更正：功能性缺陷而非崩溃或内存破坏，且 O_EXCL 属较少使用路径，严重度下调为 medium；修复方案（记录 existed 标志，仅 existed 时返回 EEXIST）正确。


### 文件系统（12 项）

#### V066 [critical·已确认·i686,x86_64·已修复] FAT32 没有共享的 in-core inode：每次路径查找都生成独立的 fat32_file_t 快照，多次打开同一文件会互相覆盖目录项并造成簇丢失/交叉链接

- **修复**：Added a per-filesystem in-core table in src/fs/fat32.cpp keyed by directory-entry location. finddir now returns the same fs_node_t (extra reference via new fs::Vfs::try_ref_node) while the file is in use, so all openers share one start_cluster/size and node->size stays current. A node whose refcount already hit zero is not revived; a new node takes over its state. release_impl removes the entry from the table. fat32_file_read now reads size/start_cluster under fs_lock. 验证方式：unit test test_fat32_shared_incore_node (RAM block device volume), passes on i686 and x86_64; boot regression
- 位置：`src/fs/fat32.cpp:1744`；相关：`src/fs/fat32.cpp:858`、`src/fs/fat32.cpp:949`、`src/fs/fat32.cpp:1491`、`src/fs/vfs.cpp:378`、`src/kernel/syscalls/fs.cpp:147`
- 证据：fat32_dir_finddir 每次都 `fat32_file_t *new_file = (fat32_file_t *)kmalloc(sizeof(fat32_file_t)); ... new_file->start_cluster = cluster; new_file->size = lookup->entry.file_size; new_file->dirent_cluster = lookup->cluster;`，而 fs::Vfs::path_to_node (src/fs/vfs.cpp:378) 没有任何节点缓存，syscall::Fs::open 每次 open 都得到一个新节点。start_cluster/size 只是打开瞬间的私有拷贝，之后 fat32_file_write/fat32_file_truncate 都用这份陈旧拷贝调用 fat32_update_dirent_metadata 回写目录项 (882-884 行)。fs_lock 只保证单次操作互斥，不保证两个节点之间状态一致。
- 触发场景：1) 进程 A open("/log", O_CREAT|O_WRONLY)（空文件，start_cluster=0,size=0），进程 B 也 open 同一文件。A 写 100 字节：分配簇 X，目录项变为 (X,100)。B 写 50 字节：B 的副本仍是 start_cluster=0,size=0，于是 fat32_ensure_file_size 再分配簇 Y，目录项被改写为 (Y,50)，簇 X 永久泄漏且 A 的数据丢失。2) A 持有已打开的文件（链 X→...），B 执行 `echo x > file`（O_TRUNC）：fat32_file_truncate 释放整条链并把目录项簇号清 0；A 的 start_cluster 仍指向已释放的簇，A 再写入时直接写进已释放（可能已被别的文件重新分配）的簇，并把目录项改回指向该链，产生交叉链接和跨文件数据损坏。3) 读者节点的 file->size 永远是打开时的值，读不到另一 fd 追加的数据。
- 修复方向：在 fat32_fs_t 中维护按 (dirent_cluster, dirent_offset) 或起始簇索引的 in-core inode 表/哈希，finddir 命中时返回同一个带引用计数的 fat32_file_t（或让 fs_node_t 共享同一 impl），所有 size/start_cluster 修改都在这一个对象上完成；或者在 VFS 层实现 dentry/inode 缓存。
- 复核意见：fat32_dir_finddir（fat32.cpp:1720 起）每次都 kmalloc 新的 fs_node_t 和 fat32_file_t，并从目录项拷贝 start_cluster/size；Vfs::path_to_node（vfs.cpp:320-395）逐级调用 finddir，没有任何节点或 dentry 缓存，syscalls/fs.cpp:147 每次 open 都得到新节点。fat32_file_write（1491-1593 行）用私有的 file->start_cluster/size 调 fat32_ensure_file_size（747-790 行，start_cluster<2 时会重新分配首簇）并在 1591 行回写目录项，fat32_file_truncate 在 990-1010 行同理。因此两个 fd 打开同一文件后的写入/O_TRUNC 会互相覆盖目录项、泄漏簇或写入已释放簇，fs_lock 只保证单次操作互斥，用户态可直接造成数据丢失和交叉链接。 更正：同一进程两次 open 同一文件也能触发，不必是两个进程。修复时 unlink 的打开计数问题（fs-fat32-2）可以一并在 in-core inode 表上解决。

#### V067 [critical·已确认·all·已修复] procfs 各 read 函数的 offset+size 32 位回绕导致内核越界 memcpy（内核内存泄露/崩溃，用户态可触发）

- 位置：`src/fs/procfs.cpp:139`；相关：`src/fs/procfs.cpp:260`、`src/fs/procfs.cpp:399`、`src/fs/procfs.cpp:433`、`src/fs/procfs.cpp:467`、`src/fs/procfs.cpp:501`、`src/fs/procfs.cpp:661`
- 证据：所有 procfs read 都是同一模式：`uint32_t bytes_to_read = size; if (offset + bytes_to_read > file_size) bytes_to_read = file_size - offset; memcpy(buffer, meminfo_buf + offset, bytes_to_read);`。offset 与 size 都是 uint32_t，`offset + bytes_to_read` 会回绕；syscall::Fs::read (src/kernel/syscalls/fs.cpp:257) 把用户传入的 count 原样传给 fs::Vfs::read，没有任何上限或用户指针校验。回绕后截断分支不触发，memcpy 以接近 4GB 的长度从 1024 字节的内核栈缓冲区（或 static 缓冲区）向用户缓冲区拷贝。
- 触发场景：用户程序 open("/proc/meminfo")，先 read(fd, buf, 10) 使 entry->offset=10（或 lseek(fd,10,SEEK_SET)），再 read(fd, buf, 0xFFFFFFF8)。offset(10) < file_size，10+0xFFFFFFF8 回绕为 2，不大于 file_size，于是 bytes_to_read=0xFFFFFFF8，memcpy 从 meminfo_buf+10 开始把内核栈及其后的内核内存连续拷到用户缓冲区：用户缓冲区有多大就泄露多少内核内存，直到读/写到未映射页时在内核态缺页而 panic。/proc/pci、/proc/usb、/proc/net/*、/proc/<pid>/status 同理。
- 修复方向：统一改为不会回绕的写法：`uint32_t remain = file_size - offset; if (bytes_to_read > remain) bytes_to_read = remain;`（offset>=file_size 已提前返回）。最好抽成一个公共的 procfs_copy_out(buf, len, offset, size, dst) 辅助函数；同时在 sys_read 中对 count 设上限并校验用户缓冲区范围。
- 复核意见：src/fs/procfs.cpp:138-143 确实是 `if (offset + bytes_to_read > file_size)` 的 uint32_t 加法，之后直接 memcpy；同一模式在 260/399/433/467/501/661 行重复。src/kernel/syscalls/fs.cpp:232-258 的 syscall::Fs::read 只检查 buf 非空，count 和 entry->offset 原样传给 fs::Vfs::read（vfs.cpp:67-72 也无钳制）。因此 offset>=1 且 count 接近 0xFFFFFFFF 时和回绕，截断分支不触发，memcpy 以近 4GB 长度从 1024 字节内核栈缓冲区向用户缓冲区拷贝，造成内核内存泄露并最终内核态缺页。 更正：arch 应为 i686/x86_64：arm64 的 Makefile 源列表（Makefile:120-166）不包含 procfs.cpp 和 shmfs.cpp。shmfs.cpp:153 虽有同样的回绕，但后面按页链表逐页拷贝，page 为 NULL 即停止，不会越界读内核内存，只是越过 file->size 读到已分配页尾部，危害明显小于 procfs。

#### V068 [critical·已确认·all·已修复] ramfs_read 中 offset + size 的 32 位回绕使长度钳制失效，memcpy 越界读取最多 4GB 内核堆

- 位置：`src/fs/ramfs.cpp:139`；相关：`src/fs/ramfs.cpp:166`、`src/kernel/syscalls/fs.cpp:258`
- 证据：`uint32_t to_read = size; if (offset + to_read > file->size) { to_read = file->size - offset; } memcpy(buffer, file->data + offset, to_read);`。offset、to_read 都是 uint32_t，和会回绕；syscall::Fs::read（syscalls/fs.cpp:258）把用户给的 count 原样传入，没有上限。
- 触发场景：用户进程打开一个 ramfs 文件（如 /bin/shell.elf），先 read 1 字节使 offset=1，再 `read(fd, buf, 0xFFFFFFFF)`：1 + 0xFFFFFFFF 回绕为 0，不大于 file->size，to_read 保持 0xFFFFFFFF，memcpy 从 file->data+1 起向用户缓冲区拷贝约 4GB：先把文件之后的内核堆内容泄露到用户空间，随后访问未映射地址在内核态缺页崩溃。任何 offset>=1 且 size > 0xFFFFFFFF-offset 的读都会触发。
- 修复方向：改为不回绕的比较：`uint32_t avail = file->size - offset; if (to_read > avail) to_read = avail;`。ramfs_write 的 `new_size = offset + size`（ramfs.cpp:166）同样需要溢出检查（溢出时返回错误）。
- 复核意见：src/fs/ramfs.cpp:138-144 为 `uint32_t to_read = size; if (offset + to_read > file->size) to_read = file->size - offset; memcpy(buffer, file->data + offset, to_read);`，offset 与 size 都是 uint32_t，和会回绕。syscall::Fs::read（src/kernel/syscalls/fs.cpp:232-258）对 count 没有上限也不校验用户缓冲区，offset=1、count=0xFFFFFFFF 时钳制失效，memcpy 越界读内核堆直至缺页。ramfs 在 arm64 上是根文件系统（kernel.cpp:252），在 x86 上是找不到 FAT32 时的回退根（fs_bootstrap.cpp:98-100），用户态可达。 更正：与 fs-procfs-shmfs-1 是同一种回绕模式但位于不同函数，需要分别修复。x86 上只有在根回退到 RAMFS 时才可达；arm64 始终可达。ramfs.cpp:166 的 new_size = offset + size 回绕本次未追踪后续后果，只确认了该行存在。

#### V069 [critical·已确认·all·已修复] ramfs 节点没有引用计数，unlink 直接 kfree 仍被打开/正在使用的节点和数据（use-after-free）

- 位置：`src/fs/ramfs.cpp:616`；相关：`src/fs/ramfs.cpp:396`、`src/fs/ramfs.cpp:492`、`src/fs/ramfs.cpp:311`、`src/fs/ramfs.cpp:597`
- 证据：ramfs 节点创建时 `new_node->flags = 0;  // RAMFS 节点不应该被自动释放`（ramfs.cpp:396/492），所以 Vfs::ref_node/release_node 对它们是空操作，ref_count 永远为 0。ramfs_unlink 不检查任何引用：`kfree(file->data); kfree(file); ... ramfs_remove_entry(dir, name); kfree(target);`。而 fd 表项、ramfs_finddir 解锁后返回的裸指针（ramfs.cpp:311-319）都继续指向该节点。
- 触发场景：用户程序 `fd = open("/tmp/a", O_RDWR|O_CREAT); unlink("/tmp/a"); write(fd, buf, n); read(fd, ...)`：unlink 后 fs_node_t、ramfs_file_t 和数据缓冲区全部归还堆；后续 write 通过已释放的 node->ops 做虚调用、对已释放的 file->lock 加锁、向已释放/已被别人复用的 file->data memcpy，破坏内核堆；close 时还会再次经由悬空节点调用。并发场景同理：任务 A 在 path_to_node 里拿到节点指针后被抢占，任务 B unlink 该文件，A 继续使用悬空节点。对目录也一样（unlink 一个被另一进程 getdents 打开的空目录）。
- 修复方向：让 ramfs 节点参与引用计数：目录项持有 1 个引用，finddir/fd 各持有引用；unlink 只从目录摘除并标记 unlinked，在最后一个引用释放时（通过 NodeOps 的释放钩子）才释放 data/file/node。至少在 unlink 时拒绝 ref_count>0 的节点。
- 复核意见：ramfs 节点创建时 flags=0（src/fs/ramfs.cpp:395-396），而 fs::Vfs::ref_node/release_node（vfs.cpp:99-121）对没有 FS_NODE_FLAG_ALLOCATED 的节点直接返回，所以 ramfs_finddir（ramfs.cpp:309-319）里的 ref_node 是空操作。ramfs_unlink（ramfs.cpp:575-618）不检查任何引用就 kfree(file->data)、kfree(file)、kfree(target)；Vfs::unlink（vfs.cpp:547-563）也没有检查是否被打开。fd 表项仍持有该 fs_node_t 指针，open 后 unlink 再 read/write/close 即是对已释放节点、锁和数据的 use-after-free。 更正：arm64 上 ramfs 是根文件系统，始终可达；x86 上仅在根回退到 RAMFS 时可达。并发抢占场景（path_to_node 取得指针后被另一任务 unlink）未单独追踪，但单进程 open+unlink+write 的序列已足以触发。

#### V070 [critical·已确认·all·已修复] shmfs_unlink 直接 kfree 仍被打开的文件节点和数据（fd 悬空，UAF/双重释放物理页）

- 位置：`src/fs/shmfs.cpp:541`；相关：`src/fs/shmfs.cpp:472`、`src/fs/shmfs.cpp:525`、`src/fs/shmfs.cpp:382`、`src/fs/vfs.cpp:105`、`src/kernel/fd_table.cpp:105`
- 证据：shmfs 文件节点创建时 `new_node->ref_count = 0; new_node->flags = 0;`（shmfs.cpp:472-473），没有 FS_NODE_FLAG_ALLOCATED，因此 fs::Vfs::ref_node/release_node 对它是空操作（vfs.cpp:105/121），打开的 fd 不持有任何引用。shmfs_unlink 中唯一的保护是 `file->map_count > 0`，但 map_ref() 全仓库没有任何调用者，map_count 恒为 0；随后 `shmfs_free_pages(file); kfree(file); ... kfree(to_remove); kfree(target);`，且释放 file 时并未获取 file->lock。shmfs_finddir 在释放 dir->lock 之后才把 entry->node 返回（shmfs.cpp:382-390），同样无引用保护。
- 触发场景：进程 A：fd = open("/shm/x", O_CREAT|O_RDWR)，write 若干数据；任意进程（包括 A 自己，如 shell 的 rm /shm/x）调用 unlink("/shm/x")：fs_node_t、shmfs_file_t 和所有物理页被释放。A 的 fd_table 中 entry->node 仍指向已释放的堆块，之后 read/write/ftruncate/close 都会解引用已释放的 node->ops（虚函数调用）和 node->impl，并对已归还 PMM 的物理页（可能已分给别的进程或页表）读写——内核堆破坏/任意物理页覆盖。并发情形：B 在 shmfs_read 持有 file->lock 拷贝数据时 A unlink，file 与页被直接释放。
- 修复方向：给 shmfs 文件节点引入真实的引用计数（open 计数 + 映射计数）：unlink 只把目录项摘掉并置 unlinked 标记，等最后一个 fd 关闭/最后一个映射解除时（在 close 回调或 release_node 中）再释放页、file 和 node；finddir 要在 dir->lock 内增加引用；释放 file 前必须获取 file->lock。
- 复核意见：src/fs/shmfs.cpp:472-473 创建文件节点时 ref_count=0、flags=0，没有 FS_NODE_FLAG_ALLOCATED，所以 src/fs/vfs.cpp:105/121 的 ref_node/release_node 对它直接返回，打开的 fd 不持有引用。shmfs_unlink（shmfs.cpp:503-548）唯一的保护是 map_count>0，而 map_ref/map_unref（shmfs.cpp:591/609）在全仓库没有调用者，map_count 恒为 0，随后直接 shmfs_free_pages、kfree(file)、kfree(target)。用户态可经 SYS_UNLINK（src/kernel/syscall.cpp:552 -> Vfs::unlink vfs.cpp:560）到达，之后 fd_table 里残留的 node 指针被 read/write/close 解引用，构成 UAF。 更正：arch 应为 i686/x86_64 而非 all：arm64 的 Makefile 源列表（Makefile:120-166）不包含 fs/shmfs.cpp 和 kernel/fs_bootstrap.cpp，/shm 只在 x86 的 fs_bootstrap.cpp:158-172 挂载。Shmfs::get_phys_pages 也没有调用者，所以实际受影响的只有 fd 读写路径，没有 mmap 路径。

#### V071 [critical·已确认·all·已修复] 管道两端节点共享同一个 impl(pipe_t)，release_node 在任一端引用归零时 kfree(impl)，导致 UAF 与二次释放

- 位置：`src/fs/vfs.cpp:146`；相关：`src/kernel/fd_table.cpp:105`、`src/kernel/syscalls/fs.cpp:759`、`src/kernel/syscalls/fs.cpp:772`、`src/fs/pipe.cpp:131`、`src/fs/pipe.cpp:157`、`src/fs/pipe.cpp:394`
- 证据：`if (node->impl) { kfree(node->impl); } kfree(node);`（release_node，ref_count 归零时）。而 fs::Pipe::create 中 `rnode->impl = pipe;` 与 `wnode->impl = pipe;` 指向同一个 pipe_t。kernel::FdTable::free 先调用 `fs::Vfs::close(node)`（pipe_close 只在 readers==0 && writers==0 时才 `node->impl = NULL; kfree(pipe)`），随后 `fs::Vfs::release_node(node)`。只关闭一端时 pipe_close 不清 impl，release_node 却把仍被另一端使用的 pipe_t 释放。
- 触发场景：(1) 常见管道用法 `writer | reader`：写进程退出、所有写端 fd 关闭 -> wnode ref_count 归 0 -> kfree(pipe)；读进程仍在 pipe_read 中访问已释放的 pipe_t（lock/信号量/buffer），之后读端关闭时 pipe_close 在已释放内存上 `readers--` 并再次 `kfree(pipe)` -> 堆损坏。单进程 `pipe(fds); close(fds[0]); write(fds[1],...)` 同样触发。(2) syscall::Fs::pipe 的失败路径：fd 表只剩 0 或 1 个空位时，`release_node(read_node); release_node(write_node);`（fs.cpp:759-760）或 `FdTable::free(read_fd)` + `release_node(write_node)`（fs.cpp:771-772）对同一个 pipe_t kfree 两次，用户进程打开 511 个 fd 后调用 pipe() 即可触发。
- 修复方向：pipe_t 自带引用计数并由管道代码独占管理其生命周期：release_node 不应通用地 kfree(impl)（改为调用节点 ops 的 destroy/release 钩子），或者创建管道节点时不设置会被通用释放的 impl，改由 pipe_close/最终释放回调在两端都释放后才 kfree(pipe)。pipe() 失败路径也要走统一的销毁函数。
- 复核意见：src/fs/pipe.cpp:131/157 两个节点的 impl 指向同一个 pipe_t，且都带 FS_NODE_FLAG_ALLOCATED。pipe() 成功后每个节点 ref_count=1（create 置 1，FdTable::alloc 加 1，fs.cpp:765/777 减 1），fork/dup 时 ref_count 与 readers/writers 同步增长（fd_table.cpp:142-146）。读端最后一个 fd 关闭时，FdTable::free（fd_table.cpp:100-105）先调 pipe_close，此时 writers>0 不释放也不清 impl，接着 release_node 把 ref_count 减到 0，在 vfs.cpp:146-148 执行 kfree(node->impl)，释放了写端仍在使用的 pipe_t。写端关闭时 pipe_close 在已释放内存上操作并在 pipe.cpp:394 再次 kfree(pipe)。kfree（src/mm/heap.cpp:317-331）只校验 magic，不检测重复释放。fs.cpp:759-760 和 771-772 的失败路径同样对同一个 pipe_t 释放两次。pipe.cpp 在三个架构都编译（arm64 见 Makefile:166）。 更正：失败路径需要 fd 表恰好只剩 0 或 1 个空位才触发，属于边缘情况；主要问题是正常关闭一端的路径。
- 被 3 个独立审计者重复报告（kernel-fs-net-syscalls-2, fs-vfs-1, x-error-init-2）

#### V072 [high·已确认·i686,x86_64·已修复] 扩展文件失败（磁盘满/稀疏写）时已分配的簇不回滚且目录项不更新：用户可用 lseek+write 永久耗尽全部空闲簇

- **修复**：fat32_ensure_file_size rolls back on failure (restores the old chain end to EOC, frees the newly allocated clusters, restores start_cluster) and fails immediately when the request exceeds the volume's cluster count. write and truncate record a new first cluster in the directory entry straight away, so a later failure cannot orphan the chain. 验证方式：unit test test_fat32_extend_failure_rolls_back (2GB sparse write, empty-file and existing-chain extension with too few free clusters; free-cluster count checked directly in the FAT); boot regression
- 位置：`src/fs/fat32.cpp:779`；相关：`src/fs/fat32.cpp:1517`、`src/fs/fat32.cpp:978`、`src/fs/fat32.cpp:789`、`src/kernel/syscalls/fs.cpp:362`
- 证据：fat32_ensure_file_size: `while (current_clusters < required_clusters) { uint32_t new_cluster = fat32_allocate_cluster(fs); if (new_cluster == 0) { return -1; } ... else { file->start_cluster = new_cluster; } ...}`，失败时直接 return -1，不释放本次已分配的簇。调用者 fat32_file_write (1517-1519 行) `if (fat32_ensure_file_size(file, requested_end) != 0) { return 0; }` 直接返回，既不回滚也不调用 fat32_update_dirent_metadata，file->size 也不变。syscall::Fs::lseek 允许把偏移设到 0x7FFFFFFF 以内任意值 (src/kernel/syscalls/fs.cpp:362)。
- 触发场景：普通用户程序：open("/x", O_CREAT|O_WRONLY); lseek(fd, 0x7FFFFFF0, SEEK_SET); write(fd, "a", 1)。ensure_file_size 需要约 2GB 的簇，于是在持有 fs_lock 的情况下逐簇分配并清零直到磁盘上所有空闲簇被用光，然后返回 -1，write 返回 0。对空文件来说新链只记录在内存的 file->start_cluster 里，磁盘目录项的起始簇仍为 0；close 之后这些簇成为无人引用的丢失簇，unlink 也无法释放（目录项里簇号为 0），文件系统永久“满盘”，之后所有 create/mkdir/write 都失败。即使不是恶意场景，磁盘正常写满时最后一次 write 也会一字节都不写并留下已分配但 size 之外的簇。fat32_file_truncate 扩展路径 (978 行) 同样不回滚。
- 修复方向：ensure_file_size 记录扩展前的链尾，失败时调用 fat32_free_cluster_chain 释放新分配的部分并把原链尾恢复为 EOC、恢复 file->start_cluster；分配前先根据空闲簇数做容量检查；空间不足时支持部分写入；稀疏扩展应设置上限或延迟分配。
- 复核意见：src/fs/fat32.cpp:777-793 的循环在 fat32_allocate_cluster 返回 0 时直接 return -1，不释放本次已分配的簇（每个新簇在 fat32.cpp:322 已写为 EOC 并链接到前一簇）；fat32_file_write 在 1516-1519 行失败后直接 return 0，跳过 1592 行的 fat32_update_dirent_metadata。空文件的新链首只存在于内存的 file->start_cluster（788 行）中，磁盘目录项起始簇仍为 0。syscalls/fs.cpp:360-381 的 lseek 只拒绝负偏移，0x7FFFFFF0 可以通过，因此普通用户可触发。 更正：簇“永久丢失”的前提是失败后直接 close/退出：若同一 fd 之后又成功写入一次（例如 lseek 回 0 再 write），1592 行会把 start_cluster 写入目录项，之后 unlink 能回收整条链。对已有簇的文件，多出来的簇仍挂在链上，unlink 可回收，仅是 size 之外的浪费。fat32_file_truncate 扩展路径（约 976-979 行）同样不回滚。另外整个分配过程持有 fs_lock 并逐簇清零，期间其它 FAT32 操作被阻塞。

#### V073 [high·已确认·i686,x86_64·已修复] unlink 不检查文件是否仍被打开：释放簇链后旧节点继续读写已释放的簇，并会改写已被复用的目录项槽位

- **修复**：unlink of a file that is in use only deletes the directory entry, marks the in-core file unlinked and sets FS_NODE_FLAG_UNLINKED; the cluster chain is freed when the last reference is released. An unlinked file never writes its old directory slot again, and creating entries in an unlinked directory is refused. 验证方式：unit test test_fat32_unlink_while_open (open, unlink, new file reuses the slot, write to both, last close frees clusters); boot regression
- 位置：`src/fs/fat32.cpp:1293`；相关：`src/fs/fat32.cpp:1297`、`src/fs/fat32.cpp:880`、`src/fs/vfs.cpp:560`
- 证据：fat32_dir_remove_entry: `if (start_cluster >= 2) { fat32_free_cluster_chain(fs, start_cluster); } if (fat32_mark_entry_deleted(fs, lookup->cluster, lookup->offset) != 0) ...`。没有任何打开计数检查，fs::Vfs::unlink (src/fs/vfs.cpp:560) 也不检查。已打开的 fat32_file_t 仍保存 start_cluster、dirent_cluster、dirent_offset；随后 fat32_file_write 末尾无条件调用 fat32_update_dirent_metadata(file)，它直接对 (dirent_cluster,dirent_offset) 处的 32 字节槽位写 cluster_low/high、file_size 并 `attributes |= ARCHIVE`，不验证该槽位是否还是自己的条目。
- 触发场景：进程 A 打开 /a.txt 并保持 fd；进程 B unlink("/a.txt")，簇链被释放、槽位标记 0xE5；B 再 create "/b.txt"，fat32_find_free_dir_entry 取到第一个 0xE5 槽位（就是刚才的槽位），并给 b.txt 分配了刚被释放的簇。此时 A 继续 write：数据写进现在属于 b.txt 的簇，且 fat32_update_dirent_metadata 把 b.txt 的目录项的起始簇和大小改成 a.txt 的旧值，b.txt 内容被破坏、其真实簇链丢失。另外先释放簇链再标记删除：若 fat32_mark_entry_deleted 失败，目录项仍然存在但指向已释放的簇。
- 修复方向：引入 in-core inode 后在 unlink 时检查打开计数：仍被打开则只标记“待删除”，在最后一次 release 时再释放簇链；unlink 时先写目录项（0xE5）再释放簇链；update_dirent_metadata 写回前校验槽位仍是本文件（或节点被标记为已删除则跳过）。
- 复核意见：src/fs/fat32.cpp:1293-1300 的 fat32_dir_remove_entry 直接释放簇链并标记 0xE5，没有任何打开计数检查；src/fs/vfs.cpp:560 附近的 Vfs::unlink 也只是转调 parent->ops->unlink。已打开的 fat32_file_t（fat32.cpp:81-90）私有保存 start_cluster/dirent_cluster/dirent_offset，fat32_file_write 末尾（fat32.cpp:1591）无条件调用 fat32_update_dirent_metadata，而后者（858-893 行）不校验槽位归属就改写簇号和大小。因此 open→unlink→另建文件→继续 write 会写入已被复用的簇并改写别的文件的目录项，用户态可达。fat32 仅在 i686/x86_64 编译（Makefile:172 wildcard fs/*.cpp，arm64 列表不含 fat32）。 更正：场景不需要两个进程，单进程 open 后 unlink 再 write（常见的临时文件用法）即可触发。末尾“先释放簇链再标记删除”的顺序问题属于 gap8-fat32-write-side-1 的根因。

#### V074 [high·已确认·all·已修复] ramfs 不实现 truncate，Vfs::truncate 回退分支只改 node->size，O_TRUNC/ftruncate 在 ramfs 上不生效且使两个 size 失配

- **修复**：Added ramfs_truncate (shrink, or grow with zero fill) and OP_TRUNCATE on ramfs files; ramfs_write zero-fills the gap when writing past the end; fs::Vfs::truncate returns -1 for filesystems without truncate instead of only changing node->size. 验证方式：unit test test_ramfs_truncate, passes on i686, x86_64 and arm64
- 位置：`src/fs/vfs.cpp:583`；相关：`src/fs/ramfs.cpp:324`、`src/fs/ramfs.cpp:198`、`src/kernel/syscalls/fs.cpp:173`、`src/kernel/syscalls/fs.cpp:673`
- 证据：RamfsFileOps::supported() 只有 `OP_READ | OP_WRITE | OP_OPEN | OP_CLOSE`（ramfs.cpp:324），所以 Vfs::truncate 走回退：`node->size = new_size; return 0;`。但 ramfs 读写用的是 `ramfs_file_t::size`（ramfs.cpp:133、198），从不看 node->size；ramfs_write 只在 `new_size > file->size` 时才同步 node->size。
- 触发场景：用户执行 `open("/tmp/f", O_WRONLY|O_TRUNC)` 后写入比原内容短的数据（shell 的 `echo x > f` 类重定向）：node->size 被置 0，但 file->size 和旧数据保留；写入 2 字节后 file->size 仍是旧长度，read 返回 新数据+旧文件尾部，而 stat 报告 size=0（node->size 未被更新，因为 new_size <= file->size）。反向：ftruncate(fd, 1000000) 把 node->size 设成 1MB 而 file->size 不变，之后 O_APPEND 写用 node->size 作偏移（syscalls/fs.cpp:296），在 1MB 处写入并把中间未初始化的堆内存变成文件内容。
- 修复方向：为 ramfs 实现 truncate（缩小时更新 file->size/node->size 并可选释放容量；扩大时分配并清零），在 RamfsFileOps 声明 OP_TRUNCATE；Vfs::truncate 对不支持的文件系统应返回 -1 而不是篡改 node->size。
- 复核意见：src/fs/ramfs.cpp:324 的 RamfsFileOps::supported() 确实只有 OP_READ|OP_WRITE|OP_OPEN|OP_CLOSE，因此 src/fs/vfs.cpp:578-584 走回退分支仅执行 node->size = new_size。ramfs_read/ramfs_write（ramfs.cpp:133、195-200）只使用 ramfs_file_t::size，且只有 new_size > file->size 时才回写 node->size，所以 O_TRUNC（syscalls/fs.cpp:171-173）后短写会读到 新数据+旧尾部，而 stat（fs.cpp:32）报告 0。根文件系统就是 ramfs（fs_bootstrap.cpp:100-108），ramfs.cpp 在三个架构的源列表里都有，路径可达。 更正：node->size 失配还会影响 lseek(SEEK_END)（syscalls/fs.cpp:368）、open 时 O_APPEND 的初始偏移（fs.cpp:200）以及 execve 用 file->size 分配缓冲区（syscalls/process.cpp:375），修复时应一并让这些路径使用文件系统的真实大小。
- 被 2 个独立审计者重复报告（fs-vfs-4, x-user-boundary-5）

#### V075 [medium·已确认·i686,x86_64·已修复] unlink / truncate 先释放簇链、后改目录项，且中间任何失败都不回滚：目录项会指向已释放（随后被复用）的簇

- **修复**：unlink marks the directory entry deleted and checks the result before freeing the chain. truncate writes the directory entry first (restoring in-memory state and returning -1 if that fails), then frees. fat32_truncate_cluster_chain writes EOC on the last kept cluster (checked) before freeing the tail. 验证方式：unit test test_fat32_truncate_real covers the reordered shrink/zero/extend paths; the I/O-failure branches themselves are untested (no fault injection)
- 位置：`src/fs/fat32.cpp:1293`；相关：`src/fs/fat32.cpp:990`、`src/fs/fat32.cpp:1010`、`src/fs/fat32.cpp:928`、`src/fs/fat32.cpp:939`
- 证据：fat32_dir_remove_entry:
```
if (start_cluster >= 2) { fat32_free_cluster_chain(fs, start_cluster); }
if (fat32_mark_entry_deleted(fs, lookup->cluster, lookup->offset) != 0) { kfree(lookup); return -1; }
```
先把整条簇链在 FAT 中标记为空闲，再去把目录项标成 0xE5。fat32_mark_entry_deleted 需要 kmalloc(bytes_per_cluster) + 读簇 + 写簇，任何一步失败（内核堆不足、ATA 超时）函数返回 -1，但 FAT 已经被改掉，目录项仍然带着原来的 start_cluster 和 file_size。fat32_file_truncate(new_size==0) 同样：990 行先 fat32_free_cluster_chain，1010 行才 fat32_update_dirent_metadata(file) 且返回值被丢弃。fat32_truncate_cluster_chain（928/939 行）先逐个释放尾部簇，最后才把 last_kept_cluster 写成 EOF。
- 触发场景：用户执行 rm /BIG.BIN（或 open(O_TRUNC)）：簇链已释放后 fat32_mark_entry_deleted / fat32_update_dirent_metadata 里的 kmalloc 失败或 ATA 写失败（或此时 QEMU 被关闭——测试跑完即退出是常态）。磁盘上目录项仍指向旧簇链且 size 不变；之后任何新文件分配到这些簇，两个文件交叉链接：读旧文件得到新文件内容，再删旧文件会把新文件的簇释放掉，启动盘镜像被逐步破坏。截断到非 0 时在写 EOF 之前中断，则保留部分的最后一簇仍链接到已释放的簇。
- 修复方向：调整顺序使每一步中断后磁盘状态仍自洽：unlink 先把目录项标记为 0xE5 并确认写成功，再释放簇链（最坏只是丢簇，可由 fsck 回收）；truncate 到 0 先把目录项的 cluster/size 写成 0 再释放；收缩时先把 last_kept 写成 EOF 再释放尾部。所有步骤检查返回值并向上返回错误。
- 复核意见：顺序确如所述：fat32.cpp:1293 先 fat32_free_cluster_chain，1297 才 fat32_mark_entry_deleted，失败时直接返回 -1 不回滚；fat32_file_truncate 在 990-992 行先释放整条链，1010 行才调用 fat32_update_dirent_metadata 且丢弃返回值；fat32_truncate_cluster_chain（925-941 行）先逐簇释放、最后才写 EOF，且 fat32_write_fat_entry 返回值未检查。代码缺陷属实，但触发条件是中途 kmalloc 失败、ATA 写失败或断电，属于错误路径/崩溃一致性问题，故降为 medium。 更正：与 fs-fat32-2 位置相同但根因不同（操作顺序与错误处理，而非缺少打开计数），不算重复。“QEMU 被关闭”要恰好落在两次同步写之间，窗口很窄；实际更可能的触发是 I/O 或内存分配失败。

#### V076 [medium·已确认·i686,x86_64·已修复] BPB 校验不足：bytes_per_sector 只检查是 2 的幂，不要求等于块设备扇区大小，小于 512 时所有扇区/簇缓冲区堆溢出

- **修复**：Added fat32_validate_bpb, called from Fat32::init: bytes_per_sector must equal the device block size, sectors_per_cluster a power of two, fat_count/reserved/sectors_per_fat non-zero, filesystem no larger than the device, FAT large enough for all clusters, root cluster in range, all in 64-bit arithmetic. probe rejects devices whose block size is not 512 (it reads into a 512-byte struct). FSInfo is only used when its sector is in the reserved area and its hint is a valid cluster. 验证方式：unit test test_fat32_rejects_bad_bpb (10 corrupted-field cases plus a valid mount); the real boot disk still mounts on i686 and x86_64. The 'FAT too small for the cluster count' check has no test case (not constructible on the 72-sector test volume).
- 位置：`src/fs/fat32.cpp:1812`；相关：`src/fs/fat32.cpp:163`、`src/fs/fat32.cpp:200`、`src/fs/fat32.cpp:1850`、`src/fs/fat32.cpp:1854`、`src/fs/fat32.cpp:1885`
- 证据：probe: `if (bpb.bytes_per_sector == 0 || (bpb.bytes_per_sector & (bpb.bytes_per_sector - 1)) != 0) return false;`（注释说“必须是 512 的倍数”但实际只查 2 的幂）。之后所有缓冲区都按 BPB 值分配：`kmalloc(fs->bpb.bytes_per_sector)` (163, 200, 1885 行)、`kmalloc(fs->bytes_per_cluster)`，但 fs::Blockdev::read(dev, sector, 1/ sectors_per_cluster, buf) 按设备真实扇区大小（ATA 固定 512 字节/扇区，见 ata.cpp:163）写入。init 中也不校验 fat_count!=0、sectors_per_fat_32 是否足以容纳 total_clusters+2 个表项、root_cluster 是否在 [2,total_clusters+1]、fat_count*sectors_per_fat_32 的 32 位溢出、total_sectors 是否不超过设备大小。
- 触发场景：挂载一个 BPB 中 bytes_per_sector=256（或 1/2/…/256）、其余字段合法的磁盘镜像：Fat32::init 读 FSInfo 时 kmalloc(256) 后 ATA 写入 512 字节，内核堆越界 256 字节；之后每次 FAT 读、簇读都越界 (spc*512 字节写入 spc*256 字节的缓冲区)，堆元数据被破坏，内核崩溃或被控制。fat_count=0 时 fat32_write_fat_entry 的循环一次不执行却返回 0，分配器反复“成功分配”同一个簇。sectors_per_fat_32 过小时 FAT 扫描会把数据区当 FAT 表项读写。bytes_per_sector=4096 时所有 LBA 计算都错位。
- 修复方向：在 probe/init 中严格校验：bytes_per_sector == Blockdev::get_block_size(dev)（或至少为其整数倍并做换算）、sectors_per_cluster 为 2 的幂且非 0、fat_count ∈ {1,2}、reserved_sectors≥1、sectors_per_fat_32*bytes_per_sector/4 ≥ total_clusters+2、root_cluster ∈ [2,total_clusters+1]、total_sectors ≤ 设备扇区数、所有乘加用 64 位计算并检查溢出；任一不满足则拒绝挂载。
- 复核意见：probe 在 fat32.cpp:1812 只检查 bytes_per_sector 非零且为 2 的幂；init（1820-1900 行）仅校验 sectors_per_cluster!=0、total_sectors 不小于保留区加 FAT 区、total_clusters!=0，未校验 fat_count、root_cluster 范围、FAT 大小与设备块大小是否一致。缓冲区按 BPB 值分配（163、200、1885 行），而 ATA 块设备固定 512 字节/扇区（src/drivers/x86/ata.cpp:44、283），bytes_per_sector<512 时确实堆越界。但 Fat32::init 只在启动时由 src/kernel/fs_bootstrap.cpp:47/65 对启动盘调用，没有用户态 mount 入口，需要畸形的启动盘镜像才能触发，故降为 medium。 更正：严重度应为 medium：攻击面是启动时挂载的磁盘镜像而非用户态可达路径。fat_count=0 时 fat32_write_fat_entry 的循环不执行（fat32.cpp:207 起）这一点属实，但后续返回值细节未逐行核对。

#### V077 [medium·已确认·all·已修复] /proc 根目录枚举把 PID 当作任务槽下标：PID >= 256 的进程永远不会被列出

- **修复**：procfs_root_readdir walks task_pool slots instead of calling get_by_pid(0..MAX_TASKS-1), so processes with PID >= 256 are listed. 验证方式：boot regression only
- 位置：`src/fs/procfs.cpp:853`；相关：`src/kernel/task.cpp:167`、`src/kernel/task.cpp:229`、`user/shell/shell.cpp:1186`
- 证据：`for (uint32_t i = 0; i < MAX_TASKS; i++) { task_t *task = kernel::Scheduler::get_by_pid(i); ...`。get_by_pid 按 pid 值查找，而 pid 由 `task_pool[i].pid = next_pid++`（task.cpp:167，next_pid 单调递增、槽位复用时不回收 pid）分配，因此循环只能发现 pid 在 [0,255] 的任务。
- 触发场景：系统启动后累计创建过 255 个以上进程（例如在 shell 里反复运行外部命令，每次 fork/exec 消耗一个 pid）。此后新进程 pid >= 256，procfs_root_readdir 找不到它们，用户态 `ps`（user/shell/shell.cpp:1186 通过 getdents 枚举 /proc）不再显示任何新进程，尽管 /proc/<pid>/status 直接打开仍然可用。另外每个目录项都要做 256 次 get_by_pid（每次关中断加自旋锁扫描 256 槽），一次 ps 是 O(N*256*256)。
- 修复方向：在 Scheduler 中提供按槽位枚举的接口（如在 task_lock 下遍历 task_pool[i]，返回第 n 个 state!=TASK_UNUSED 的任务的 pid 快照），procfs_root_readdir 改为按槽位而不是按 pid 值遍历。
- 复核意见：src/fs/procfs.cpp:852-853 用 `for (i = 0; i < MAX_TASKS; i++) get_by_pid(i)`，MAX_TASKS=256（src/include/kernel/task.h:20）；get_by_pid（task.cpp:229-239）按 pid 值匹配，而 pid 来自单调递增的 next_pid++（task.cpp:51,167），槽位复用不回收 pid。所以 pid>=256 的任务不会被 /proc 根目录枚举，累计创建 255 个进程后 ps 就看不到新进程。 更正：仅影响 i686/x86_64（arm64 不编译 procfs）。后果是 ps 列表不全和 readdir 性能差，不涉及内存安全，降为 medium。


### 网络协议栈（28 项）

#### V078 [critical·已确认·i686,x86_64·已修复] DNS/DHCP 发送后无条件 Netbuf::free，而 Ip::output 可能已把 buf 挂入 ARP 等待队列：释放后使用/二次释放

- **修复**：Sending now only borrows the Netbuf (Ip::output / Ethernet::output / Netdev::transmit never keep or free it; every caller frees after the call). Arp::queue_packet stores its own clone, so the unconditional free in dns.cpp/dhcp.cpp no longer leaves a dangling pointer on the ARP pending queue. 验证方式：unit test test_arp_pending_packet_survives_caller_free (src/tests/net/netstack_test.cpp)
- 位置：`src/net/dns.cpp:407`；相关：`src/net/dhcp.cpp:223`、`src/net/dhcp.cpp:307`、`src/net/dhcp.cpp:359`、`src/net/ip.cpp:587`、`src/net/arp.cpp:463`、`src/net/arp.cpp:68`
- 证据：dns_do_query: `int ret = net::Udp::sendto(pcb, buf, server_ip, DNS_PORT); net::Netbuf::free(buf);`（不看 ret 就释放）。但 src/net/ip.cpp:580-588 中，当 Arp::resolve 返回 -1（正在解析）时 `if (net::Arp::queue_packet(next_hop, buf) == 0) return 0;`，queue_packet（arp.cpp:463）直接把同一个 buf 指针挂到 `entry->pending_queue`，没有 clone。之后 arp.cpp:319 的 arp_send_pending 会对该 buf 调用 Ethernet::output（Netbuf::push 并向 buf->data 写 14 字节以太网头，再 memcpy 到网卡），ARP 超时路径 arp_free_pending（arp.cpp:356/379/390/197）会再次 Netbuf::free。e1000 的 transmit 只拷贝不释放，所以直接发送成功时 free 是对的，唯独排队路径所有权已转移。
- 触发场景：启动后静态配置 IP，ARP 缓存中还没有 DNS 服务器（或网关）的条目，在内核 shell 执行 `nslookup example.com`：Ip::output 把查询包挂入 ARP pending 队列并返回 0，dns_do_query 立刻 kfree(buf->head)+kfree(buf)。随后轮询循环里收到的应答 Netbuf、PCB 等会复用这两块堆内存；ARP 应答到达时 arp_send_pending 向已释放/已被复用的内存写以太网头并把垃圾数据发出去；若 ARP 一直无应答，超时清理时 arp_free_pending 对同一指针二次 kfree，堆元数据损坏。首次 DNS 查询几乎必然走这条路径。
- 修复方向：统一 buf 所有权约定：Ip::output 返回 0 表示所有权已转移（排队或已发送），调用者只在 ret<0 时释放；同时让直接发送成功路径由协议栈在 Ethernet::output/Netdev::transmit 之后释放（或让 Arp::queue_packet 存放 Netbuf::clone 的副本，调用者始终释放原件）。dns.cpp:407 以及 dhcp.cpp:223/307/359 都要按新约定修改。
- 复核意见：src/net/dns.cpp:406-407 在 Udp::sendto 之后无条件 Netbuf::free(buf)；而 Ip::output(ip.cpp:585-588) 在 Arp::resolve 返回 -1 时通过 Arp::queue_packet(arp.cpp:472-484) 把同一指针挂入 pending_queue 并返回 0，没有 clone。ARP 应答到达时 cache_update -> arp_send_pending(arp.cpp:68-85) 对已释放的 buf 调 Ethernet::output（push 并写以太网头）；resolve 的 LRU 替换(arp.cpp:197)、cache_delete/cache_clear 还会再次 free。栈内其他调用者（icmp.cpp:170-172、tcp.cpp:450-452、socket.cpp:392-394）都只在 ret<0 时释放，证明 DNS 的写法与排队路径冲突；首次 nslookup 时 DNS 服务器/网关的 ARP 尚未解析，几乎必走此路径，属于正常操作下的堆释放后写。 更正：auditor 所说的“ARP 超时路径二次释放”目前不会经 cache_cleanup 触发（该函数无调用者，见 net-link-ip-7），二次释放只会经 arp.cpp:197 的 LRU 替换或 cache_delete/cache_clear 发生；主要后果是 ARP 应答到达时的释放后使用/写。DHCP 的三处同样写法(dhcp.cpp:223/307/359)中，DISCOVER/广播 REQUEST 因 ip_addr==0 在入队前就失败，实际只有 RENEWING 单播和 RELEASE 可能命中，且因 DHCP 永远到不了 BOUND 而目前不可达。实际只影响 i686。

#### V079 [critical·已确认·all·已修复] IP 分片重组：offset+len 使用 uint16_t 计算发生回绕（Ping of Death），导致内核堆溢出

- 位置：`src/net/ip.cpp:120`；相关：`src/net/ip.cpp:165`、`src/net/ip.cpp:170`、`src/net/ip.cpp:183`、`src/net/ip.cpp:202`
- 证据：`r->total_len = offset + len;`（total_len、offset、len 均为 uint16_t），offset 最大 8191*8=65528，再加上最多 1480 的 len 会超过 65535 回绕。ip_reass_complete 中 `uint16_t expected_offset` 以同样方式累加并回绕，于是 `expected_offset != r->total_len` 的完整性检查照样通过；随后 `Netbuf::alloc(r->total_len)` 只按回绕后的小长度分配，而 `memcpy(dest + f->offset, f->data, f->len)` 仍按真实偏移（最高约 65528）写入。全程没有 offset+len <= 65535 的校验。
- 触发场景：攻击者发送一组连续分片覆盖 0..65528，最后一片 offset=65528、len=1000（MF=0）：total_len 回绕成 992，expected_offset 同样回绕成 992，检查通过；alloc(992) 得到约 1120 字节的堆块，循环把前面约 64KB 的分片数据依次拷到 dest+offset，越过缓冲区覆盖后面约 63KB 的内核堆（堆块头 magic、其他对象），造成堆破坏/任意内核内存改写。
- 修复方向：在 ip_reassemble 中校验 `(uint32_t)offset + data_len <= 65535`（且不超过可支持的最大重组长度），否则丢弃并释放整个重组条目；长度累加使用 uint32_t；同时校验非末尾分片长度为 8 的倍数、末片确定的 total_len 与已有分片不矛盾。
- 复核意见：src/net/ip.cpp:116-120 中 offset、len、total_len 均为 uint16_t，`r->total_len = offset + len` 无上界校验；ip.cpp:165-173 的 expected_offset 同为 uint16_t，按相同方式回绕，因此完整性检查照样通过。随后 ip.cpp:178-183 按回绕后的小长度 alloc/put 成功（小于 1920 时 put 不返回 NULL），memcpy(dest + f->offset, ...) 却按真实偏移（最高约 65K）写入，越过 netbuf.cpp:11-21 分配的至多 2048 字节堆块。ip_reassemble（ip.cpp:200-228）和 Ip::input（ip.cpp:466）都没有 offset+len<=65535 的检查。 更正：实际只在 i686 上可达（x86_64 的 e1000 启动时被跳过，arm64 未编入网络栈）。触发需要能向网卡注入原始分片的同网段对端（tap/桥接），slirp 用户态网络自身不会产生这种分片。修复时应一并限制重组总长不超过 Netbuf 容量（见 net-link-ip-2）。

#### V080 [critical·已确认·all·已修复] IP 分片重组：重组后总长超过 1920 字节时 Netbuf::put 返回 NULL，memcpy 写向 NULL+offset（远程可触发内核崩溃）

- 位置：`src/net/ip.cpp:181`；相关：`src/net/netbuf.cpp:12`
- 证据：`net::Netbuf *buf = net::Netbuf::alloc(r->total_len); ... uint8_t *dest = net::Netbuf::put(buf, r->total_len); for (...) memcpy(dest + f->offset, f->data, f->len);` Netbuf::alloc 把缓冲区上限截到 NETBUF_MAX_SIZE(2048，含 128 headroom)，total_len > 1920 时 put 返回 NULL，代码未检查就以 NULL 为基址逐片 memcpy。而分片重组的意义恰恰是处理大于 MTU 的报文，total_len 最大可到 65535。
- 触发场景：局域网任意主机执行 `ping -s 3000 <castoros-ip>`（或 slirp 把一个大于 1500 字节的 UDP 应答分片后送入）：两个分片到齐后 ip_reass_complete 以 dest=NULL 执行 memcpy(0+0, ..., 1480)，在 e1000 中断上下文里向地址 0 写入 -> 内核缺页异常/panic，或改写当前进程低地址内存。无需任何认证，单个 ping 即可打死内核。
- 修复方向：重组前检查 total_len 是否超出 netbuf 容量（或让 netbuf 支持大缓冲区/按需分配 total_len+headroom）；检查 alloc/put 返回值，失败时释放重组条目并丢弃。
- 复核意见：src/net/netbuf.cpp:11-14 把 total_size 截到 NETBUF_MAX_SIZE=2048（netbuf.h:22-23，headroom 128），netbuf.cpp:72 的 put 在 tailroom(1920) < len 时返回 NULL。src/net/ip.cpp:181-183 不检查 dest 就执行 memcpy(dest + f->offset, ...)，total_len > 1920 时即向 NULL+offset 写入。两个合法分片（如 3000 字节的 ICMP/UDP 报文）到齐即可触发，ip.cpp:466 的长度检查只约束单个分片。 更正：实际只影响 i686（x86_64 e1000 被跳过，arm64 无网络栈）。slirp 模式下宿主机无法直接 ping 客户机，但 slirp 会把超过 MTU 的 UDP 应答分片后送入，tap 模式下 `ping -s 3000` 可直接触发。写地址 0 的后果是内核态缺页并 panic，还是改写当前进程低地址映射，取决于当时的页表，未动态验证。

#### V081 [critical·已确认·all·已修复] Netbuf::alloc 静默截断/整数回绕，调用者不检查 put() 返回值：用户态 sendto 大于 1920 字节即可让内核向 NULL 写入或堆溢出

- 位置：`src/net/netbuf.cpp:11`；相关：`src/net/netbuf.cpp:54`、`src/net/netbuf.cpp:72`、`src/net/socket.cpp:339`、`src/net/socket.cpp:384`、`src/net/udp.cpp:195`、`src/net/icmp.cpp:145`
- 证据：`uint32_t total_size = NETBUF_HEADROOM + size; if (total_size > NETBUF_MAX_SIZE) total_size = NETBUF_MAX_SIZE;` 请求超过 1920 字节时不报错而是返回一个更小的缓冲区；size 接近 2^32 时 `128 + size` 还会回绕成很小的值（例如 size=0xFFFFFFF0 -> total_size=0x70，data=head+128 已越过 end）。put() 的检查 `buf->end - buf->tail < (int)len` 把 len 转成 int，len>=0x80000000 时变负数从而通过检查。所有调用者都不检查 put() 的返回值：src/net/socket.cpp:389 `uint8_t *data = net::Netbuf::put(nbuf, len); memcpy(data, buf, len);`，而 src/kernel/syscall.cpp:387/396 把用户传入的 len 原样传下来，没有任何上限检查。
- 触发场景：用户程序创建 UDP socket 后调用 sendto(fd, buf, 4000, ...)：Netbuf::alloc(4000) 返回只有 1920 字节 tailroom 的缓冲区，put(nbuf,4000) 返回 NULL，随后 memcpy(NULL, buf, 4000) 在内核态向地址 0 写入用户可控数据 -> 内核缺页 panic（若该进程在 0 地址附近有映射则被静默改写）。传 len=0xFFFFFFF0 时 alloc 只分配 112 字节，put 因有符号比较通过，memcpy 以 head+128 为起点做近 4GB 的拷贝，直接破坏内核堆。同样的模式存在于 UDP/TCP/ICMP/DHCP 的所有发送路径以及 e1000 接收路径。
- 修复方向：alloc 中对 size 做上限检查（size > NETBUF_MAX_SIZE - NETBUF_HEADROOM 时直接返回 NULL，不要截断）；push/put 用无符号比较（`(size_t)(buf->end - buf->tail) < len`）；所有调用者检查 put/push 返回值；socket 层对 UDP 负载长度按 MTU/65507 做上限并返回 EMSGSIZE。
- 复核意见：netbuf.cpp:11-14 确实把 total_size 截断到 2048 且 128+size 可 32 位回绕；netbuf.cpp:72 的 put 用 (int)len 做有符号比较。socket.cpp 的 send/sendto 中 alloc(len) 后直接 `data = put(nbuf,len); memcpy(data, buf, len)`，不检查 NULL，syscall.cpp:391-398 把用户 len 原样传入且无上限、无用户指针校验。len=2000 时 put 返回 NULL 导致内核向地址 0 memcpy；len=0xFFFFFFF0 时 kmalloc(112)、end-tail=-16 不小于 (int)len=-16，检查通过后越界写堆。该路径在 Udp::sendto 查找网卡之前执行，所以 x86_64（无 e1000）同样可达。 更正：arch 应为 i686 和 x86_64：syscall.cpp:578 的 #if !defined(ARCH_ARM64) 使 arm64 不注册 socket 系统调用，且 arm64 不编译 net 目录。e1000 接收路径(e1000.cpp:480)的包长受硬件帧长限制，一般不会超过 1920，不属于同等严重的触发点。
- 被 2 个独立审计者重复报告（net-link-ip-1, tests-kernel-fs-net-1）

#### V082 [critical·已确认·i686,x86_64·已修复] UDP send/sendto 不限制 len，Netbuf::put 失败返回 NULL 后直接 memcpy，用户可触发内核空指针写/堆溢出

- **修复**：The NULL-put check was already in place (0db9387). Remaining hole fixed: Socket::send/sendto now check the full size_t length up front (-EMSGSIZE above 1472 bytes) because alloc/put take 32 bits while memcpy used the full length on x86_64. TCP send/recv clamp the length. 验证方式：unit test test_socket_udp_send_rejects_oversize (includes a 0x100000010 length on x86_64)
- 位置：`src/net/socket.cpp:344`；相关：`src/net/socket.cpp:389`、`src/net/socket.cpp:390`、`src/net/netbuf.cpp:72`
- 证据：`net::Netbuf *nbuf = net::Netbuf::alloc(len); ... uint8_t *data = net::Netbuf::put(nbuf, len); memcpy(data, buf, len);` 没有检查 data。Netbuf::alloc 把 total_size 截断为 NETBUF_MAX_SIZE(2048)，`put` 在 `buf->end - buf->tail < (int)len` 时返回 NULL；并且 `(int)len` 在 len >= 0x80000000 时为负，检查被绕过，put“成功”并返回 1920 字节缓冲区的指针。
- 触发场景：用户程序 `sendto(udp_fd, buf, 4000, 0, &addr, sizeof(addr))`：len > 2048-128，put 返回 NULL，内核执行 memcpy(NULL, buf, 4000)，在内核态向地址 0 写入 -> 缺页 panic（若进程在 0 地址映射了页面则是静默写）。传入 len=0x80000000 时 put 不返回 NULL，memcpy 向 1920 字节的堆缓冲区复制 2GB -> 内核堆被彻底破坏。connect 之后的 send() 走同样代码。
- 修复方向：在 send/sendto 中先校验 len（UDP 上限 = MTU/最大净荷，超出返回 -EMSGSIZE），检查 put 的返回值；Netbuf::put 中改用无符号比较 `(size_t)(end - tail) < len`。
- 复核意见：socket.cpp:338-345(send) 与 384-390(sendto) 中 Netbuf::alloc(len) 后直接 put+memcpy，未检查 put 返回值，也未限制 len。netbuf.cpp:11-14 把 total_size 截到 2048，netbuf.cpp:72 的 put 在 tailroom(最多 1920) < (int)len 时返回 NULL，于是 len>1920 时执行 memcpy(NULL, buf, len)；len>=0x80000000 时 (int)len 为负，检查通过，向 1920 字节堆缓冲区拷贝超长数据。len 来自 sys_send/sendto_wrapper(syscall.cpp:384-400) 的用户寄存器，无任何校验，用户态一次系统调用即可触发。 更正：x86_64 上 size_t len 传给 alloc/put 时会截断为 uint32_t，但 memcpy 仍用完整的 size_t len，所以 len 低 32 位较小、高位非零时同样溢出。UDP socket 无需网卡即可创建，x86_64 也可达。
- 被 3 个独立审计者重复报告（kernel-fs-net-syscalls-4, net-transport-3, x-user-boundary-4）

#### V083 [critical·已确认·i686,x86_64·已修复] socket 系统调用未对任何用户指针/长度做校验，内核直接读写用户给出的任意地址

- 位置：`src/net/socket.cpp:437`；相关：`src/kernel/syscall.cpp:356`、`src/net/socket.cpp:279`、`src/net/socket.cpp:472`、`src/net/socket.cpp:568`、`src/net/socket.cpp:752`、`src/net/tcp.cpp:1096`
- 证据：src/kernel/syscall.cpp 349-474 行的包装器把用户寄存器值直接强转为指针传入 net::Socket::*；socket.cpp 中 recv `memcpy(buf, nbuf->data, copy_len);`、recvfrom 写 `src_addr`/`*addrlen`、accept 写 `addr`、getsockopt `*(int *)optval = ...`、select `memcpy(readfds, &read_result, sizeof(fd_set))`、Tcp::read `memcpy(buf, pcb->recv_buf + ..., copy_len)`，以及 send/Tcp::write 从 `buf` 读取，均没有任何地址范围或映射检查。
- 触发场景：用户进程建立 UDP socket，自己向自己发一个内容可控的数据报，然后 `recv(s, (void*)内核地址, n, 0)`：内核把受控数据写入任意内核地址（提权）。反向：`send(s, (void*)内核地址, n, 0)` 把内核内存内容发到网络（信息泄漏）。传入未映射地址则内核缺页崩溃。
- 修复方向：在系统调用包装层增加统一的用户缓冲区校验/拷贝（范围必须落在用户空间且已映射，copy_from_user/copy_to_user），sockaddr/optval/fd_set/timeval 先拷到内核栈再使用。
- 复核意见：syscall.cpp:345-475 的全部 socket 包装器把寄存器值直接强转为指针传入 net::Socket::*，全仓库 grep 不到 copy_from_user/copy_to_user/access_ok 之类的校验函数。socket.cpp:437/469 的 recv/recvfrom 直接 memcpy(buf, nbuf->data, copy_len)，recvfrom 还写 src_addr/*addrlen；tcp.cpp:1096 的 Tcp::read 同样直接写 buf，send 路径直接从 buf 读。内核与用户共享地址空间且无 SMAP/PAN 检查，故 recv 到内核地址即任意内核写，send 自内核地址即信息泄漏。 更正：自发自收的 UDP 利用依赖本机回环/本机 IP 投递路径是否可用（未逐行核实），但未映射或内核地址直接导致内核缺页或内存破坏这一点不依赖该前提。i686 上网络可用，可达性最强；x86_64 上无网卡时 recv 侧难以拿到数据，但 send/sendto/getsockopt/select 等读写用户指针的路径仍可达。

#### V084 [critical·已确认·i686,x86_64·已修复] 监听端口收到 SYN 时在持有 tcp_lock 的情况下调用 pcb_new()，自旋锁自死锁，内核永久挂起

- 位置：`src/net/tcp.cpp:606`；相关：`src/net/tcp.cpp:564`、`src/net/tcp.cpp:890`
- 证据：Tcp::input 在 564 行 `tcp_lock.lock_irqsave(irq_state);` 后进入 `case TCP_LISTEN`，606 行 `tcp_pcb_t *new_pcb = net::Tcp::pcb_new();`；而 pcb_new 在 890 行 `sync::SpinlockIrqGuard guard(tcp_lock);` 再次获取同一把锁。src/kernel/sync/spinlock.cpp 的 Spinlock::lock() 是 `while (!try_lock()) cpu_relax();`，不可重入，且此时中断已关闭。
- 触发场景：任何进程 bind+listen 一个 TCP 端口后，远端（或 QEMU hostfwd）向该端口发一个 SYN：RX 中断 -> Ip::input -> Tcp::input -> LISTEN 分支 -> pcb_new() 在关中断状态下对已持有的 tcp_lock 无限自旋，整机硬挂起。即 TCP 服务端功能完全不可用，并且是远程可触发的内核死锁。
- 修复方向：拆出不加锁的 tcp_pcb_alloc()（只分配和初始化，不入链表），在 input 已持锁时调用它并手工入链；或在 LISTEN 分支先解锁再分配、重新加锁后再校验监听 PCB 仍然有效。
- 复核意见：tcp.cpp:564 lock_irqsave(tcp_lock) 后进入 TCP_LISTEN 分支，606 行调用 Tcp::pcb_new()，而 pcb_new 在 890 行用 SpinlockIrqGuard 再次获取同一把 tcp_lock，中间没有解锁（628 行才解锁）。src/kernel/sync/spinlock.cpp:48-56 的 lock() 是 atomic_xchg 忙等，不可重入，且此时中断已关，必然永久自旋。到达路径完整：SYS_LISTEN -> Socket::listen (syscall.cpp:364) -> Tcp::listen 把 PCB 放入 tcp_listen_pcbs，之后任何到该端口的 SYN 经 tcp_find_pcb 第二个循环命中。 更正：x86_64 启动时 e1000 被跳过，没有网卡收包，实际只在 i686 上可触发。Makefile 的 qemu 目标（452 行）用 -netdev user 且没有 hostfwd，所以默认运行配置下外部 SYN 进不来，需要 hostfwd/tap 才能复现；缺陷本身无疑。

#### V085 [high·已确认·all·已修复] ARP 没有重试/超时机制：PENDING 条目永不重发请求也永不过期，等待队列无界增长

- **修复**：PENDING ARP entries re-send the request every ARP_RETRY_INTERVAL (from Arp::resolve and from the new once-a-second maintenance work) and are released with their queue after ARP_MAX_RETRIES. The pending queue is capped at ARP_PENDING_MAX (4, oldest dropped) and is transmitted and freed after arp_cache_lock is dropped. Arp::cache_cleanup now has a caller, so the 5-minute expiry also works. 验证方式：unit test test_arp_pending_queue_bounded (queue bound, no duplicate request inside the interval); the time-based retry/give-up is untested
- 位置：`src/net/arp.cpp:186`；相关：`src/net/arp.cpp:377`、`src/net/arp.cpp:463`、`src/net/arp.cpp:365`、`src/net/ip.cpp:587`
- 证据：resolve() 中 `else if (entry->state == ARP_STATE_PENDING) { return -1; }` 直接返回而不重发 ARP 请求；`entry->retries` 全文件只被赋 0，从不递增，所以 cache_cleanup 中 `if (arp_cache[i].retries >= ARP_MAX_RETRIES)` 永远为假；而且 cache_cleanup() 在整个仓库没有任何调用者（RESOLVED 条目的 5 分钟超时同样永不生效）。queue_packet() 向 pending_queue 追加时没有长度上限。ARP_RETRY_INTERVAL 宏未被使用。
- 触发场景：第一次 ARP 请求丢失（或目标暂时不在线，或向本机自身 IP/不存在的主机发包）：条目停在 PENDING，之后发往该 IP 的每个包都只是被挂进 pending_queue 并返回成功，再也不会发 ARP 请求，目标永久不可达（直到 32 个其他条目把它 LRU 淘汰）。TCP 重传、应用循环 sendto 或 ping 会让队列无限增长，每个包占用最多约 2KB 内核堆，最终耗尽内存；且 Ip::output 一直返回 0，上层以为发送成功。
- 修复方向：为 PENDING 条目记录上次请求时间，在 resolve() 或周期定时器中按 ARP_RETRY_INTERVAL 重发并递增 retries，超过 ARP_MAX_RETRIES 后释放队列并置 FREE（可向上层报告主机不可达）；限制 pending_queue 长度（例如只保留最近 1~3 个包）；注册定时器周期调用 cache_cleanup。
- 复核意见：src/net/arp.cpp:186-189 对 PENDING 条目直接 return -1，不重发请求；retries 全仓库只在 arp.cpp:203/315 被赋 0，从不递增，故 arp.cpp:377 的判断恒假；全仓库 grep cache_cleanup 只有定义(arp.cpp:365)和声明(arp.h:160)，没有调用者，ARP_RETRY_INTERVAL 也未被引用。queue_packet(arp.cpp:463-488) 追加到链表尾且无长度上限，而 Ip::output(ip.cpp:587-588) 入队后返回 0，所以向不在线的同网段主机持续发包会无限占用内核堆，且首个 ARP 请求丢失后该 IP 只能等 LRU 淘汰才会再次请求。 更正：仅 i686 实际可达（x86_64 上 e1000 被跳过，arm64 不编译网络栈）。另外 arp_send_pending(arp.cpp:77-80) 成功发送后也不释放 buf（e1000 transmit 只拷贝不释放），即使解析成功，排队的包也会泄漏，修复时应一并处理。

#### V086 [high·已确认·i686,x86_64·已修复] net::Dhcp::input 没有任何调用者，且发送后立刻释放 68 端口的 PCB：DHCP 应答永远到不了状态机

- **修复**：A shared UDP PCB bound to 0.0.0.0:68 lives while any DHCP client is active; its receive callback calls Dhcp::input. Dhcp::timer is now driven once a second and retransmits DISCOVER/REQUEST (DHCP_MAX_RETRIES, then ERROR) and renew/rebind requests. 验证方式：unit test test_dhcp_acquires_lease (DISCOVER -> OFFER -> REQUEST -> ACK -> BOUND on a fake device); not run against QEMU's DHCP server; timer retries untested
- 位置：`src/net/dhcp.cpp:472`；相关：`src/net/dhcp.cpp:224`、`src/net/dhcp.cpp:308`、`src/net/udp.cpp:166`
- 证据：全仓库 grep `Dhcp::`，input 只有定义，udp.cpp/ip.cpp/net.cpp 都不调用它。同时 dhcp_send_discover 在 221-224 行 `sendto(...); Netbuf::free(buf); net::Udp::pcb_free(pcb);`，发送完就把绑定在 DHCP_CLIENT_PORT 的 PCB 删除，也没有通过 Udp::recv 注册回调。Udp::input（udp.cpp:130-171）找不到 68 端口的 PCB 时走 `Icmp::send_dest_unreachable` 并丢包。
- 触发场景：即使 DISCOVER 成功发出，服务器的 OFFER（目的端口 68）到达后 udp_find_pcb 返回 NULL，内核回一个 ICMP 端口不可达并丢弃报文；dhcp_handle_offer/ack/nak 永远不会执行，客户端永久停在 SELECTING，`dhcp status` 一直显示 SELECTING。
- 修复方向：在 Dhcp::start 时创建一个长期存在的 PCB，绑定 0.0.0.0:68，用 Udp::recv 注册回调，在回调里调用 Dhcp::input(buf->dev, buf->data, buf->len) 并释放 buf；Dhcp::stop 时再 pcb_free。发送函数复用这个 PCB 而不是每次新建/释放。
- 复核意见：全仓库 grep 显示 net::Dhcp::input 只有 src/net/dhcp.cpp:472 的定义，udp.cpp/net.cpp 中没有任何 DHCP 或 68 端口的分发逻辑，Udp::recv 也没有调用者为 DHCP 注册回调。dhcp_send_discover(dhcp.cpp:221-224) 与 dhcp_send_request(dhcp.cpp:305-308) 发送后立即 pcb_free，68 端口上没有任何长期 PCB，因此 OFFER/ACK 永远到不了 dhcp_handle_offer/ack，DHCP 客户端功能整体不可用。 更正：实际运行中会先被 net-apps-3 挡住（DISCOVER 根本发不出去），本条是独立的第二个根因，两者都需修复。x86_64 上 e1000 被跳过，实际只影响 i686。

#### V087 [high·已确认·i686,x86_64·已修复] Dhcp::start 先把接口 IP 清零再发 DISCOVER，而 Ip::output 拒绝 ip_addr==0 且不支持广播：DISCOVER 必然发送失败，并且破坏现有网络配置、占住客户端槽位

- **修复**：DISCOVER can now be sent (broadcast from 0.0.0.0, on the client's own device via a new optional device argument to Udp::sendto). Dhcp::start saves the address it clears and, if DISCOVER cannot be sent, restores it and frees the client slot. Stopping before a lease is obtained, or running out of retries, also restores the address. The client table is guarded by a Mutex instead of a spinlock. 验证方式：unit tests test_dhcp_start_failure_rolls_back and test_dhcp_acquires_lease
- 位置：`src/net/dhcp.cpp:560`；相关：`src/net/dhcp.cpp:564`、`src/net/dhcp.cpp:462`、`src/net/dhcp.cpp:396`、`src/net/ip.cpp:547`、`src/net/ip.cpp:580`、`src/net/udp.cpp:336`
- 证据：`net::Netdev::set_ipaddr(dev, 0); ... int ret = dhcp_send_discover(client);`。发送路径 Udp::sendto -> Ip::output（ip.cpp:547）：`if (dev->ip_addr == 0) { LOG_ERROR_MSG("ip: Device %s has no IP address\n"...); return -1; }`。此外 Ip::output 对 255.255.255.255 没有任何广播特判：get_next_hop 算出网关或 0xFFFFFFFF 后直接交给 Arp::resolve，等于对广播地址做 ARP（或把广播包单播给网关 MAC）。Udp::sendto 还固定使用 Netdev::get_default() 而不是 client->dev。start 失败时 `dhcp_lock.unlock_irqrestore; return ret;`，不回滚 dhcp_alloc_client 分配的槽位，也不恢复原 IP。
- 触发场景：内核 shell 执行 `dhcp start`：接口原有静态 IP 被清成 0，dhcp_send_discover 返回 -1，shell 打印 `Failed to start DHCP client`。此后网卡没有 IP，所有 Ip::output 都失败（网络整体不可用）；再次 `dhcp start` 因 dhcp_find_client 找到残留槽位而返回 -1（“已经在运行”），必须先 `dhcp stop` 且手工重新配置 IP。dhcp_handle_offer 里的 REQUEST、dhcp_handle_nak 里的重新 DISCOVER 也因同一原因必然失败。
- 修复方向：为 DHCP 提供绕过路由/ARP 的原始发送路径：源 IP 0.0.0.0、目的 255.255.255.255、目的 MAC ff:ff:ff:ff:ff:ff，直接在 client->dev 上经 Ethernet::output 发送；Ip::output 对受限广播/定向广播地址直接使用广播 MAC，并允许 DHCP 在 ip_addr==0 时发送。start 失败时释放槽位（client->dev = NULL）并恢复原地址，或者在拿到 ACK 之前不要清除旧配置。
- 复核意见：src/net/dhcp.cpp:560 先 set_ipaddr(dev,0)（netdev.cpp:271-276 无条件写 0），随后 dhcp_send_discover -> Udp::sendto(udp.cpp:336 固定用 get_default) -> Ip::output，在 ip.cpp:547-550 因 dev->ip_addr==0 返回 -1，DISCOVER 必然失败。Dhcp::start(dhcp.cpp:564-568) 失败时既不恢复原 IP 也不释放 dhcp_alloc_client 占用的槽位，kernel_shell.cpp:1714 的 `dhcp start` 之后网卡无 IP、所有 Ip::output 失败，再次 start 因 dhcp_find_client 命中而返回 -1。ip.cpp 的 output 路径中也确实没有对 255.255.255.255 的广播 MAC 特判（仅 input 侧 ip.cpp:476 有）。 更正：补充：Dhcp::start 在持有 dhcp_lock（lock_irqsave，关中断）期间调用 Netdev::set_ipaddr，而后者内部取 sync::MutexGuard(dev->lock)（netdev.cpp:274），在自旋锁/关中断区内取互斥锁本身也是隐患。实际只影响 i686（x86_64 无 e1000）。

#### V088 [high·待确认·all·已修复] Ip::input 不按 total_length 裁剪缓冲区，以太网填充字节被当作上层负载（短 TCP 段校验和失败、ICMP 回显长度错误）

- **修复**：Ip::input trims the buffer to total_length (new Netbuf::trim) before dispatching; Udp::input trims to the UDP length. 验证方式：unit tests test_ip_input_drops_ethernet_padding and test_tcp_client_connection (all injected TCP segments are padded to 60 bytes)
- 位置：`src/net/ip.cpp:466`；相关：`src/net/tcp.cpp:542`、`src/net/tcp.cpp:559`、`src/net/icmp.cpp:54`、`src/net/icmp.cpp:72`
- 证据：`if (total_len < hdr_len || total_len > buf->len) {drop}` 之后只做 `net::Netbuf::pull(buf, hdr_len)`，从未把 buf->len/tail 收缩到 total_len。上层全部使用 buf->len：tcp.cpp:542 `net::Tcp::checksum(src_ip, dst_ip, tcp, buf->len)`（伪首部长度也用 buf->len）、tcp.cpp:559 `data_len = buf->len - hdr_len`；icmp.cpp:54 `checksum(icmp, buf->len)`、icmp.cpp:72 `data_len = buf->len - sizeof(icmp_header_t)`。以太网最小帧 60 字节，IP 报文小于 46 字节时尾部带填充（QEMU e1000 模型会把短帧补到 60 字节，驱动只剥离 CRC）。
- 触发场景：对端发来不带数据的 TCP 段（纯 ACK/FIN/RST 为 40 字节 IP，带 MSS 选项的 SYN-ACK 为 44 字节）：帧被补齐到 60 字节，Tcp::input 看到 buf->len=26 而实际 TCP 长度是 20/24，伪首部长度不符 -> 校验和不匹配 -> 段被丢弃（三次握手的 SYN-ACK、所有纯 ACK 都收不到）；即便校验碰巧通过，也会把 2~6 字节填充当作数据交给应用。`ping -s 4` 时 Echo Reply 会多带出填充字节，长度与请求不符。未验证的假设：收到的短帧确实带填充（真实以太网与 QEMU e1000 均如此，但未做动态确认）。
- 修复方向：在通过 total_len 校验后立即裁剪：`buf->len = total_len; buf->tail = buf->data + total_len;`（再 pull 头部），使所有上层协议看到的长度等于 IP 负载长度。
- 复核意见：src/net/ip.cpp:466-502 只校验 total_len <= buf->len，之后仅 pull 头部，从不把 buf->len 收缩到 total_len。src/net/tcp.cpp:542 用 buf->len 计算含伪首部的校验和、tcp.cpp:559 用 buf->len 算 data_len；src/net/icmp.cpp:54、72 同样使用 buf->len。src/drivers/x86/e1000.cpp:476-483 按 desc->length 原样拷贝整帧，不去除填充。代码缺陷属实；但短帧是否确实带填充到达（QEMU e1000 模型补齐到 60 字节）只是静态推断，未动态确认。 更正：ICMP 的校验和不受零填充影响（icmp.cpp:54 仍通过），只是回显应答会多带出填充字节。UDP 路径在 udp.cpp:103-106 使用 udp->length，基本不受影响。受影响最重的是 TCP：44 字节的 SYN-ACK 和 40 字节的纯 ACK 会因伪首部长度不符被丢弃。只在 i686 上可达。

#### V089 [high·已确认·all·已修复] Ip::output 不支持广播/未配置地址发送：源 IP 为 0 直接失败，广播目的地址被拿去做 ARP 解析（DHCP 无法工作）

- **修复**：Ip::output sends limited and directed broadcast to ff:ff:ff:ff:ff:ff and multicast to 01:00:5e:xx without ARP, allows a limited broadcast from an interface with no address (source 0.0.0.0), and loops packets for the interface's own address or 127/8 back through the receive queue (new Netdev::loopback; input accepts 127/8 only from itself). 验证方式：unit tests test_ip_broadcast_from_unconfigured_interface and test_ip_loopback_to_own_address; multicast mapping untested
- 位置：`src/net/ip.cpp:547`；相关：`src/net/ip.cpp:580`、`src/net/dhcp.cpp:221`、`src/net/dhcp.cpp:560`
- 证据：`if (dev->ip_addr == 0) { LOG_ERROR_MSG("ip: Device %s has no IP address\n"...); return -1; }`；之后无论目的地址是什么都执行 `net::Arp::resolve(dev, next_hop, dst_mac)`，没有对 255.255.255.255、子网定向广播、多播地址使用广播/多播 MAC 的特判，也没有对本机 IP/127.0.0.0/8 的回环处理。而 src/net/dhcp.cpp:560 先 `Netdev::set_ipaddr(dev, 0)`，再由 dhcp_send_discover 调 `Udp::sendto(pcb, buf, 0xFFFFFFFF, DHCP_SERVER_PORT)`。
- 触发场景：在 shell 执行 dhcp start：IP 被清零后 DISCOVER 经 Udp::sendto -> Ip::output，立刻因 ip_addr==0 返回 -1，DHCP 永远拿不到地址。即使设备已有 IP，向 255.255.255.255 或子网广播地址发 UDP 也会为该地址发 ARP 请求，无人应答，报文永久滞留在 pending 队列（并触发前述无界队列泄漏）。向本机自己的 IP 发包同样因 ARP 无应答而黑洞。
- 修复方向：在 Ip::output 中：目的为受限广播/定向广播时直接使用 ETH_BROADCAST_ADDR，多播映射到 01:00:5e:xx，跳过 ARP；允许源地址为 0.0.0.0 的广播发送（DHCP）；目的为本机地址或 127/8 时走回环（克隆后送回 Ip::input）。
- 复核意见：src/net/ip.cpp:547-550 在 dev->ip_addr==0 时直接返回 -1。src/net/dhcp.cpp:560 先 set_ipaddr(dev, 0)，再由 dhcp_send_discover（dhcp.cpp:221）经 Udp::sendto（udp.cpp:368）调用 Ip::output，因此 DISCOVER 必然失败，DHCP 无法取得地址。ip.cpp:580 对任何目的地址都调用 Arp::resolve（arp.cpp:170-213），全栈没有广播、多播或本机地址的特判；向 255.255.255.255 发包会建立永远 PENDING 的 ARP 条目，报文滞留在 pending 队列。 更正：i686 上 ping 可用，说明地址是静态配置的，所以实际影响是 DHCP 功能完全不可用，以及广播/多播/回环发送不可用，不影响普通单播。只在 i686 上可达。

#### V090 [high·已确认·all·已修复] 发送路径 Netbuf 所有权约定自相矛盾：发送成功后无人释放（每发一个包泄漏一个 Netbuf），而 DHCP 无条件释放又会与 ARP 等待队列形成悬空指针/双重释放

- **修复**：Same ownership rule: all callers (arp request/reply, icmp x3, Udp::output, tcp, socket send/sendto) free unconditionally after sending, and the ARP pending queue frees its clones after transmitting them, so nothing leaks on success and nothing is freed twice. 验证方式：unit tests test_arp_pending_packet_survives_caller_free and test_arp_pending_queue_bounded; leak itself only by code review
- 位置：`src/net/ip.cpp:584`；相关：`src/net/arp.cpp:77`、`src/net/arp.cpp:243`、`src/net/arp.cpp:279`、`src/net/icmp.cpp:170`、`src/net/icmp.cpp:210`、`src/net/icmp.cpp:253`
- 证据：arp.cpp:75 的注释声称 `net::Ethernet::output 成功后，buf 由网卡驱动负责释放`，所有调用者都只在失败时释放：`int ret = net::Ip::output(NULL, buf, dst_ip, IP_PROTO_ICMP); if (ret < 0) { net::Netbuf::free(buf); }`（icmp.cpp:210、arp.cpp:243/279、udp.cpp:212、tcp.cpp:450/500 等）。但唯一的驱动 e1000_netdev_transmit（src/drivers/x86/e1000.cpp:383-425）只是 `memcpy(dev->tx_buffers[cur], buf->data, buf->len)` 后返回 0，从不释放 buf（E1000::send 甚至传入栈上的 Netbuf）。arp_send_pending 发送成功后同样不释放。另一方面 Ip::output 在“已立即发出”和“已挂入 ARP pending 队列”两种情况下都返回 0，调用者无法区分所有权是否已转移；dhcp.cpp:221-223 `ret = Udp::sendto(...); net::Netbuf::free(buf);` 则无条件释放。
- 触发场景：(1) 泄漏：主机持续 ping CastorOS，每个 Echo Reply（以及每个 ARP 应答、每个 TCP 段/ACK、每个 UDP 报文、对每个无监听端口的广播 UDP 回的 ICMP 不可达）都泄漏 sizeof(Netbuf)+128+len 字节的内核堆，正常使用网络一段时间后 kmalloc 耗尽。(2) UAF：设备已有 IP 时 DHCP 发送到一个尚未解析的地址，Ip::output 把 buf 挂入 arp_entry.pending_queue 并返回 0，dhcp 随即 free(buf)；之后 queue_packet 沿 tail->next 遍历已释放内存，或 ARP 解析完成/条目被淘汰时再次发送并 free -> use-after-free / double free。
- 修复方向：统一约定：例如“Ip::output/Ethernet::output 一旦被调用即接管 buf，成功、失败、排队都由下层负责释放”，在 Netdev::transmit 返回后（驱动为拷贝式）由 Netdev::transmit 或 Ethernet::output 释放；或让 Ip::output 用不同返回值区分“已发送(调用者释放)”与“已排队(所有权转移)”。然后逐个修正调用者（含 dhcp.cpp 和 arp_send_pending）。
- 复核意见：泄漏属实：src/drivers/x86/e1000.cpp:383-425 只 memcpy 后返回 0，src/net/netdev.cpp:237-246 和 src/net/ethernet.cpp:115 发送成功后都不释放 buf。所有调用者只在 ret<0 时释放（udp.cpp:212、icmp.cpp:170、tcp.cpp:450、arp.cpp:245/281），arp_send_pending（arp.cpp:77-80）成功后也不释放，与 arp.cpp:75 的注释“由驱动释放”矛盾，所以每个成功发出的包泄漏一个 Netbuf 加数据区。UAF 部分在代码上成立：ip.cpp:587-588 排队后返回 0，而 dhcp.cpp:223/307/359 无条件 free，会在 arp.cpp:463-488 的 pending_queue 中留下悬空指针。 更正：UAF/双重释放目前基本是潜在问题：DISCOVER 和初始 REQUEST 在 ip_addr==0 时于 ip.cpp:547 提前返回 -1，不会入队；只有设备已有 IP 且 DHCP 向未解析地址发送（续租、release）时才触发，而 DHCP 因 net-link-ip-8 本就拿不到租约。DHCP 立即发送成功的那条路径，无条件 free 反而是正确的。核心问题是发送成功路径的泄漏，只在 i686 上可达。

#### V091 [high·已确认·all·已修复] net::Stack::init() 从未被调用：TCP 定时器未注册，各协议 init 均未执行

- **修复**：kernel.cpp calls net::Stack::init() (instead of Netdev::init() alone) before the NIC driver registers its device. Stack::init also registers a once-a-second kworker item that runs Arp::cache_cleanup, Ip::reass_timer and Dhcp::timer. 验证方式：boot regression on i686 and x86_64 (init messages in the boot log); test_tcp_send_failure_is_retransmitted calls Tcp::timer directly
- 位置：`src/net/net.cpp:22`；相关：`src/kernel/kernel.cpp:534`、`src/net/net.cpp:50`、`src/net/arp.cpp:365`、`src/net/ip.cpp:233`、`src/net/tcp.cpp:506`
- 证据：全仓库 grep `Stack::init` 只有定义没有调用；src/kernel/kernel.cpp:534 只调用了 `net::Netdev::init()`，随后直接初始化 e1000 并 `Netdev::up(eth0)`。Arp::init / Ip::init / Icmp::init / Udp::init / Tcp::init / Socket::init 以及 `tcp_timer_id = drivers::Timer::register_callback(net_tcp_timer_callback, NULL, 100, true)` 全都只存在于 Stack::init 中。静态变量靠 BSS 清零碰巧可用（锁=0、表=0），所以收发基本能工作，掩盖了问题。
- 触发场景：正常启动后建立 TCP 连接：由于 net::Tcp::timer() 永远不会被调度，任何丢失的 SYN/数据段都不会重传，RTO、TIME_WAIT 回收、连接超时都不生效，丢一个包连接就永久挂死、PCB 永不回收；tcp_isn 永远从 0 开始（ISN 可预测，重启后与旧连接序列号冲突）。另外若日后简单地在 e1000 初始化之后补上 Stack::init()，其中的 Netdev::init() 会 memset 掉已注册的网卡表。
- 修复方向：在 kernel.cpp 中于网卡驱动初始化之前调用 net::Stack::init()（并去掉重复的 Netdev::init 调用，或让 Netdev::init 幂等）；同时在其中注册 Arp::cache_cleanup 与 Ip::reass_timer 的周期定时器（目前这两个函数也没有任何调用者）。
- 复核意见：全仓库 grep 确认 net::Stack::init 只有 src/net/net.cpp:22 的定义，没有任何调用者；src/kernel/kernel.cpp:534 只调用 net::Netdev::init()。net::Tcp::timer() 的唯一调用点是 net.cpp:19 的回调，而该回调仅在 net.cpp:50（Stack::init 内）注册，因此 tcp.cpp:1316 的重传/超时处理永不运行。Arp::cache_cleanup(arp.cpp:365)、Ip::reass_timer(ip.cpp:233) 同样没有调用者。 更正：“tcp_isn 永远从 0 开始”不准确：tcp.cpp:45 的 tcp_gen_isn 每次都会加上 uptime_ms*250000，ISN 并非恒为 0，只是缺少 Tcp::init 中的初始种子（仍可预测）。实际影响限于 i686（x86_64 跳过 e1000，arm64 不编译网络栈）。
- 被 2 个独立审计者重复报告（net-link-ip-5, x-doc-drift-6）

#### V092 [high·已确认·i686,x86_64·未修复] socket 描述符使用独立的全局表，未接入每进程 fd 表：close() 关不掉 socket，进程退出不回收，进程间互相可见

- **未修复**：Sockets still live in a global socket_table indexed from 0, separate from the per-process FdTable. The correct fix makes a socket an fs_node with NodeOps registered through kernel::FdTable, and re-routes SYS_CLOSE/SYS_FCNTL/read/write/dup/fork/exit in src/kernel/syscall.cpp, src/kernel/syscalls/fs.cpp, fd_table.cpp, task.cpp and the user library. That is a cross-subsystem redesign in files owned by other batches; a partial fix inside src/net would leave the fd-number collision in place. Left untouched.
- 位置：`src/net/socket.cpp:50`；相关：`src/net/socket.cpp:133`、`src/net/socket.cpp:485`、`src/kernel/syscall.cpp:143`、`src/kernel/syscall.cpp:474`、`src/kernel/task.cpp:202`
- 证据：`#define MAX_SOCKETS 64` / `static socket_t *socket_table[MAX_SOCKETS];`，socket()/accept() 返回的是这个全局数组的下标（从 0 开始），与 kernel::FdTable 完全无关。net::Socket::closesocket 没有任何系统调用入口（syscall.cpp 中未注册，用户库 close() 走 SYS_CLOSE -> syscall::Fs::close -> FdTable::free）。SYS_FCNTL 只映射到 net::Socket::fcntl，read/write/dup/fork 也完全不认识 socket。
- 触发场景：(1) 进程第一次调用 socket() 得到 0（与 stdin 同号），随后 `close(sockfd)` 实际关闭的是该进程的 stdin(fd 0)，socket 与其 TCP/UDP PCB、绑定端口永不释放；64 个 socket（全系统合计）用完后所有进程的 socket() 永久失败，只能重启。(2) 进程退出或被 kill 时 Scheduler::free 只清理 FdTable，socket 泄漏、监听端口一直被占用。(3) 任何进程都可以对别的进程的 socket 号调用 recv/send/shutdown，读取或劫持其连接。(4) 对普通文件 fd 调 fcntl(F_GETFL/F_SETFL) 会操作到同号的 socket，对 socket 调 read()/write()/dup2() 则操作到同号文件。
- 修复方向：把 socket 做成 fs_node_t（或通用 file 对象）并通过 kernel::FdTable::alloc 分配 fd：socket()/accept() 返回进程 fd，ops 的 read/write/close 转发到 socket 层，close 与进程退出时自动调用 closesocket；select/fcntl 按 fd 查 FdTable 再分发。
- 复核意见：src/net/socket.cpp:50 的 socket_table 是全局静态数组，socket_alloc_fd(socket.cpp:59-71) 返回从 0 开始的下标，socket_get(socket.cpp:90) 没有属主检查。全仓库 grep closesocket 只有定义(socket.cpp:485)和声明(socket.h:214)，没有任何调用者或系统调用入口；SYS_CLOSE 走 syscall::Fs::close -> FdTable::free(syscalls/fs.cpp:213-221)，进程清理(task.cpp:202-210, 952-959)只处理 fd_table。SYS_FCNTL 只路由到 net::Socket::fcntl(syscall.cpp:470-475)，因此泄漏、跨进程可见、编号冲突均成立。 更正：仅 i686/x86_64 编译（syscall.cpp:344 的 #if !defined(ARCH_ARM64)）。x86_64 上 e1000 被跳过，但 socket 层仍会初始化，socket() 仍可消耗表项。
- 被 4 个独立审计者重复报告（kernel-fs-net-syscalls-5, x-doc-drift-7, x-user-boundary-6, gap6-abi-struct-parity-lp64-1）

#### V093 [high·已确认·i686,x86_64·未修复] closesocket 没有任何系统调用入口，且 socket fd 与进程 VFS fd 是两个互不相干的全局/进程编号空间：socket 永不关闭，用户 close(sockfd) 关掉的是无关文件

- **未修复**：Same root cause as kernel-fs-net-syscalls-5: closesocket has no syscall entry and close(sockfd) hits the VFS fd table. Needs the socket-as-fd integration (or a new syscall number plus user-library change), all outside this batch's ownership. Note closesocket still calls Tcp::close followed immediately by pcb_free, so once it is wired up it must wait for send_buf/FIN to drain.
- 位置：`src/net/socket.cpp:485`；相关：`src/net/socket.cpp:50`、`src/net/socket.cpp:59`、`src/kernel/syscall.cpp:579`、`user/lib/src/socket.cpp:220`
- 证据：全仓库 grep `closesocket` 只有 socket.cpp 的定义和 socket.h 的声明，syscall.cpp 中没有包装器调用它。socket fd 来自全局 `socket_table[MAX_SOCKETS]`（下标从 0 开始，所有进程共享，无属主检查）；用户库 user/lib/src/socket.cpp 用 `close(sockfd)` 关闭，而 SYS_CLOSE 走 syscall::Fs::close -> 当前进程的 FdTable。进程退出时也没有任何代码释放其 socket。
- 触发场景：进程 A 调 socket() 得到 0，使用后 close(0)：实际关闭的是自己的 stdin（fd 0），socket 0 和它的 PCB（约 16KB 缓冲、绑定的端口）永远泄漏，TCP 连接也不会发 FIN。全系统累计创建 64 个 socket 后 socket() 永久失败。同时任意进程都可以对别的进程的 socket 编号执行 recv/send/bind（无隔离）。
- 修复方向：把 socket 做成 VFS 节点/文件对象并登记到进程 fd_table（close、进程退出、fork 引用计数统一处理），或至少增加 SYS_CLOSESOCKET 并让 socket 记录属主、在进程退出时回收；socket fd 不应与文件 fd 数值重叠。
- 复核意见：全仓库 grep closesocket 只有 src/net/socket.cpp:485 的定义和 src/include/net/socket.h:214 的声明，src/kernel/syscall.cpp:579-594 注册的 socket 系统调用里没有 close 入口，SYS_CLOSE(syscall.cpp:545) 只走 VFS fd 表。socket fd 是全局 socket_table[64] 的下标（socket.cpp:49-66，socket_get 在 91-100 行无属主检查），而用户库 user/lib/src/socket.cpp:220/225/242 等用 close(sockfd) 关闭；task.cpp 与 syscalls/fs.cpp 中也没有任何 socket 回收代码。因此 socket 和 PCB 永不释放，64 个之后 socket() 永久失败，且 close 关掉的是同号的文件 fd。 更正：仅 i686/x86_64 可达（arm64 用 #if !defined(ARCH_ARM64) 排除）。x86_64 上 e1000 被跳过，但 socket() 本身仍可创建并泄漏。

#### V094 [high·已确认·i686,x86_64·已修复] 快速重传用 tcp_send_segment 以 snd_nxt 作为序列号把旧数据当新数据发送，破坏字节流；重复 ACK 判定也过宽

- **修复**：Timer and fast retransmit both go through a new tcp_xmit() that uses the segment's original sequence number and touches neither snd_nxt nor the queue. Only segments with no data, no SYN/FIN and an unchanged window count as duplicate ACKs. A retransmitted SYN no longer carries ACK. 验证方式：unit tests test_tcp_client_connection (dup-ACK and fast-retransmit phase) and test_tcp_send_failure_is_retransmitted
- 位置：`src/net/tcp.cpp:213`；相关：`src/net/tcp.cpp:420`、`src/net/tcp.cpp:759`
- 证据：`tcp_send_segment(pcb, seg->flags | TCP_FLAG_ACK, seg->data, seg->data_len);` —— tcp_send_segment 使用 `tcp->seq_num = htonl(pcb->snd_nxt)`（420 行）而不是 seg->seq，并且会推进 snd_nxt、再次调用 tcp_queue_unacked 追加一个新段。重复 ACK 的判定是 759 行 `ack == pcb->snd_una && pcb->unacked`，不要求段不带数据、窗口不变，因此对端的普通数据段也计数。
- 触发场景：本端有未确认数据时，对端连续发来 3 个带数据的段（ack 不变）：dup_ack_count 到 3，本端把“丢失”的数据以新的序列号再发一遍——对端收到的是流中重复插入的一段数据（数据损坏），原来的空洞仍未补上，同时 unacked 队列里多出一个重复段。
- 修复方向：重传必须使用 seg->seq 构造报文（抽出一个不修改 snd_nxt、不入队的 tcp_xmit_seg()，供超时重传和快速重传共用）；重复 ACK 只统计无数据、无 SYN/FIN、窗口未变化的段。
- 复核意见：src/net/tcp.cpp:213 快速重传调用 tcp_send_segment(pcb, seg->flags|ACK, seg->data, seg->data_len)，而 tcp_send_segment 在 420 行用 pcb->snd_nxt 作序列号、444 行推进 snd_nxt、456-459 行再次 tcp_queue_unacked，完全没有使用 seg->seq，旧数据被当作新字节插入流中。重复 ACK 判定在 759 行仅为 ack==snd_una && pcb->unacked，不排除带数据或窗口更新的段，对端普通数据段也会计数。 更正：若被重传的段带 FIN/SYN 标志，还会额外多消耗一个序列号并重复发送 FIN。触发条件是本端有未确认段时收到 3 个 ack 不前进的段（真实丢包或双向传输时成立）。

#### V095 [high·已确认·i686,x86_64·已修复] 接收缓冲区满时数据被静默丢弃但仍推进 rcv_nxt 并 ACK，且通告窗口恒为 4096：TCP 字节流丢数据

- **修复**：rcv_nxt advances only by bytes actually stored; the advertised window follows free buffer space; Tcp::read compacts the buffer and sends a window update when it reopens; overlapping retransmissions are trimmed; every data segment gets an ACK; FIN takes effect only when all data before it has arrived. 验证方式：unit test test_tcp_client_connection (9000 bytes into the 8192-byte buffer, zero window, window update, retransmit, byte-exact readback)
- 位置：`src/net/tcp.cpp:351`；相关：`src/net/tcp.cpp:300`、`src/net/tcp.cpp:424`、`src/net/tcp.cpp:1100`
- 证据：`if (pcb->recv_buf && pcb->recv_len + copy_len <= pcb->recv_buf_size) { memcpy(...); pcb->recv_len += copy_len; } pcb->rcv_nxt += data_len;` —— 放不下时不拷贝却照样推进 rcv_nxt，随后 780 行发送 ACK 确认。rcv_wnd 自 pcb_new 设为 TCP_DEFAULT_WINDOW 后从不更新（424 行始终通告 4096）。Tcp::read 只有在全部读完时才把 recv_len/recv_read_pos 归零（1100 行），部分读取后空间不回收。tcp_ooseq_merge 300-304 行同样问题。
- 触发场景：对端连续发送超过 8192 字节（例如 HTTP 下载），应用读取稍慢或每次只读一部分：recv_len 到达 8192 后，后续段全部被“确认但丢弃”，对端认为已送达不再重传，应用读到的流中间缺失数据且无任何错误指示。
- 修复方向：放不下的数据不能确认：只接收能放入的部分并按实际接收量推进 rcv_nxt；rcv_wnd 按剩余空间动态通告；接收缓冲改为环形缓冲或在 read 后 memmove 压缩，并在窗口重新打开时发窗口更新。
- 复核意见：src/net/tcp.cpp:351-355 在 recv_len+copy_len 超过 recv_buf_size(8192, tcp.cpp:33/876) 时跳过 memcpy 却仍执行 rcv_nxt += data_len，随后 776-780 行因 rcv_nxt 变化而发送 ACK；tcp_ooseq_merge 300-304 行相同。rcv_wnd 只在 865 行设为 TCP_DEFAULT_WINDOW(4096) 后再无赋值（grep 确认），424 行恒通告 4096；Tcp::read 1100-1103 行只有全部读完才把 recv_len 清零，部分读取不回收空间。因此应用读取落后时后续段被确认但丢弃，字节流静默缺失。 更正：丢弃是整段粒度（放不下就整段不拷贝），不是截断。实际只在 i686 上可通过真实网卡触发（x86_64 的 e1000 被跳过，仅回环可达）。

#### V096 [high·已确认·i686,x86_64·已修复] 发送成功路径上 netbuf 无人释放：每发送一个 TCP/UDP 报文泄漏一个 netbuf

- **修复**：TCP/UDP send paths free the Netbuf after Ip::output on success as well as failure (tcp_xmit, tcp_send_rst, Udp::output, Socket::send/sendto). 验证方式：boot regression plus the TCP/UDP netstack tests exercising these paths; no leak counter
- 位置：`src/net/tcp.cpp:450`；相关：`src/net/tcp.cpp:500`、`src/net/tcp.cpp:1378`、`src/net/udp.cpp:212`、`src/net/udp.cpp:368`、`src/net/socket.cpp:347`、`src/net/socket.cpp:392`
- 证据：`int ret = net::Ip::output(dev, buf, pcb->remote_ip, IP_PROTO_TCP); if (ret < 0) { net::Netbuf::free(buf); return ret; }`，成功时不释放。Ip::output -> Ethernet::output -> Netdev::transmit -> e1000_netdev_transmit 只是 `memcpy(dev->tx_buffers[cur], buf->data, buf->len)` 后返回 0，从不 free；arp.cpp 注释写着“成功后 buf 由网卡驱动负责释放”，但驱动并未实现。
- 触发场景：ARP 已解析的正常情况下，每个 SYN/ACK/数据段/RST/重传段、每个 UDP 数据报都泄漏 sizeof(Netbuf)+ (128+len) 字节堆内存。持续收发（例如下载文件时每收一个段回一个 ACK）很快耗尽内核堆，之后 kmalloc 失败导致全系统异常。
- 修复方向：明确所有权约定：要么驱动 transmit 成功后释放 buf（同步拷贝型驱动最简单的做法是在 Netdev::transmit 成功后 free），要么所有调用者在 Ip::output 返回 0 且未被 ARP 排队时释放。需要区分“已排入 ARP pending 队列”与“已发送”两种成功。
- 复核意见：tcp.cpp:450-462、tcp.cpp:500-503、tcp.cpp:1378-1381、udp.cpp:212-215、icmp.cpp:170/210/253 全部只在 ret<0 时释放 buf。下游 ip.cpp:584 -> ethernet.cpp:115 -> netdev.cpp:237 -> e1000.cpp:383-425 只做 memcpy 到 tx_buffers 后返回 0，全链路没有任何 Netbuf::free；arp.cpp:75 的注释声称驱动负责释放但驱动未实现。每个成功发送的报文泄漏一个 Netbuf 结构加 NETBUF_HEADROOM+len 的数据区（netbuf.cpp 两次 kmalloc）。 更正：泄漏范围比报告更广：ICMP echo/reply（icmp.cpp:170/210/253）、ARP 请求/应答（arp.cpp:243/279）以及 arp_send_pending（arp.cpp:77）成功时同样不释放。x86_64 启动时 e1000 被跳过，实际只在 i686 上可触发。修复时还要注意 ip.cpp:586-588 的 ARP 排队成功也返回 0，此时不能由调用者释放。

#### V097 [high·已确认·i686,x86_64·已修复] TCP/UDP 输入使用含以太网填充的 buf->len 计算校验和与数据长度，短报文（纯 ACK、SYN-ACK、FIN）被全部丢弃

- **修复**：Same trim: padded SYN-ACK/ACK/FIN segments now pass the TCP checksum and carry no phantom data; short UDP datagrams no longer return padding bytes. 验证方式：unit tests test_tcp_client_connection and test_ip_input_drops_ethernet_padding
- 位置：`src/net/tcp.cpp:542`；相关：`src/net/tcp.cpp:559`、`src/net/udp.cpp:134`、`src/net/ip.cpp:502`
- 证据：`uint16_t calc_checksum = net::Tcp::checksum(src_ip, dst_ip, tcp, buf->len);` 和 559 行 `uint32_t data_len = buf->len - hdr_len;`。Ip::input 只检查 `total_len > buf->len`，随后 `Netbuf::pull(buf, hdr_len)`，从不把 buf->len 裁剪到 IP total_length；e1000 接收用描述符长度 `desc->length` 整帧上送（最小帧 60 字节，可能还含 FCS）。因此 IP 总长 < 46 的 TCP 段，buf->len 比真实 TCP 长度多出填充字节，伪首部长度字段不同 -> 校验和必然不匹配。
- 触发场景：connect() 发出 SYN，对端回 SYN-ACK（带 MSS 选项时 IP 总长 44，纯 ACK/FIN 为 40），帧被填充到 60 字节；Tcp::input 以 26（或更大）作为 TCP 长度算校验和，打印 "tcp: Invalid checksum" 并丢弃，握手永远无法完成、ACK 永远收不到。即便校验碰巧通过，填充字节也会被当作 data_len>0 的数据写入接收缓冲并推进 rcv_nxt。UDP 同理：134 行只 pull 头部，短数据报的 recv()/recvfrom() 返回值包含尾部填充字节。未验证的假设：网卡/QEMU 上送的帧长度包含填充（QEMU e1000 对短帧补齐到 60 字节）。
- 修复方向：在 Ip::input 中把 buf->len/tail 裁剪到 total_len（重组路径同样处理）；Udp::input 再把长度裁剪到 udp_len。
- 复核意见：ip.cpp:465-469 只检查 total_len > buf->len，502 行 pull 掉 IP 头后从不把 buf->len 裁剪到 total_length；e1000.cpp:476-482 按 desc->length 整帧上送（RCTL 设了 SECRC，e1000.cpp:271，所以不含 FCS，但含最小帧填充）。tcp.cpp:542 用 buf->len 作为伪首部的 TCP 长度，559 行用它算 data_len，所以被填充到 60 字节的短帧（纯 ACK 多 6 字节、带 MSS 的 SYN-ACK 多 2 字节）校验和必然不等并被丢弃。QEMU 的 e1000/slirp 会把短帧补齐到 60 字节，这与实测 ping 正常并不矛盾：ICMP 校验和没有伪首部长度字段，尾部零填充不改变结果。 更正：长度多出的是填充字节而非 FCS（SECRC 已开启）。唯一未在本仓库内静态验证的是 QEMU 补齐短帧的行为，但以太网最小帧长是标准行为，真实网卡同样如此。UDP 侧 udp.cpp:134 只 pull 头部，短数据报会把填充字节当作载荷返回给 recvfrom；UDP 校验和是否也用 buf->len 需在修复时一并检查。x86_64 无网卡，实际只影响 i686。

#### V098 [high·已确认·i686,x86_64·已修复] tcp_pcb::next 同时被 tcp_pcbs、pending_queue、accept_queue 三个链表复用，新连接入队会截断/破坏活动 PCB 链表

- 位置：`src/net/tcp.cpp:624`；相关：`src/net/tcp.cpp:713`、`src/net/tcp.cpp:718`、`src/net/tcp.cpp:891`、`src/net/tcp.cpp:1030`
- 证据：pcb_new() 已执行 `pcb->next = tcp_pcbs; tcp_pcbs = pcb;`（891-892 行）把新 PCB 放到活动链表头；LISTEN 分支随后 `new_pcb->next = pcb->pending_queue; pcb->pending_queue = new_pcb;`（624-625 行）覆盖同一个 next 指针。握手完成时 713 行 `*pp = pcb->next`、718 行 `pcb->next = listen->accept_queue`，accept() 中 1031 行 `new_pcb->next = NULL` 又继续改写同一指针。
- 触发场景：（在修复上一条死锁之后）系统已有若干活动连接 A、B（tcp_pcbs = A->B）。监听端口收到一个 SYN：tcp_pcbs = P1，P1->next 被改成 pending_queue(NULL)，A、B 从 tcp_pcbs 中消失：之后发给 A/B 的段找不到 PCB 被回 RST，重传/TIME_WAIT 定时器不再处理它们，端口冲突检查也看不到它们。同理，第一个已 accept 的连接 P1 在第二个 SYN 到来时被从 tcp_pcbs 中截掉，其后续数据段落到监听 PCB 的 LISTEN 分支（带 ACK）被回 RST，服务端最多只能维持一个连接。
- 修复方向：为 pending/accept 队列使用独立的链接字段（如 pcb->queue_next），不要复用全局链表的 next；或者半连接 PCB 不入 tcp_pcbs，而由 tcp_find_pcb 额外遍历监听 PCB 的 pending 队列。
- 复核意见：pcb_new 在 tcp.cpp:891-892 把新 PCB 以 next 挂到 tcp_pcbs 链表头，LISTEN 分支 624-625 行立即用同一个 next 字段改写为 pcb->pending_queue，于是 tcp_pcbs 头结点之后的原有活动 PCB 全部从链表上脱落。713 行 *pp = pcb->next、718 行 pcb->next = listen->accept_queue、accept() 1030-1031 行 new_pcb->next = NULL 继续改写同一指针。脱落的 PCB 不再被 tcp_find_pcb（373 行）、Tcp::timer（1323 行）、bind/alloc_port 的端口检查看到，报告的后果成立。 更正：严重度降为 high：链表上始终是合法指针，不是内存破坏，而是连接丢失、定时器失效和 PCB 泄漏；并且当前被 606 行的自死锁（net-transport-1）遮蔽，必须先修那条才能走到这里。还有一个附带后果：pcb_free（905-911 行）在 tcp_pcbs 上找不到已脱落的 PCB，摘链变成空操作。

#### V099 [high·已确认·i686,x86_64·已修复] 握手完成时不清理 unacked 队列中的 SYN/SYN-ACK：已建立的连接会被重传 SYN，约 63 秒后被本端判定超时关闭

- **修复**：The ACK completing the handshake (SYN_SENT and SYN_RECEIVED) and the ACK of our FIN (CLOSING, LAST_ACK) call tcp_ack_received, removing the SYN/FIN from the retransmit queue. The timer skips CLOSED PCBs, and CLOSING now arms the TIME_WAIT timer. 验证方式：unit tests test_tcp_client_connection (unacked empty after handshake and after FIN is ACKed) and test_tcp_listen_backlog_and_teardown (accepted child)
- 位置：`src/net/tcp.cpp:671`；相关：`src/net/tcp.cpp:700`、`src/net/tcp.cpp:759`、`src/net/tcp.cpp:832`、`src/net/tcp.cpp:841`、`src/net/tcp.cpp:1335`
- 证据：SYN_SENT 分支：`pcb->snd_una = ack; ... pcb->state = TCP_ESTABLISHED;`，SYN_RECEIVED 分支 700-701 行：`pcb->snd_una = ack; pcb->state = TCP_ESTABLISHED;`，都没有调用 tcp_ack_received()，SYN(/SYN-ACK) 段仍留在 pcb->unacked，timer_retransmit 仍然有效。之后 ESTABLISHED 中只有 `TCP_SEQ_GT(ack, pcb->snd_una)`（即本端又发了新数据并被确认）才会清理。CLOSING/LAST_ACK 分支（832、841 行）对 FIN 同样不清理。
- 触发场景：客户端 connect 后只接收不发送（或服务端 accept 后只接收）：1 秒后 Tcp::timer 重传 SYN|ACK 段（1366 行），指数退避重传 5 次后 1335 行 `pcb->state = TCP_CLOSED`，一个完全正常的连接在约 1+2+4+8+16+32 秒后被本端杀死。并且因为 unacked 非空，759 行把对端的正常数据段当作重复 ACK，三个数据段后触发“快速重传”再发一个 SYN 并使 snd_nxt 多加 1，本端后续发送的数据序列号全部错位，对端无法按序接收。
- 修复方向：在 SYN_SENT/SYN_RECEIVED -> ESTABLISHED、CLOSING/LAST_ACK 的 ACK 处理中统一调用 tcp_ack_received(pcb, ack)；Tcp::timer 对 CLOSED 状态的 PCB 不应再重传。
- 复核意见：connect() 在 tcp.cpp:1018 以 SYN_SENT 状态发 SYN，tcp_send_segment 458-459 行把它放进 unacked 并启动 timer_retransmit。SYN_SENT 分支 666-671 行与 SYN_RECEIVED 分支 700-701 行只写 snd_una 和状态，没有调用 tcp_ack_received，SYN 段和定时器都保留。之后 Tcp::timer 1328-1403 行会以 seg->flags|ACK 重传 SYN，重试到 TCP_MAX_RETRIES=5（tcp.h:43）后在 1335 行把连接置 CLOSED；ESTABLISHED 中 759 行 ack==snd_una && unacked 成立，使每个收到的数据段都计为重复 ACK，第三个触发 213 行再发一个 SYN，并在 439 行让 snd_nxt 多加 1。 更正：只有本端在握手后一直不发送新数据时才会走到超时关闭；一旦本端发数据并被确认，755-758 行的 tcp_ack_received 会把残留的 SYN 段一并清掉。但快速重传那条路径只要在本端首次发送前收到 3 个数据段就会触发。832/841 行对 FIN 同理：LAST_ACK -> CLOSED 后 unacked 里的 FIN 仍会被定时器重传。只影响 i686（x86_64 无网卡）。

#### V100 [high·已确认·i686,x86_64·已修复] Tcp::write 每次最多只发一个 MSS，其余数据滞留在 send_buf 中无人发送；完全没有发送窗口/对端窗口控制

- **修复**：Tcp::write leaves what does not fit the window in send_buf; new tcp_output() sends it as ACKs or window updates arrive, limited by the peer's window (now read from incoming segments), cwnd and MSS. The MSS option is parsed from, and sent in, SYN segments. FIN is deferred until send_buf drains (fin_pending). 验证方式：unit test test_tcp_client_connection (4000-byte write against a 1000-byte window; wire stream checked contiguous and byte-exact)
- 位置：`src/net/tcp.cpp:1061`；相关：`src/net/tcp.cpp:1065`、`src/net/tcp.cpp:754`
- 证据：`uint32_t send_len = copy_len; if (send_len > pcb->mss) send_len = pcb->mss; tcp_send_segment(..., pcb->send_buf, send_len); ... pcb->send_len -= send_len; return copy_len;` —— 返回值声称已接受 copy_len 字节，但只发出前 mss(1460) 字节，剩余部分留在 send_buf；代码中没有任何其它地方（ACK 处理、定时器）会发送 send_buf 的残留。此外 snd_wnd 从不由对端 tcp->window 更新，write 也不检查 snd_wnd/cwnd。
- 触发场景：用户 `send(fd, buf, 4000, 0)` 返回 4000，但线上只发出 1460 字节，其余 2540 字节永远不发（除非应用再次 send，届时又只发出最旧的 min(本次长度, 1460) 字节）。大于一个 MSS 的写入导致对端永远收不全数据、协议卡死；send_buf 逐步被占满后 write 返回 0。
- 修复方向：实现 tcp_output()：在 write 之后以及每次收到新 ACK/窗口更新时循环发送 send_buf 中的数据，受 min(snd_wnd, cwnd) 和 MSS 限制；从输入段解析 window 字段更新 snd_wnd；实现零窗口探测。
- 复核意见：src/net/tcp.cpp:1047-1075 中 Tcp::write 把 copy_len 字节拷入 send_buf，却只调用一次 tcp_send_segment 发送 min(copy_len, mss) 字节，然后返回 copy_len；全文件 grep send_buf/send_len 只有 write 自己引用，ACK 处理和 Tcp::timer 都不会继续发送残留数据。snd_wnd 只在 tcp.cpp:866 初始化为默认值，之后既不从对端 window 字段更新也不在 write 中检查；cwnd 只被增减、从不用于限制发送。socket.cpp:336 的 Socket::send 直接透传 len，没有分片循环，所以大于一个 MSS 的 send 必然滞留数据。 更正：x86_64 上 e1000 在启动时被跳过（kernel.cpp:540），实际只在 i686 上可触发。补充：后续 write 发送的是 send_buf 头部最旧的数据，长度按本次 copy_len 截取，所以残留会一直滞后而不是乱序；修复时除了实现 tcp_output 循环，返回值语义也要与实际入队量一致。

#### V101 [high·已确认·i686,x86_64·已修复] 系统调用路径（read/write/close/connect/select）不加锁地访问 PCB，与 RX 中断和定时器中断竞争：数据丢失与 unacked 链表 use-after-free

- 位置：`src/net/tcp.cpp:1096`；相关：`src/net/tcp.cpp:122`、`src/net/tcp.cpp:1056`、`src/net/tcp.cpp:1083`、`src/net/tcp.cpp:1113`、`src/net/tcp.cpp:1007`、`src/net/socket.cpp:413`
- 证据：Tcp::read：`memcpy(buf, pcb->recv_buf + pcb->recv_read_pos, copy_len); pcb->recv_read_pos += copy_len; if (pcb->recv_read_pos >= pcb->recv_len) { pcb->recv_len = 0; pcb->recv_read_pos = 0; }` 全程不持 tcp_lock、不关中断；而 RX 中断里的 tcp_process_data 会 `pcb->recv_len += copy_len`。Tcp::write -> tcp_send_segment -> tcp_queue_unacked（122-128 行 `while (tail->next) tail = tail->next; tail->next = seg;`）同样无锁，而中断里的 tcp_ack_received 会 kfree 该链表的结点，Tcp::timer 会读取 pcb->unacked。pcb->lock（Mutex）从未被使用。
- 触发场景：(1) 进程在 read 中刚做完 memcpy、尚未执行 `recv_len = 0`，此时 RX 中断追加了新数据并 ACK；返回后 recv_len 被清零，新数据丢失。(2) 进程在 write 中遍历 unacked 找尾结点时被 RX 中断打断，tcp_ack_received 释放了它正指向的段，返回后向已释放内存写 `tail->next = seg`，堆损坏。(3) snd_nxt 的读改写被中断里的 tcp_send_segment(ACK) 穿插。未验证的假设：系统调用执行期间中断是开启的（ping_ioctl/select 在系统调用内轮询 uptime，说明如此）。
- 修复方向：所有访问 PCB 可变状态的 socket 侧入口都要用 tcp_lock（irqsave）或每 PCB 自旋锁保护，拷贝到用户缓冲区之前先在锁内摘取数据；或把 RX 处理推迟到下半部线程并统一用 pcb->lock。
- 复核意见：Tcp::read（tcp.cpp:1078-1106）和 Tcp::write（1037-1076）以及 tcp_queue_unacked（120-127 的尾部遍历）都不持 tcp_lock 也不关中断；tcp_lock 只在 input（564 起）、timer（1320 起）和链表增删处使用，pcb->lock 仅在 887 行 init 后无人使用。i686 的 INT 0x80 是陷阱门（arch/i686/syscall/syscall.cpp:46，0x8F 类型），系统调用期间 IF=1；e1000_irq_handler 直接在中断上下文调用 E1000::receive -> Netdev::receive -> Ethernet::input，最终到 tcp_process_data（352 行 recv_len += copy_len）和 tcp_ack_received（166-168 行 kfree 段）。因此 read 清零 recv_len 丢数据、unacked 链表 UAF 两个窗口都真实存在。 更正：x86_64 上 e1000 被跳过，没有 RX 中断来源，实际只在 i686 上可触发（定时器侧的 Tcp::timer 仍会并发读 unacked）。auditor 标注的“未验证假设”已确认：i686 系统调用门是陷阱门，中断保持开启。
- 被 2 个独立审计者重复报告（net-transport-8, x-concurrency-5）

#### V102 [medium·已确认·i686,x86_64·已修复] tcp_send_segment 在发送失败前已推进 snd_nxt，且失败/入队失败的段不进入重传队列：序列空间出现永久空洞

- **修复**：tcp_send_segment queues the segment for retransmission before sending and advances snd_nxt only once it is queued; a failed transmit stays queued for the timer. Tcp::connect returns to CLOSED if the SYN cannot be queued; tcp_output keeps the data in send_buf if queuing fails. 验证方式：unit test test_tcp_send_failure_is_retransmitted
- 位置：`src/net/tcp.cpp:444`；相关：`src/net/tcp.cpp:459`、`src/net/tcp.cpp:1065`、`src/net/tcp.cpp:1018`
- 证据：438-444 行先 `pcb->snd_nxt++ / += len`，450 行才 `Ip::output`，失败时 `free(buf); return ret;` 不回滚 snd_nxt，也不调用 tcp_queue_unacked；459 行 `tcp_queue_unacked(...)` 的返回值（kmalloc 失败返回 -1）被忽略；Tcp::write 1065 行忽略 tcp_send_segment 的返回值并照样从 send_buf 中移除数据。
- 触发场景：Ip::output 返回 -1（设备 down、ARP 解析失败/排队失败、驱动 TX 描述符超时）或堆紧张时：数据段没有发出也不在 unacked 队列里，但 snd_nxt 已前进且数据已从 send_buf 删除。之后的段带着更高的序列号，对端永远等待缺失的字节，连接永久卡死且本端不会重传。connect() 的 SYN 发送失败时同样：状态停在 SYN_SENT 且没有任何重传。
- 修复方向：先把段放入重传队列（失败则整体返回错误、不推进 snd_nxt），再尝试发送；发送失败时保留在队列中由重传定时器重试。write 根据结果决定是否消费 send_buf。
- 复核意见：src/net/tcp.cpp:438-444 在 450 行 Ip::output 之前就推进 snd_nxt，451-454 行失败分支只 free(buf) 并返回，既不回滚 snd_nxt 也不入 unacked 队列；459 行忽略 tcp_queue_unacked 的 -1 返回（tcp.cpp:93/108 kmalloc 失败）。Tcp::write 1065 行忽略返回值，1069-1073 行无条件把数据从 send_buf 移除。Ip::output 确实可能返回 -1（netdev.cpp:225 设备 down、e1000.cpp:401 TX 描述符超时、ip.cpp:597 ARP 排队失败），因此序列空洞是真实的，但只发生在错误路径上。 更正：严重度降为 medium：只在发送失败/堆耗尽等错误路径触发。x86_64 上 e1000 被跳过（kernel.cpp:538-540），实际只影响 i686。首包 ARP 未解析时走 queue_packet 返回 0，不属于失败路径。

#### V103 [medium·已确认·i686,x86_64·已修复] 半连接 PCB 永不回收且 accept 队列无上限：少量 SYN 即可永久耗尽 backlog，或耗尽内核堆

- **修复**：Half-open PCBs are unlinked and freed on RST or when SYN+ACK retransmission runs out, returning the backlog slot; backlog now counts pending plus accept-queue connections (new accept_count). 验证方式：unit test test_tcp_listen_backlog_and_teardown (RST path and backlog); the retransmit-exhaustion path is untested
- 位置：`src/net/tcp.cpp:601`；相关：`src/net/tcp.cpp:695`、`src/net/tcp.cpp:714`、`src/net/tcp.cpp:1335`
- 证据：`if (pcb->pending_count >= pcb->backlog) break;` 只限制 pending_count；握手完成时 714 行 `listen->pending_count--` 后放入 accept_queue，accept_queue 长度不受任何限制。SYN_RECEIVED 的 PCB 收到 RST（695 行）或 SYN-ACK 重传 5 次超时（1335 行）时只是 `pcb->state = TCP_CLOSED`，既不从 pending_queue 摘除、不减 pending_count，也不释放（每个 PCB 含 2×8192 字节缓冲）。
- 触发场景：(1) 对监听端口发 backlog 个（默认 5）不完成握手的 SYN：pending_count 永久等于 backlog，监听 socket 此后拒绝所有新连接，且每个半连接泄漏约 16KB。(2) 客户端反复完成握手而应用不 accept：accept_queue 无限增长，每个连接 16KB+，耗尽内核堆。
- 修复方向：半连接超时/RST 时从 pending_queue 摘除、递减 pending_count 并释放 PCB；backlog 同时约束 pending + accept 队列的总长度；为 SYN_RECEIVED 设置独立的超时。
- 复核意见：tcp.cpp:601 只用 pending_count 限制；714 行握手完成后 pending_count-- 并在 718-719 行挂入 accept_queue，accept_queue 没有任何长度检查。695 行（RST）和 1335 行（重传超限）只把状态置为 TCP_CLOSED，不从 pending_queue 摘除、不减 pending_count，也没有任何代码回收 CLOSED 的 PCB（pcb_free 只被 socket.cpp:248/255/494 调用）。缺陷真实存在，但目前被 606 行的自死锁（net-transport-1）完全遮蔽：第一个 SYN 就挂死，根本走不到半连接状态。 更正：严重度降为 medium：当前不可达，修复 net-transport-1 之后才会暴露。另外由于 next 指针复用（net-transport-2），pending_queue 本身也会被破坏，三者需要一起修。

#### V104 [medium·待确认·i686,x86_64·已修复] 释放监听 PCB 时不处理 pending/accept 队列：子 PCB 泄漏，半连接 PCB 的 listen_pcb 成为悬空指针并在收到 ACK 时被写入

- **修复**：Tcp::pcb_free on a listening PCB unlinks, resets (RST) and frees every pending and accept-queue child instead of leaving listen_pcb dangling. 验证方式：unit test test_tcp_listen_backlog_and_teardown
- 位置：`src/net/tcp.cpp:897`；相关：`src/net/tcp.cpp:705`、`src/net/tcp.cpp:1114`
- 证据：pcb_free 只把 pcb 自己从 tcp_pcbs/tcp_listen_pcbs 摘除并 kfree，不遍历 `pcb->pending_queue` / `pcb->accept_queue`。SYN_RECEIVED 分支 705-719 行：`tcp_pcb_t *listen = pcb->listen_pcb; tcp_pcb_t **pp = &listen->pending_queue; ... listen->pending_count--; pcb->next = listen->accept_queue; listen->accept_queue = pcb;` 直接解引用并写 listen。
- 触发场景：服务端收到 SYN 生成半连接 P1（listen_pcb 指向监听 PCB L）后关闭监听 socket，L 被 kfree；随后客户端的第三次握手 ACK 到达，P1 仍可在 tcp_pcbs 中被找到，代码读写已释放的 L（`listen->accept_queue = pcb` 写入已释放堆块 -> 堆损坏），P1 被挂到一个永远无人 accept 的队列上泄漏。已完成握手但未 accept 的连接同样随监听 socket 关闭而全部泄漏且不发 RST。假设：监听 socket 的关闭路径可达（当前 closesocket 无系统调用入口，接通后即触发）。
- 修复方向：pcb_free/close 监听 PCB 时在 tcp_lock 内遍历 pending_queue 和 accept_queue，对每个子 PCB 发 RST 并释放（或至少清空其 listen_pcb）。
- 复核意见：pcb_free（tcp.cpp:897-934）确实只把自身从 tcp_pcbs/tcp_listen_pcbs 摘除后 kfree，不遍历 pending_queue/accept_queue；SYN_RECEIVED 分支 705-719 行确实直接解引用并写 pcb->listen_pcb。但目前有两层不可达：Socket::closesocket（socket.cpp:485）在整个 src 下没有任何调用者（syscall.cpp 349-474 行没有对应 wrapper），监听 PCB 实际上永远不会被释放；并且 606 行的自死锁使半连接 PCB 根本无法产生。代码缺陷属实，但释放后写入的场景现在走不到。 更正：属于潜伏缺陷，严重度降为 medium：需要同时接通 closesocket 的系统调用入口并修复 net-transport-1 之后才会变成真实的释放后写入。当前更现实的问题反而是 socket 永远无法关闭（socket_t 和 PCB 随进程退出泄漏）。

#### V105 [medium·已确认·i686,x86_64·已修复] UDP 接收队列没有长度上限，远端可无限堆积数据报耗尽内核堆

- **修复**：UDP receive queue capped at UDP_RECV_QUEUE_MAX (64) datagrams per PCB, new arrivals dropped; a tail pointer makes enqueue O(1). 验证方式：unit test test_udp_recv_queue_bounded
- 位置：`src/net/udp.cpp:158`；相关：`src/net/udp.cpp:148`
- 证据：`tail->next = buf; ... pcb->recv_queue_len++;` —— recv_queue_len 只被递增/递减，从不与任何上限比较；入队还要在关中断持 udp_lock 的情况下 O(n) 遍历到队尾。
- 触发场景：任何绑定了 UDP 端口但读取慢或不读取的 socket（包括忘记关闭的 socket，见 closesocket 不可达问题）：对端持续发包，每个包占用一个 netbuf（最多约 2KB）永久留在队列中，内核堆被耗尽后全系统 kmalloc 失败；队列很长时每个 RX 中断的遍历时间也线性增长。
- 修复方向：为每个 UDP PCB 设置接收队列上限（包数和/或字节数），超限丢弃新包；保存队尾指针使入队 O(1)。
- 复核意见：src/net/udp.cpp:147-158 在无 recv_callback 时把 netbuf 挂到 recv_queue 尾部并 recv_queue_len++，全仓库 grep recv_queue_len 只有 158 行递增和 390 行递减，没有任何上限比较。Socket 层的 UDP 收包走 Udp::recv_poll（socket.cpp:428/460），不设置回调，所以用户 socket 一定走这条队列路径；Netbuf::alloc 用 kmalloc 分配头和数据区（netbuf.cpp:16-21），堆积直接消耗内核堆。入队是在 lock_irqsave 下 O(n) 遍历到尾部，也属实。 更正：严重度降为 medium：需要对端向一个已绑定但不读取的端口持续灌包才会耗尽堆，属于远端 DoS/边缘场景而非常规路径；且 x86_64 上 e1000 被跳过，实际只影响 i686。PCB 释放时队列会被清空（udp.cpp:257-262），所以不是永久泄漏而是无界增长。


### 设备驱动（15 项）

#### V106 [high·已确认·arm64·已修复] 控制台读阻塞通过 PL011 关中断忙等实现，用户进程 read(stdin) 期间整个系统冻结

- **修复**：src/drivers/arm/serial.cpp: receive now goes through a 256-byte ring filled by a PL011 RX interrupt handler (IRQ 33, registered on first blocking read). Serial::getchar no longer spins with interrupts masked: between checks it yields to other ready tasks, sleeps in WFI until an interrupt is pending, then briefly unmasks interrupts so the timer tick and RX interrupt are taken. has_char/getchar_nonblock/serial_try_getchar read from the ring; getchar_nonblock no longer returns a negative value for bytes >= 0x80. Added Serial::rx_inject. SVC entry was NOT changed (arch code, outside ownership): the reader stays runnable instead of blocking, because Scheduler::block from an arm64 syscall hangs (see notes). 验证方式：Unit tests test_serial_rx_buffer_order and test_serial_rx_buffer_overflow (arm64 Serial Tests). Boot regression: the arm64 exec check types a command into the user shell through this path and prints 1.
- 位置：`src/drivers/arm/serial.cpp:269`；相关：`src/fs/devfs.cpp:149`、`src/drivers/arm/serial.cpp:375`、`src/include/drivers/arm/serial.h:30`
- 证据：`char drivers::Serial::getchar() { while (pl011_read(PL011_FR) & PL011_FR_RXFE) { __asm__ volatile("nop"); } ...`：纯轮询，既不 yield 也不睡眠。devconsole_read (src/fs/devfs.cpp:149) 在系统调用上下文调用它。arm64 从 EL0 进入 SVC 时 PSTATE.I 被置位，vectors.S / exception.cpp / src/arch/arm64/syscall/*.cpp 中没有任何 daifclr 或 hal::Interrupt::enable()（全树只有 kernel.cpp:319 和 context_asm.S:443 开中断）。PL011 的 RX 中断 (enable_rx_interrupt, PL011_IRQ 33) 从未被启用或注册。对比 x86 的 Keyboard::getchar() 在循环中调用 Scheduler::yield()。
- 触发场景：用户态 shell 调用 read(0, buf, n) 等待输入：CPU 在 EL1 关中断自旋，定时器 PPI 30 一直 pending 无法投递，g_timer_ticks/timer_ticks 停止增长，Scheduler::timer_tick 不运行，睡眠任务不被唤醒，其他就绪任务（后台进程、内核线程）完全得不到 CPU，直到用户敲键。按键之间丢失的 tick 不会补回（每次只触发一次中断），基于 tick 的计时变慢。
- 修复方向：实现中断驱动的 RX：注册 PL011_IRQ 处理函数，把字符放入环形缓冲区并唤醒等待任务；getchar() 在缓冲区空时阻塞/让出 CPU（至少像 x86 那样调用 kernel::Scheduler::yield()），并保证系统调用期间可被抢占。
- 复核意见：src/drivers/arm/serial.cpp:267-275 的 Serial::getchar 是纯 nop 轮询，src/fs/devfs.cpp:145-152 的 devconsole_read 在 ARCH_ARM64 分支下对首字符直接调用它。全树 daifclr 只出现在 hal.cpp:138（Interrupt::enable，arm64 系统调用路径无调用者）、interrupt.h:58 和 context_asm.S:443（内核线程入口），svc.S 在 `bl syscall_dispatcher` 前没有开中断，返回前（svc.S:167）还再次 daifset，所以 SVC 期间 IRQ 始终屏蔽。enable_rx_interrupt/PL011_IRQ 除定义外无任何引用，因此 shell 等待输入时定时器 tick、睡眠唤醒和其它任务调度全部停止。 更正：行号应为 serial.cpp:267-270（函数起始 267）；devfs.cpp 的调用点在 152 行。仅修 getchar 让出 CPU 不够，还需在 SVC 入口开 IRQ，否则 yield 之后其它系统调用里的等待依然无 tick。
- 被 2 个独立审计者重复报告（drivers-arm-platform-4, x-concurrency-6）

#### V107 [high·已确认·i686·已修复] RX 在硬中断上下文中同步跑完整协议栈，并重入使用 sync::Mutex 保护的发送路径（递归重入破坏 TX 环 / 在中断里阻塞调度）

- 位置：`src/drivers/x86/e1000.cpp:390`；相关：`src/drivers/x86/e1000.cpp:523`、`src/drivers/x86/e1000.cpp:491`、`src/net/netdev.cpp:268`、`src/kernel/sync/mutex.cpp:65`
- 证据：`e1000_irq_handler` -> `drivers::E1000::receive` -> `net::Netdev::receive` -> `net::Ethernet::input`（同步）-> 例如 `net::Arp` 应答 (src/net/arp.cpp:279) -> `net::Ethernet::output` -> `net::Netdev::transmit` -> `e1000_netdev_transmit`，其中 `sync::MutexGuard guard(e1000_mutex);`。`Mutex::lock()` (src/kernel/sync/mutex.cpp) 以被中断任务的 pid 作为所有者：若 `owner_pid_ == current->pid` 则直接递归进入；否则把 `current->state = TASK_BLOCKED` 并调用 `Scheduler::schedule()`。而 irq_handler (src/arch/i686/interrupt/irq.cpp:119-128) 在 handler 返回后才发 EOI。
- 触发场景：情形A：任务 T 正在 e1000_netdev_transmit 中（已取 cur=dev->tx_cur，正在 memcpy 到 tx_buffers[cur]），此时网卡 RX 中断到来，中断里需要回 ARP/ICMP/TCP ACK，Mutex 因为 owner 是同一个 pid 而递归放行，中断路径使用同一个 cur 覆盖缓冲区、推进 tx_cur 并写 TDT；返回后 T 继续向已交给硬件的缓冲区 memcpy 并再次写同一描述符，结果是一个包丢失、另一个包内容混杂。情形B：T 持有 e1000_mutex 时被时钟抢占，任务 U 运行时发生 RX 中断，中断处理中 Mutex::lock 把 U 置为 BLOCKED 并 schedule()，此时该 IRQ 尚未 EOI，PIC 上 IRQ11 及更低优先级（鼠标、ATA 等）中断全部被挂起，直到 U 被唤醒；如果被中断的是 idle 任务则 idle 被阻塞。
- 修复方向：IRQ 处理中只把收到的 Netbuf 放入队列，由内核线程/软中断在进程上下文调用 net::Netdev::receive；发送路径和 RX 环访问改用 sync::SpinlockIrqGuard（关中断自旋锁）保护 tx_cur/描述符，而不是可睡眠且按 pid 递归的 Mutex。
- 复核意见：src/drivers/x86/e1000.cpp:519-534 的 e1000_irq_handler 直接调用 E1000::receive，后者在 :491 调 net::Netdev::receive，netdev.cpp:268 同步进入 Ethernet::input（ethernet.cpp:71/75 → Arp::input/Ip::input），arp.cpp:150→279 在同一上下文里 Ethernet::output → Netdev::transmit（netdev.cpp:237）→ e1000_netdev_transmit，其中 :390 取 sync::MutexGuard。mutex.cpp:65-70 按被中断任务的 pid 判定递归直接放行，mutex.cpp:74-79 否则把当前任务置 BLOCKED 并 schedule()；而 irq.cpp:119-128 在 handler 返回后才发 EOI。Mutex::lock 返回时恢复中断，所以任务在 memcpy/写描述符期间可被 RX 中断重入，两种情形的链路都已逐段核实。 更正：情形 A 的时间窗口较窄（发送途中恰好收到需要应答的包），情形 B 需要持锁任务被抢占；属于真实竞态但非每次必现。另外 Netbuf::alloc 在中断中走堆的 SpinlockIrqGuard，本身是安全的，问题集中在 Mutex 的使用上。

#### V108 [high·已确认·i686·已修复] e1000 发送路径用阻塞 Mutex 保护，但会在中断上下文被调用；按 pid 判定的递归锁让 IRQ 直接重入正在发送的线程，破坏 TX 环并永久卡死发送

- 位置：`src/drivers/x86/e1000.cpp:390`；相关：`src/kernel/sync/mutex.cpp:65`、`src/drivers/x86/e1000.cpp:520`、`src/net/netdev.cpp:236`、`src/net/tcp.cpp:1375`、`src/net/dhcp.cpp:432`
- 证据：`sync::MutexGuard guard(e1000_mutex);` 位于 e1000_netdev_transmit。该函数既在线程上下文（系统调用，i686 陷阱门 IF=1）被调用，也在 IRQ 上下文被调用：e1000_irq_handler -> E1000::receive -> Netdev::receive -> Ethernet::input -> ARP reply / ICMP echo reply / TCP ACK -> Netdev::transmit；以及定时器 IRQ -> net::Tcp::timer 重传。Mutex::lock 中 `if (owner_pid_ == current->pid) { recursion_++; return; }`，而 IRQ 中的 current 就是被打断的线程，于是 IRQ「递归」获得该锁。
- 触发场景：线程 T 在 transmit 中已读 `cur = dev->tx_cur`（=5）并正在 memcpy 到 tx_buffers[5]；此时 RX 中断到来并需要回 ARP/ACK：IRQ 内 Mutex::lock 因 owner==current 直接成功，同样使用 cur=5，填 desc[5]、tx_cur=6、写 TDT=6 并发出。返回后 T 继续覆盖 tx_buffers[5]，把 desc[5].status 清 0，再次写 tx_cur=6/TDT=6（尾指针未变化，网卡不会再处理 desc[5]）。T 的包丢失，且 desc[5] 的 DD 位永远不会再被置位；环绕回到槽 5 时 `while (!(desc->status & E1000_TXD_STAT_DD) && timeout-- > 0)` 必然超时返回 -1 且 tx_cur 不前进，此后所有发送永久失败。
- 修复方向：发送路径改用 lock_irqsave 自旋锁（SpinlockIrqGuard）保护 tx_cur/描述符；或把 RX 处理下放到内核线程（中断只做 ack 并唤醒），使协议栈只在线程上下文运行。Mutex 应在 in_interrupt() 时断言失败，递归判定不能在中断上下文生效。
- 复核意见：src/drivers/x86/e1000.cpp:390 用 sync::MutexGuard 保护发送，而 e1000_irq_handler(:509) -> E1000::receive -> Netdev::receive (netdev.cpp:268) 在中断里同步调用 Ethernet::input，ARP reply (arp.cpp:150) 等回包会再次进入 transmit。mutex.cpp:64 的递归判定只比较 owner_pid_ 与 current->pid，中断里 current 就是被打断的持锁线程，所以直接放行；Mutex::lock 返回前恢复了中断状态，i686 系统调用走陷阱门 (syscall.cpp:46, IF=1)，且 Makefile:88 为 -O0，memcpy 期间窗口不小。两次都用同一个 cur，第二次 `desc->status = 0` 后 TDT 值不变，该槽 DD 位不再置位，32 个描述符绕回后 :398 的等待必然超时且 tx_cur 不前进，发送永久失败。 更正：只在 i686 可达（x86_64 上 E1000::init 被 kernel.cpp:538 跳过，arm64 无网络栈）。永久卡死这一环节依赖网卡在 TDT 未变化时不再处理该描述符，属于静态推断，未在 QEMU 上复现。修复建议可行：TX 环改用关中断自旋锁，并在 Mutex::lock 中对中断上下文断言。
- 被 2 个独立审计者重复报告（x-concurrency-4, x-cpp-migration-1）

#### V109 [high·已确认·x86_64·已修复] e1000/UHCI 等驱动把内核虚拟地址和 MMIO 映射地址存进 uint32_t，x86_64 上无法使用，导致 x86_64 完全没有网卡和 USB

- **修复**：驱动侧不再截断地址（e1000 的 MMIO 基址、描述符环物理地址和 64 位 BAR；UHCI 不再把指针转成 uint32_t）。x86_64 启动时不再跳过 e1000 和 USB 的初始化（`6b59f72`）。验证方式：x86_64 上 DHCP 获得地址、ping 3/3、TCP 双向字节流实测通过；USB 存储设备枚举成功。
- 位置：`src/drivers/x86/e1000.cpp:579`；相关：`src/drivers/x86/e1000.cpp:135`、`src/drivers/x86/e1000.cpp:150`、`src/drivers/x86/e1000.cpp:188`、`src/drivers/x86/usb/uhci.cpp:69`、`src/drivers/x86/usb/uhci.cpp:117`、`src/drivers/x86/usb/uhci.cpp:494`
- 证据：`uint32_t mmio_virt = mm::Vmm::map_mmio(bar0, dev->mmio_size); ... dev->mmio_base = (volatile uint32_t *)mmio_virt;`（map_mmio 在 x86_64 返回 0xFFFF8000_4xxxxxxx）；`dev->rx_descs_phys = mm::Vmm::virt_to_phys((uint32_t)(uintptr_t)dev->rx_descs);`（e1000.cpp:135,150,188,203）；uhci.cpp:69/93/117/174/494 同样 `(uint32_t)(uintptr_t)ptr`。kernel.cpp:538-544、590-616 因此在 x86_64 上直接跳过 E1000::init 和整个 USB 子系统（注释：`x86_64 VMM MMIO not ready`），但 map_mmio 本身已支持 64 位，真正的问题是驱动里的截断。
- 触发场景：x86_64 用 `make run-disk`（带 -device e1000）启动：网卡不初始化，netdev 列表为空，socket/DHCP/DNS/ping 等全部网络功能不可用；若去掉 kernel.cpp 的跳过，mmio_base 被截断为低 32 位用户地址，第一次读写寄存器就缺页，virt_to_phys(截断地址) 返回 0 使描述符环初始化失败。
- 修复方向：驱动中虚拟地址用 uintptr_t/指针，物理地址用 paddr_t（并向设备写入高 32 位寄存器 RDBAH/TDBAH）；DMA 缓冲从 4GB 以下分配；改完后去掉 kernel.cpp 中 x86_64 的跳过分支。
- 复核意见：e1000.cpp:579 把 map_mmio 的返回值存入 uint32_t，而 src/include/mm/vmm.h:266 声明其返回 uintptr_t；e1000.cpp:135/150/188/203 和 uhci.cpp:69/93/117/174/494/654 都把指针强转为 uint32_t 再传给 virt_to_phys（vmm.h:303 参数为 uintptr_t）。kernel.cpp:538-541 与 :590-592 在 ARCH_X86_64 下直接跳过 E1000 和 USB 初始化，与实测启动日志 "x86_64 VMM MMIO not ready" 一致，x86_64 上确实没有网卡和 USB。 更正：e1000.cpp:574 的 bar0 也是 uint32_t，64 位 BAR 同样会被截断。kernel.cpp 的注释把原因归为 VMM MMIO 未就绪，实际 map_mmio 接口已是 64 位，阻塞点在驱动的截断；去掉跳过分支前需先修驱动。

#### V110 [high·待确认·i686·已修复] DMA 缓冲区/TD 池假定堆内存物理连续，跨页时把数据写到错误的物理页

- **修复**：src/drivers/x86/usb/uhci.cpp: frame list and TD/QH pools are allocated with drivers::Dma. Control and bulk transfers go through a contiguous DMA bounce buffer (OUT copied in before submit, IN copied back after completion), since callers pass heap and stack buffers. Also: zero-length bulk transfers and max_packet_size == 0 are rejected instead of dereferencing a null TD or looping forever, and a timed-out transfer waits 2 ms after being unlinked before its TDs and buffer are reused. 验证方式：Untested at runtime: the test boot has no UHCI controller (QEMU needs -usb). Compiles on i686 and x86_64; the Dma allocator itself is unit tested.
- 位置：`src/drivers/x86/usb/uhci.cpp:654`；相关：`src/drivers/x86/usb/uhci.cpp:499`、`src/drivers/x86/usb/uhci.cpp:74`、`src/drivers/x86/usb/uhci.cpp:117`、`src/drivers/x86/usb/uhci.cpp:98`、`src/drivers/x86/usb/uhci.cpp:143`、`src/drivers/x86/usb/uhci.cpp:494`
- 证据：`uint32_t data_phys = mm::Vmm::virt_to_phys(...urb->buffer);` 之后每个 TD 用 `data_phys + offset`；TD 池同理 `td->phys_addr = hc->td_pool_phys + i * sizeof(uhci_td_t)`（8KB，跨 2~3 页）。i686 的 `expand()`（src/mm/heap.cpp:99）是逐页 `mm::Pmm::alloc_frame()`（位图 first-free）再映射，物理页并不保证相邻；内核栈上的 CBW/CSW/描述符缓冲同样只转换首地址。virt_to_phys 失败返回 0 也未检查。
- 触发场景：运行一段时间后（用户进程退出释放了低位物理帧），堆扩展得到不相邻的帧。FAT32 kmalloc 一个簇缓冲（非页对齐，必然跨页）并从 U 盘读取：跨页之后的 TD 的 buffer 指向首页物理地址+offset，即另一块无关物理页，控制器 DMA 覆盖该页（任意内核/用户内存损坏），而调用者缓冲区后半段是垃圾；写操作则把无关内存写入磁盘。未验证点：启动早期位图通常连续分配，所以 TD 池本身多半侥幸连续。
- 修复方向：TD/QH 池、帧列表用 `mm::Pmm::alloc_frames()` 分配物理连续页；数据传输按页拆分，每个 TD（≤64 字节且不跨页）单独做 virt_to_phys，或统一使用物理连续的 bounce buffer；检查 virt_to_phys 返回 0。
- 复核意见：uhci.cpp:654 只对 urb->buffer 首地址做 virt_to_phys，之后每个 TD 用 data_phys + offset；TD 池 (:69-74, :117) 为 256 个 TD 的 kmalloc_aligned 区域，同样只转换首地址。i686 的堆扩展 (src/mm/heap.cpp:98-99) 逐页调用 Pmm::alloc_frame 再映射，不保证物理连续；fat32.cpp:282 等处 kmalloc 的簇缓冲非页对齐。代码缺陷静态上成立，但实际出现不相邻帧取决于运行期物理内存碎片，未能静态确认。 更正：同一根因也存在于 e1000：e1000.cpp:127/143/180/196 用 kmalloc_aligned(…,16) 分配描述符环和 2048 字节收发缓冲，只转换首地址 (:135/150/188/203)，跨页时同样出错，且 e1000 是默认 QEMU 配置的一部分，影响面比 USB 更大。USB 路径需要用 -usb 之类参数显式加 UHCI 控制器，Makefile:452 的默认运行命令没有。

#### V111 [high·已确认·i686·已修复] UHCI 热插拔轮询在定时器中断上下文中调用 Timer::wait 忙等 tick，插入 USB 设备即死循环

- 位置：`src/drivers/x86/usb/uhci.cpp:1047`；相关：`src/drivers/x86/usb/uhci.cpp:1139`、`src/drivers/x86/timer.cpp:54`、`src/drivers/x86/timer.cpp:162`、`src/drivers/x86/usb/usb.cpp:40`
- 证据：start_hotplug_monitor 用 drivers::Timer::register_callback 注册 uhci_hotplug_timer_callback；该回调由 timer.cpp 的 timer_callback（IRQ0 处理函数，中断门，IF=0，尚未发 EOI）直接调用 → poll_port_changes → check_port_changes，检测到新连接时执行 `drivers::Timer::wait(100);`，之后 handle_port_connect 的枚举过程还会多次 usb_delay/Timer::wait。Timer::wait 的实现是 `while (drivers::Timer::get_uptime_ms() < target) pause;`，而 timer_ticks 只在这个 IRQ0 处理函数里递增。
- 触发场景：系统运行中插入 U 盘（或 QEMU 中 device_add usb-storage）：500ms 轮询回调发现 CSC+CCS，在 IRQ0 处理函数内部调用 Timer::wait(100)；此时中断关闭，timer_ticks 不再前进，循环条件永远为真，整个系统在中断上下文中死锁，没有任何输出。
- 修复方向：定时器回调中只置一个标志或唤醒一个内核线程（工作队列），端口去抖和设备枚举放到线程上下文执行；并给 Timer::wait 加上在关中断/中断上下文调用的检测。
- 复核意见：src/drivers/x86/usb/uhci.cpp:1139 把 uhci_hotplug_timer_callback 注册为定时器回调，src/drivers/x86/timer.cpp:38-55 的 timer_callback（IRQ0 处理函数，idt.h:42 为 0x0E 中断门，IF=0，且 irq.cpp:121-128 在 handler 返回后才发 EOI）直接调用它。回调链 poll_port_changes -> uhci.cpp:1047 `drivers::Timer::wait(100)`，而 timer.cpp:161-164 的 wait 忙等 timer_ticks，timer_ticks 只在该 IRQ0 处理函数里递增，因此永不返回。kernel.cpp:618-621 在非 x86_64 上启动该监控，i686 上 USB 确实初始化，路径可达。 更正：触发条件是 i686 上存在 UHCI 控制器（uhci_controller_count>0，例如 QEMU 加 -usb）且运行期出现新连接（CSC+CCS 且 port_device 为空）；没有 UHCI 控制器时 start_hotplug_monitor 直接返回。x86_64 上 USB 被整体跳过，不受影响。后续 handle_port_connect 中的 usb_delay_ms（usb.cpp:391/461/501）是同一问题，但第一个 wait 就已挂死。

#### V112 [high·已确认·i686·已修复] 热插拔检测在中断上下文中执行 Timer::wait 忙等和整套枚举，插入设备即永久挂死内核

- **修复**：The timer-callback half was already fixed by 8e5a0b5. The remaining half is fixed here: uhci_irq_handler no longer calls check_port_changes (which busy-waits 100 ms and enumerates) in interrupt context; on resume-detect it raises the existing hot-plug deferred work, so debounce, enumeration and disconnect run only on kworker. 验证方式：Untested at runtime (no UHCI controller in the test boot). Compile only.
- 位置：`src/drivers/x86/usb/uhci.cpp:1047`；相关：`src/drivers/x86/usb/uhci.cpp:823`、`src/drivers/x86/usb/uhci.cpp:1121`、`src/drivers/x86/usb/uhci.cpp:1057`
- 证据：`uhci_hotplug_timer_callback()` 由 `drivers/x86/timer.cpp` 的 `timer_callback()`（IRQ0 处理函数，EOI 之前）直接调用，`uhci_irq_handler()` 第 823 行也调用 `check_port_changes(hc)`。而 check_port_changes 内 `drivers::Timer::wait(100);` 接着 `drivers::Usb::handle_port_connect()` -> enumerate_device（reset_port 的 wait(60)、控制传输轮询 wait(1) ……）。Timer::wait 是 `while (get_uptime_ms() < target) pause;`，依赖 IRQ0 递增 timer_ticks；在 IRQ0 处理函数内部（未发 EOI、IF=0）tick 永远不会前进。
- 触发场景：系统启动后在 QEMU monitor 执行 device_add usb-storage（或真机插入 U 盘）：端口 CSC 置位 -> 500ms 后定时器回调在 IRQ0 上下文进入 check_port_changes -> Timer::wait(100) 死循环，整个内核永久挂死。拔出路径同样在中断上下文调用 disconnect_device -> fs::Blockdev::unregister_device 获取 sync::Mutex（可睡眠锁）并 kfree。
- 修复方向：中断/定时器回调里只记录“端口有变化”标志并唤醒一个内核线程（或工作队列），由线程上下文执行去抖、枚举和断开；IRQ 处理函数中不要调用 check_port_changes。
- 复核意见：uhci.cpp:1121 的 uhci_hotplug_timer_callback 通过 Timer::register_callback 注册，由 timer.cpp:38-57 的 timer_callback 在 IRQ0 处理函数内直接调用；i686 的 IRQ 走中断门 (idt.h:42, 0x0E)，IF=0，且 irq.cpp:128 的 EOI 在 handler 返回之后才发。check_port_changes 在 uhci.cpp:1047 调用 Timer::wait(100)，而 timer.cpp:160-164 的 wait 只是循环比较 get_uptime_ms()，后者 (:137-141) 完全依赖 timer_ticks，在本次 IRQ0 内不会再增长，循环永不退出。uhci.cpp:823 在 uhci_irq_handler 里也调用 check_port_changes，同样在 IF=0 下死等。 更正：只影响带 UHCI 控制器的 i686（start_hotplug_monitor 在 uhci_controller_count==0 时直接返回，x86_64 被 kernel.cpp:616 排除），且要运行期插入设备才触发，Makefile:452 的默认运行命令不含 USB，所以评为 high 而非 critical。
- 被 2 个独立审计者重复报告（drivers-usb-1, x-concurrency-2）

#### V113 [high·已确认·i686,x86_64·已修复] VGA ANSI 转义参数整数溢出可使光标变为负数/超大值，导致向 VGA 缓冲区之外的内核内存写入

- **修复**：src/drivers/x86/vga.cpp: ANSI parameters saturate at 9999 instead of wrapping; cursor-move counts are clamped to [1, 80]; vga_update_cursor clamps row/col onto the screen; vga_putentry_at refuses to write outside the 80x25 buffer. Added Vga::get_cursor. The same unbounded parameter accumulation existed in the x86 and arm framebuffer terminals (a huge count overflowed the row/column addition into a negative position); both now saturate too. 验证方式：Unit tests test_vga_ansi_param_wraparound, test_vga_ansi_cursor_moves_clamped, test_fb_terminal_ansi_param_overflow (suite vga_ansi_tests, run on i686 and x86_64). The arm framebuffer parser change is untested (no framebuffer on arm64 in the test boot).
- 位置：`src/drivers/x86/vga.cpp:258`；相关：`src/drivers/x86/vga.cpp:303`、`src/drivers/x86/vga.cpp:312`、`src/drivers/x86/vga.cpp:321`、`src/drivers/x86/vga.cpp:330`、`src/drivers/x86/vga.cpp:141`、`src/fs/devfs.cpp:207`
- 证据：`ansi_params[ansi_param_count - 1] = ansi_params[ansi_param_count - 1] * 10 + (c - '0');` 对 int 无上限累加，-O0 下回绕为负数。随后 'A'/'B'/'C'/'D' 分支只按正数假设做钳位：`vga_row = (vga_row >= n) ? (vga_row - n) : 0;`(303 行，n 为负时 row 变成 row+|n|)、`vga_row = (vga_row + n < VGA_HEIGHT) ? (vga_row + n) : ...`(312 行，n 为负得到负行号)，321/330 行对列同理。之后 `vga_putentry_at` 直接 `vga[y * VGA_WIDTH + x] = ...`，没有任何边界检查。VGA_ADDRESS 位于内核直映区 (KERNEL_VIRTUAL_BASE+0xB8000)，其后 0x48000 字节就是内核镜像 (物理 1MB)。
- 触发场景：文本模式下 (grub 菜单 gfxpayload=text 或 Framebuffer::init 失败) 用户进程 write(1, "\033[4294965453AX", ...)：devconsole_write -> Vga::putchar。参数回绕为 -1843，'A' 使 vga_row += 1843，随后字符 'X' 被写到 0xB8000+1843*160 ≈ 物理 0x100000 处，即内核镜像开头；换一个数值可写直映区内任意偏移 (每次 2 字节，字符+属性可控)。用 'B'/'D' 负值还可写到 0xB8000 之下的低端内存。用户态即可破坏内核内存。
- 修复方向：解析参数时做饱和 (例如超过 9999 就不再累加)，并在 A/B/C/D 中把 n 钳到 [1, VGA_HEIGHT/VGA_WIDTH]；在 vga_putentry_at 和 vga_update_cursor 中再加一道 0<=x<80、0<=y<25 的断言/钳位。
- 复核意见：src/drivers/x86/vga.cpp:258 对 int 参数无上限累加（Makefile:88 为 -O0，实际回绕），303/312/321/330 行的钳位都只假设 n 为正，vga_row/vga_col 是 int（17-18 行），n 为负时 `vga_row >= n` 恒真得到 vga_row - n 的超大行号。正常字符路径（360-366 行）只检查 vga_col >= VGA_WIDTH，不检查行号，vga_putentry_at（138-142 行）无边界检查直接写 vga[y*80+x]。用户态 write(1) 经 devfs.cpp:195-210 在 Framebuffer 未初始化时走 Vga::putchar，可达。 更正：前提是处于 VGA 文本模式：grub.cfg 默认 gfxpayload=keep（图形帧缓冲），只有选择 gfxpayload=text 的菜单项（grub.cfg:56）或 Framebuffer 初始化失败时才会走 Vga::putchar，所以默认启动配置下不触发，评级保持 high 而非 critical。写入后若再输出换行，vga_newline 会把 vga_row 拉回 24 并滚屏，攻击者需在换行前写入。修复建议正确，最关键的是在 vga_putentry_at / vga_update_cursor 处对 x、y 做范围钳位。

#### V114 [medium·待确认·arm64·已修复] 用户进程地址空间没有映射 virtio-mmio 区域 (0x0a000000)，在进程上下文刷新帧缓冲会触发内核 Data Abort

- **修复**：Fixed inside the driver rather than in create_space(): virtio-gpu registers are now accessed through the TTBR1 kernel mapping (PHYS_TO_VIRT of 0x0a000000), which is identical in every address space, instead of the low identity mapping that user address spaces lack. 验证方式：Partly: the arm64 boot probes all 32 virtio-mmio slots through the new mapping without faulting (prints 'Device not found' as before). Flushing from a user process with a real virtio-gpu-device present was not run.
- 位置：`src/drivers/arm/virtio_gpu.cpp:200`；相关：`src/arch/arm64/mm/mmu.cpp:1074`、`src/arch/arm64/mm/mmu.cpp:1076`、`src/fs/devfs.cpp:214`、`src/kernel/task.cpp:1108`、`src/drivers/arm/virtio_gpu.cpp:94`
- 证据：驱动直接用物理地址当虚拟地址访问 MMIO：`virtio_write32(VIRTIO_MMIO_QUEUE_NOTIFY, 0);`，virtio_base 位于 0x0a000000..0x0a003fff。启动页表用 1GB 设备块覆盖 0~1GB，所以内核线程下可用；但 hal::Mmu::create_space()（src/arch/arm64/mm/mmu.cpp:1074-1076）为每个进程新建的 L2 表只映射 `new_l2[64] = 0x08000000`（GIC）和 `new_l2[72] = 0x09000000`（串口），0x0a000000（L2 索引 80）没有映射。
- 触发场景：使用 `-device virtio-gpu-device` 启动使帧缓冲初始化成功后：(1) 用户进程 write(1, ...) -> devconsole_write (src/fs/devfs.cpp:214) -> Framebuffer::flush -> VirtioGpu::flush -> virtio_gpu_cmd 写 0x0a00xx50，当前 TTBR0 是进程页表 -> EL1 translation fault，exception.cpp 打印寄存器后 `while(1) wfi` 死机；(2) 即使进程不输出，Scheduler::timer_tick (src/kernel/task.cpp:1108) 在 arm64 上每 100 tick 调 LOG_INFO_MSG（klog 目标默认 BOTH），在 IRQ 上下文同样走到 virtio 的 MMIO 访问，用户进程运行满 1 秒必然崩溃。
- 修复方向：把所有内核 MMIO（GIC、PL011、virtio-mmio 等）通过 TTBR1 高地址区域统一映射（ioremap 风格），驱动使用该虚拟地址；临时方案是在 create_space() 中补上 0x0a000000 的 2MB 设备块映射。根本上应让内核运行在 TTBR1 而不是依赖 TTBR0 恒等映射。
- 复核意见：src/drivers/arm/virtio_gpu.cpp:23/383 把 0x0a000000 起的物理地址直接当虚拟地址，virtio_write32（:98）经 TTBR0 恒等映射访问；src/arch/arm64/mm/mmu.cpp:1072-1076 的 create_space 只填了 new_l2[64] 和 new_l2[72]，L2[80]（0x0a000000）确实为空。devfs.cpp:214 与 kprintf.cpp:385-387 在 Framebuffer 初始化后都会走到 VirtioGpu::flush，所以进程页表下访问会 translation fault。但 Makefile:346/457 默认用 `-device virtio-gpu-pci`，MMIO 扫描找不到设备、fb_initialized 保持 false，默认配置下不可达，且未做动态验证，故只判 plausible。 更正：仅在手动使用 `-device virtio-gpu-device`（virtio-mmio 传输）启动时才会触发；仓库 Makefile 的 run 目标用的是 virtio-gpu-pci，该驱动根本探测不到，帧缓冲不会初始化。严重度因此降为 medium。另外 timer_tick 的 LOG_INFO_MSG 路径是经 klog -> vkprintf_vga -> kprintf.cpp:386 的 Framebuffer::flush 到达的。

#### V115 [medium·已确认·arm64·未修复] virtio-gpu 驱动只扫描 virtio-mmio，而 Makefile 的 arm64 运行目标使用 -device virtio-gpu-pci，帧缓冲控制台永远无法初始化

- **未修复**：The fix is a Makefile change (-device virtio-gpu-pci to virtio-gpu-device in the arm64 run and run-disk targets) plus making kernel.cpp report terminal_init failure; both are outside my ownership. I also could not validate it: switching the device makes the framebuffer console path live on arm64 for the first time, and I was not allowed to start QEMU with that device. The driver-side blockers for doing so (drivers-arm-platform-2 and -3) are fixed in this branch but not run against a real device.
- 位置：`src/drivers/arm/virtio_gpu.cpp:383`；相关：`Makefile:346`、`Makefile:457`、`src/kernel/kernel.cpp:231`
- 证据：find_virtio_gpu() 只遍历固定的 MMIO 槽位：`volatile uint8_t *base = (volatile uint8_t *)(VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_SIZE);` 并要求 `device_id == VIRTIO_DEV_GPU`。但 Makefile:346 (`run`) 和 Makefile:457 (`run-disk`) 传给 QEMU 的是 `-device virtio-gpu-pci`，该设备挂在 PCIe ECAM 总线上，32 个 virtio-mmio 槽位的 DeviceID 全为 0。arm64 上没有任何 PCI/ECAM 代码。
- 触发场景：执行 `make run ARCH=arm64`：kernel.cpp:231 调用 drivers::Framebuffer::terminal_init() -> VirtioGpu::init() -> find_virtio_gpu() 返回 NULL，打印 "virtio-gpu: Device not found"，fb_initialized 始终为 false。QEMU 图形窗口一直黑屏，所有输出只走串口；kernel.cpp 仍然打印 "[4.2] Framebuffer console initialized"（terminal_init 无返回值，失败被吞掉）。
- 修复方向：把 Makefile 中 arm64 的 `-device virtio-gpu-pci` 改为 `-device virtio-gpu-device`（virtio-mmio 传输），或者实现 PCI ECAM + virtio-pci 传输；同时让 terminal_init 返回错误码，kernel.cpp 根据结果打印日志。
- 复核意见：src/drivers/arm/virtio_gpu.cpp:381-396 的 find_virtio_gpu() 只扫描 0x0a000000 起的 32 个 virtio-mmio 槽位并要求 DeviceID==GPU；Makefile:346 与 Makefile:457 给 arm64 传的是 -device virtio-gpu-pci（PCI 传输），而 kernel.cpp:234 注释也承认 arm64 没有 PCI 代码，因此设备不可能被发现。kernel.cpp:231-232 调用 terminal_init() 后无条件打印 "Framebuffer console initialized"，失败被吞掉。后果只是图形窗口黑屏、串口仍可用，属于功能缺失而非崩溃，故降为 medium。 更正：严重度建议 medium：不影响内核稳定性，只是 make run / run-disk 的图形控制台在 arm64 上不可用。修复方案（改为 -device virtio-gpu-device）正确。

#### V116 [medium·已确认·arm64·已修复] 显示分辨率来自设备但帧缓冲是固定 1280x800 的静态数组，分辨率更大时 memset/绘制越界覆盖内核 BSS

- **修复**：src/drivers/arm/virtio_gpu.cpp: gpu_get_display_info clamps the device-reported mode to the 1280x800 static framebuffer and ignores a zero width/height; init refuses to continue if the size still exceeds the buffer before computing fb_size. 验证方式：Untested at runtime: the test boot has no virtio-mmio GPU, so only the device-not-found path runs.
- 位置：`src/drivers/arm/virtio_gpu.cpp:547`；相关：`src/drivers/arm/virtio_gpu.cpp:265`、`src/drivers/arm/virtio_gpu.cpp:544`、`src/drivers/arm/virtio_gpu.cpp:551`、`src/drivers/arm/framebuffer.cpp:251`
- 证据：gpu_get_display_info() 无上限地接受设备值：`display_width = resp->pmodes[i].r.width; display_height = resp->pmodes[i].r.height;`。随后 `fb_size = display_width * display_height * 4;`，但缓冲区是 `static uint32_t static_fb[DEFAULT_WIDTH * DEFAULT_HEIGHT]`（1280*800*4 = 4,096,000 字节），接着 `memset(framebuffer, 0, fb_size);`，并以 fb_size 作为 attach_backing 的长度。framebuffer.cpp 的 clear()/fb_scroll()/fb_put_pixel_fast() 也都以 fb_info.width*height 为界写入。
- 触发场景：用 `-device virtio-gpu-device,xres=1920,yres=1080` 启动：fb_size = 8,294,400，memset 从 static_fb 起清零 8.3MB，越界约 4.2MB，把其后的内核 .bss（任务池、页表/PMM 元数据等）清零；之后每次清屏/滚屏继续越界写，同时宿主机通过 backing 读取越界的内核内存。结果是启动阶段随机崩溃或静默的内核数据损坏。
- 修复方向：在 gpu_get_display_info() 中将宽高钳制到 DEFAULT_WIDTH/DEFAULT_HEIGHT（或拒绝过大模式），或者按实际分辨率从 PMM 分配连续物理页作为帧缓冲；同时校验 width/height 非零及乘法溢出。
- 复核意见：src/drivers/arm/virtio_gpu.cpp:263-266 直接采用设备返回的 r.width/r.height，没有任何上限；:544 计算 fb_size = w*h*4，而 :547 的 static_fb 固定为 1280*800 个 uint32_t，:551 memset(framebuffer,0,fb_size) 以及 :561 attach_backing 都使用 fb_size，分辨率大于默认值时必然越界写 BSS。但 QEMU virtio-gpu 默认分辨率正好是 1280x800，只有用户显式指定 xres/yres（且使用 mmio 传输，见同批第 1 条，Makefile 默认的 pci 设备根本找不到）才会触发，所以是非默认配置下的内存破坏。 更正：触发条件需同时满足：使用 virtio-gpu-device（mmio）且 xres/yres 超过 1280x800；当前 Makefile 的默认目标下驱动找不到设备，不会走到这段代码。严重度建议 medium。修复时还应检查 width/height 为 0 及乘法溢出。

#### V117 [medium·待确认·i686·已修复] e1000 的描述符环和收发缓冲区用 kmalloc_aligned 从内核堆分配，不保证物理连续，跨页时 DMA 写入错误的物理页

- **修复**：New drivers::Dma (src/drivers/x86/dma.cpp, src/include/drivers/x86/dma.h): page-aligned, zeroed, physically contiguous frames below 4GB via Pmm::alloc_frames, accessed through the kernel direct map. e1000 RX/TX descriptor rings and packet buffers now come from it, two 2048-byte buffers per page so no buffer crosses a page. 验证方式：Unit tests test_dma_alloc_properties, test_dma_free_returns_frames, test_dma_alloc_invalid (suite dma_alloc_tests). Boot regression on i686: e1000 initialises with link up. No packet traffic is exercised by the test boot.
- 位置：`src/drivers/x86/e1000.cpp:143`；相关：`src/drivers/x86/e1000.cpp:127`、`src/drivers/x86/e1000.cpp:180`、`src/drivers/x86/e1000.cpp:196`、`src/mm/heap.cpp:99`
- 证据：`dev->rx_buffers[i] = (uint8_t *)kmalloc_aligned(E1000_RX_BUFFER_SIZE, 16);` 之后只对缓冲区首地址做一次 `mm::Vmm::virt_to_phys(...)` 并写入 `rx_descs[i].buffer_addr`。i686 的堆扩展 (src/mm/heap.cpp:98-113) 是逐页 `mm::Pmm::alloc_frame()` + `map_page`，相邻虚拟页的物理帧并不保证连续（中间可能插入页表帧，或复用之前释放的帧）。2048 字节缓冲区只按 16 字节对齐，32 个 RX + 32 个 TX 缓冲区中大约一半会跨越 4K 页边界；512 字节的描述符环 (第127/180行) 同样可能跨页。网卡只拿到首地址的物理地址，会按物理地址线性写 2048 字节。
- 触发场景：系统运行一段时间后（或启动阶段堆扩展时物理帧不连续），某个 rx_buffers[i] 的后半部分所在虚拟页映射到与首页不相邻的物理帧。收到一个较大的帧（例如 1500 字节）时，网卡把数据后半段写到首页物理帧之后的那个物理帧，该帧可能属于页表、其他进程或堆中其他对象，造成静默内存破坏；驱动随后从虚拟地址 memcpy 得到的是后半段为垃圾的数据包。TX 方向则发送出错误内容。目前在 QEMU 中能工作只是因为启动早期 PMM 恰好顺序分配。
- 修复方向：DMA 内存改用物理连续的页分配（例如 mm::Pmm::alloc_frames_zone(count, ZONE_NORMAL/ZONE_DMA) 后再映射），或至少让每个缓冲区页对齐且不超过一页（kmalloc_aligned(2048, 4096) 仍需保证单页内）；描述符环单独占用一个整页。
- 复核意见：src/drivers/x86/e1000.cpp:127/143（及 TX 环对应位置）确实用 kmalloc_aligned(…,16) 分配描述符环和 2048 字节缓冲区，并只对首地址做一次 virt_to_phys；src/mm/heap.cpp:96-113 的 i686 堆扩展逐页 Pmm::alloc_frame()+map_page，没有任何物理连续性保证，kmalloc_aligned（heap.cpp:399-431）也不保证不跨页。缺陷在代码层面成立，但 e1000 在启动阶段初始化，此时 PMM 的 find_free_frame 基本按顺序分配，观测上 i686 ping 正常；实际是否出现不连续帧（例如跨 4MB 边界插入页表帧或复用已释放帧）未能静态确认。 更正：应定性为潜在（latent）缺陷：当前启动顺序下物理帧大概率连续，故障取决于堆扩展时 PMM 的分配状态。严重度建议 medium。x86_64 上 e1000 被跳过，不受影响。

#### V118 [medium·已确认·i686,x86_64·已修复] 帧缓冲终端和双缓冲脏区状态完全没有锁，中断/抢占中的 kprintf 与线程上下文并发时 flush() 会用负的行数计算出近 4GB 的 memcpy

- 位置：`src/drivers/x86/framebuffer.cpp:331`；相关：`src/drivers/x86/framebuffer.cpp:321`、`src/drivers/x86/framebuffer.cpp:795`、`src/drivers/x86/framebuffer.cpp:806`、`src/lib/kprintf.cpp:385`、`src/fs/devfs.cpp:203`、`src/drivers/x86/e1000.cpp:529`
- 证据：flush(): 第321行检查 `dirty_line_start < 0 || dirty_line_end < 0`，第326行 `if (dirty_line_start < 0) dirty_line_start = 0;`，第330-331行 `offset = dirty_line_start * fb_info.pitch; size = (dirty_line_end - dirty_line_start) * fb_info.pitch;`，第334行 memcpy，第337-338行把两者置 -1。全局静态变量 dirty_line_*、term_cursor_*、ansi_state/ansi_params/ansi_param_count 没有任何锁或关中断保护；kprintf/klog（默认 LOG_TARGET_BOTH）和 devconsole_write (src/fs/devfs.cpp:202-214) 都直接调用 terminal_putchar/flush，而 IRQ 处理函数里也会打日志（如 e1000.cpp:529 的 LOG_INFO_MSG、irq.cpp:124 的 Unhandled IRQ，以及中断上下文中运行的网络栈日志）。
- 触发场景：用户进程 write(/dev/console) 进入 devconsole_write -> flush()，刚通过第321行检查（start=100,end=116）即被网卡中断打断；中断里 LOG_INFO_MSG 输出并在 vkprintf 末尾调用 flush()，把 dirty_line_start/end 复位为 -1。返回后原 flush 执行第326行把 start 置 0，end 仍为 -1，size = (uint32_t)((-1 - 0) * pitch) ≈ 4GB，memcpy 越过帧缓冲映射区写入并触发内核缺页崩溃。同类竞争还包括：';' 分支中 ansi_param_count++ 后被中断里的序列结束把计数清零，随后 `ansi_params[ansi_param_count - 1] = 0` 写到 ansi_params[-1]；光标行列被并发修改导致输出错乱。时钟抢占下两个任务同时写控制台也会触发同样的问题。
- 修复方向：为帧缓冲终端加一把 SpinlockIrq（terminal_putchar/terminal_write/flush/clear/scroll 全部在锁内），flush 中先把 start/end 读到局部变量并校验 `start < end` 后再计算大小；或者让中断上下文的日志只进入环形缓冲区，由线程上下文刷到屏幕。
- 复核意见：framebuffer.cpp:321-338 的 flush() 对全局 dirty_line_start/end 无任何锁或关中断保护，kprintf.cpp:385 和 devfs.cpp:213 都会调用它。Makefile:88 是 -O0，所以 :321、:326、:330-331 每次都重新读全局变量；若在 :321 之后被一次会打日志的中断（如 e1000.cpp:529 的 LOG_INFO_MSG，或中断里运行的网络栈日志）打断并被复位为 -1，:326 把 start 置 0 而 end 仍为 -1，size 变成约 4GB 的 memcpy，内核崩溃。:805-806 的 ansi_param_count 同理可写到 ansi_params[-1]。 更正：窗口只有十来条指令，且需要启用帧缓冲双缓冲并有中断上下文日志输出到屏幕，故降为 medium。时钟抢占下两个任务并发写控制台的说法未核实调度器是否抢占内核态。若改用优化编译，值会被缓存在寄存器里，该具体场景会消失，但数据竞争仍在。

#### V119 [medium·待确认·i686·已修复] UHCI/USB 核心/MSC 完全没有锁：每类传输只有一个 QH 槽位，并发提交互相覆盖

- **修复**：Added a per-controller sync::Mutex held across Uhci::submit_urb (schedule slot, TD/QH free lists) and a per-device sync::Mutex held across the whole CBW-data-CSW transaction in msc_scsi_command (also covers msc->tag). Multiple QHs per transfer type were not implemented; transfers on one controller are simply serialised. 验证方式：Untested at runtime (no UHCI controller in the test boot). Compile only.
- 位置：`src/drivers/x86/usb/uhci.cpp:710`；相关：`src/drivers/x86/usb/uhci.cpp:580`、`src/drivers/x86/usb/uhci.cpp:626`、`src/drivers/x86/usb/uhci.cpp:755`、`src/drivers/x86/usb/uhci.cpp:113`、`src/drivers/x86/usb/usb_mass_storage.cpp:135`
- 证据：`hc->qh_bulk->element = qh->phys_addr | UHCI_LP_QH; hc->active_bulk_qh = qh;`（控制传输在 580 行同样写 `hc->qh_ctrl->element`），结束时无条件 `hc->qh_bulk->element = UHCI_LP_TERM;`。free_tds/free_qhs 空闲链表、msc->tag、端点 toggle、hc->devices 链表都没有任何锁或关中断保护；等待循环 Timer::wait(1) 期间任务可被抢占（irq_handler 末尾 schedule_from_irq）。fs::Blockdev::read 也不加锁。
- 触发场景：两个任务同时访问 USB 磁盘（例如一个经 FAT32，另一个经 /dev 原始块设备或分区表探测，或 shell 的 `usb scan` 与文件 I/O 并发）：任务 A 发送 CBW 后被抢占，任务 B 覆盖 qh_bulk->element 并发送自己的 CBW；A 的 TD 被从调度中摘掉 -> 超时；B 完成时把 element 置 TERM。结果是 CBW/DATA/CSW 序列交错（tag 不匹配、数据写到错误 LBA）、TD 空闲链表被并发修改而损坏。未验证点：若所有访问都经同一 FAT32 的 fs_lock 则被上层串行化。
- 修复方向：为每个控制器加互斥锁保护 TD/QH 池和调度链表，并支持在 qh_ctrl/qh_bulk 后链接多个 QH；MSC 层对每个设备的整个 CBW-DATA-CSW 事务加互斥锁。
- 复核意见：uhci.cpp:580 与 :710 直接覆盖 hc->qh_ctrl->element / qh_bulk->element，结束时无条件置 UHCI_LP_TERM；free_tds 空闲链表 (:113-116) 和 usb_mass_storage.cpp:135 的 `++msc->tag` 都无锁，grep 在 usb_mass_storage.cpp 中找不到任何 Mutex。等待循环用 Timer::wait(1) 忙等且中断打开，可被抢占。但是否真有两个任务并发访问同一 USB 设备未核实：fat32.cpp:77 有 fs_lock，可能把常见路径串行化。 更正：需要显式配置 UHCI 控制器和 USB 存储设备才可达，默认运行命令不含。并发来源（原始块设备与 FAT32 同时访问、shell 的 usb 命令）未逐一核实，故降为 medium。

#### V120 [low·待确认·i686·已修复] 热拔出时直接 kfree MSC 设备（内嵌 fs::Blockdev）和 usb_device，仍被文件系统/进行中的传输引用 -> use-after-free

- **修复**：UsbMsc::disconnect now marks the device gone under the device lock (ready=false, usb_dev/endpoints nulled) before unregistering, so later block I/O returns an error instead of touching the freed usb_device; msc_scsi_command rechecks after taking the lock. The msc structure (which embeds the Blockdev) is only freed when the block device has no remaining references; otherwise it is deliberately kept. This is a leak-instead-of-use-after-free fix: there is no release hook to free it when the last reference is dropped later, and usb_device itself is still not reference counted. 验证方式：Untested at runtime (no USB device in the test boot). Compile only.
- 位置：`src/drivers/x86/usb/usb_mass_storage.cpp:531`；相关：`src/drivers/x86/usb/usb.cpp:781`、`src/drivers/x86/usb/uhci.cpp:1057`
- 证据：disconnect 中 `fs::Blockdev::unregister_device(&msc->blockdev); kfree(msc);`。unregister_device 在 `dev->ref_count > 1` 时只打印 "Unregistering device ... with outstanding references" 就继续，而 blockdev 结构体内嵌在 msc 中被一并释放；随后 usb.cpp:781 `free_device(dev)` 释放 usb_device（msc->ep_in/ep_out、URB 的 device/endpoint 都指向其内部）。没有任何引用计数或“设备已移除”标志。
- 触发场景：U 盘上的 FAT32 已挂载（持有 Blockdev 引用）时拔出设备：msc 与 usb_device 被释放，之后任何文件操作通过悬空的 fs::Blockdev* 调用 ops->read(private_data=已释放 msc)，解引用已释放的 usb_dev/端点，向已复用的堆内存读写。若拔出发生在某任务正处于 uhci_submit_bulk 轮询期间，urb->endpoint->toggle 的回写直接写入已释放内存。未验证点：断开路径本身目前在中断上下文中会先因 Timer::wait/互斥锁问题出事（见热插拔条目）。
- 修复方向：把 Blockdev 独立分配并引用计数；断开时仅标记 msc->ready=false / 设备 gone，让 I/O 返回错误，待引用归零后再释放；usb_device 同样引用计数。
- 复核意见：代码属实：usb_mass_storage.cpp:527-531 在 unregister_device 后直接 kfree(msc)，blockdev.cpp:163-166 对仍有引用的情况只打印警告，usb.cpp:781 随后 free_device(dev)，全程没有引用计数或已移除标志。但当前内核里没有任何代码消费 USB 块设备：fs_bootstrap.cpp:84-93 只对 ata0..ata3 调用 Fat32::init，Blockdev::get_by_name / Fat32::init 在别处没有调用者，所以“已挂载的 U 盘 FAT32 持有悬空 Blockdev*”这个场景目前无法出现。属于潜在的生命周期缺陷，一旦有人给 USB 存储加挂载支持就会变成真实的 UAF。 更正：当前可达的后果应降级：没有挂载 USB 存储的路径，也没有其它持有 msc->blockdev 引用的使用者，因此文件系统 UAF 场景今天不可达；“传输进行中被拔出”也需要有任务在对该设备做 I/O，同样没有调用者。另外断开路径是在 IRQ0 回调里运行（见 x-error-init-9），其中 blockdev.cpp:151 在中断上下文里获取互斥锁，这才是拔出时更现实的问题。修复建议本身（引用计数、标记 gone）合理。


### 用户态（3 项）

#### V121 [high·已确认·all·已修复] 用户库 printf/snprintf 打印负数时丢掉首位数字（%d 输出错误）

- **修复**：user/lib/src/stdio.cpp, %d/%i in both printf and snprintf: the buffer holds digits only, the sign is emitted separately in all three alignment modes and counted in the field width, and the magnitude is taken with unsigned arithmetic so LLONG_MIN works. 验证方式：untested at runtime: added test_printf_signed to user/tests/tests.cpp (8 cases via snprintf), which compiles on all three archs, but tests.elf is not run by test.sh; shell/hello boot regression passes
- 位置：`user/lib/src/stdio.cpp:167`；相关：`user/lib/src/stdio.cpp:192`、`user/lib/src/stdio.cpp:444`、`user/lib/src/stdio.cpp:469`、`user/lib/src/stdio.cpp:10`
- 证据：`num_to_str_dec((unsigned long long)(val < 0 ? -val : val), val < 0, tmp, &len);` 传入的已经是绝对值，而 num_to_str_dec 只有在 `(long long)val < 0` 时才往 tmp 写 '-'，所以 tmp 里没有符号。但后面的输出代码假设 tmp[0] 是 '-'：`if (!zero_pad && val < 0) buffer[pos++]='-'; for (int i = (val < 0 && !zero_pad) ? 1 : 0; ...)` 从下标 1 开始拷贝，跳过了第一位数字；左对齐分支和 zero_pad 且 pad==0 的分支则完全不输出符号。snprintf 中同样的代码在 444/469 行。
- 触发场景：用户 shell 执行 `printf("Error: exec failed for '%s' (code=%d)\n", path, ret)`，ret=-1 时输出 `code=-`；printf("%d", -42) 输出 `-2`；printf("%-5d", -42) 输出 `42   `；printf("%02d", -42) 输出 `42`。所有负数的十进制输出都是错的。
- 修复方向：统一约定：要么向 num_to_str_dec 传原始有符号值并让它写符号（调用方不再单独写 '-'），要么 tmp 只含数字、调用方负责符号并始终从 i=0 拷贝。同时修正左对齐和 zero_pad 分支的符号输出，并处理 LLONG_MIN。
- 复核意见：user/lib/src/stdio.cpp:167-168 向 num_to_str_dec 传入的是绝对值，而该函数(第8-13行)只在 (long long)val<0 时写 '-'，所以 tmp 只含数字。右对齐分支(第189-195行)先单独输出 '-'，再从 i=1 开始拷贝，丢掉首位数字：-42 输出 "-2"，-1 输出 "-"。左对齐分支(173-176行)与 zero_pad 且 pad==0 的情况完全不输出符号；snprintf 的 444-472 行是同样的代码。 更正：zero_pad 且 pad>0 的情况(如 %05d)是正确的；出错的是无宽度/空格填充右对齐、左对齐、以及 zero_pad 但宽度不足三种情况。LLONG_MIN 的 -val 溢出是附带的次要问题。

#### V122 [medium·待确认·arm64·已修复] arm64 控制台输入不做 CR->LF 转换，用户 Shell 只认 '\n'，交互终端按回车无法提交命令

- **修复**：User-side half only: shell_read_line treats '\r' as end of line, same as '\n'. The kernel-side CR->LF mapping in devconsole_read / Serial::getchar was not added (devfs.cpp and the serial driver are outside this batch). 验证方式：boot regression only (the arm64 exec check feeds '\n' and still passes); not tried on an interactive raw tty
- 位置：`user/shell/shell.cpp:130`；相关：`src/fs/devfs.cpp:152`、`src/drivers/arm/serial.cpp:274`、`src/kernel/kernel_shell.cpp:526`
- 证据：shell_read_line: `} else if (c == '\n') { buffer[i] = '\0'; print("\n"); return (int)i; }`，其余分支只接受 '\b'/127、ESC、32..126，'\r'(0x0D) 落入无分支被静默丢弃。arm64 的输入链路 devconsole_read -> drivers::Serial::getchar() (`return (char)(pl011_read(PL011_DR) & 0xFF);`) 原样返回串口字节，全仓库 grep `'\r'` 在输入路径上没有任何 CR->LF 映射（只有 more 命令的按键判断接受 '\r'）。x86 键盘扫描码表把 Enter 映射为 '\n'，所以只有 arm64 受影响。
- 触发场景：make run ARCH=arm64（-serial mon:stdio）时 QEMU 把宿主终端设为 raw 模式（ICRNL 关闭），回车键发送 0x0D。用户输入 `ls` 后按回车：shell 读到 '\r'，不匹配任何分支，命令永远不执行，只能用 Ctrl-J 提交。未验证的假设：宿主终端确实发送 CR（QEMU stdio chardev 的标准行为）；若测试脚本通过管道喂 '\n' 则不会暴露。
- 修复方向：在内核侧做最小行规程：arm64 串口输入路径（devconsole_read 或 Serial::getchar）把 '\r' 映射为 '\n'；同时用户 shell_read_line 把 '\r' 与 '\n' 同等处理。
- 复核意见：user/shell/shell.cpp:130 的 shell_read_line 只把 '\n' 当作行结束，'\r' 不落入任何分支被丢弃；arm64 的 devconsole_read(src/fs/devfs.cpp:144-161)直接返回 Serial::getchar() 的原始字节(src/drivers/arm/serial.cpp:267-274)，全仓库输入路径没有 CR->LF 映射。Makefile 的 arm64 运行目标确实使用 -serial mon:stdio(346/355/457行)。但 QEMU 在交互终端下实际送入 0x0D 这一环未做动态验证，且已有的 arm64 实测能在用户 shell 中执行 exec，说明至少管道喂入 '\n' 的方式可用，故只判 plausible。 更正：影响仅限交互式终端(宿主 tty 为 raw 模式)下的 arm64；通过管道/脚本喂入 '\n' 不受影响，Ctrl-J 可作为绕过手段，严重度降为 medium。
- 被 2 个独立审计者重复报告（gap4-console-input-flow-1, user-shell-3）

#### V123 [medium·已确认·all·已修复] 单阶段“管道”直接在 shell 进程内 exec 外部程序，shell 被替换后系统失去交互入口

- **修复**：user/shell/shell.cpp: shell_parse_pipeline reports an empty segment ('prog |', '| prog', 'a || b') as a syntax error instead of dropping it, and the num_stages == 1 path now goes through shell_execute_command (no exec in the shell's own process). 验证方式：boot regression only
- 位置：`user/shell/shell.cpp:2198`；相关：`user/shell/shell.cpp:2170`、`user/shell/shell.cpp:3014`、`user/shell/shell.cpp:2107`
- 证据：shell_run() 只要行内含 '|' 就走管道路径 (3014)。shell_parse_pipeline 会丢弃空段，所以 "/bin/hello.elf |" 或 "| /bin/hello.elf" 得到 num_stages==1。shell_execute_pipeline: `if (num_stages == 1) return shell_execute_single_command(stages[0].argc, stages[0].argv);` 没有 fork；而 shell_execute_single_command 对非内建命令执行 `int ret = exec(abs_path);` (2170)。该函数的注释写的是“在子进程中”，但此路径是在 shell 自身进程里调用的。
- 触发场景：用户输入 `/bin/helloworld.elf |`（或任何带多余 '|' 的外部程序路径）。shell 进程自身被 execve 替换成 helloworld，程序退出后没有任何进程重新拉起 shell（kernel.cpp 只调用一次 load_user_shell），系统只能重启。
- 修复方向：num_stages==1 时改走 shell_execute_command()（只执行内建命令），或者对外部程序先 fork 再 exec 并 waitpid；同时对空管道段报语法错误而不是静默丢弃。
- 复核意见：user/shell/shell.cpp:3014 只要行内含 '|' 就走管道路径；shell_parse_pipeline 仅在 argc>0 时才递增 num_stages(约2107行及末段处理)，所以 "prog |" 得到 num_stages==1。shell_execute_pipeline 在 2197-2199 行不 fork 直接调用 shell_execute_single_command，后者在 2170 行对外部程序直接 exec(abs_path)，于是 shell 进程自身被替换。 更正：需要用户输入带多余 '|' 的外部程序路径这种非常规命令才触发，属边缘输入，严重度降为 medium。src/kernel/kernel.cpp 中 load_user_shell 有两处调用(336、659行)，但未见 shell 退出后自动重启的逻辑，“无法恢复”的结论未逐一追踪到 init/回收路径；另外 arm64 上 exec 本身目前就会崩溃，后果表现不同。


### 构建系统与文档（1 项）

#### V124 [high·已确认·arm64·未修复] arm64 内核被链接到低半区物理地址 (TTBR0 范围)，与用户地址空间重叠，mmap 区域起点正好是内核所在的 1GB 块

- **未修复**：The real fix is a true higher-half arm64 kernel (linker_arm64.ld, start.S, arch/arm64/mm/mmu.cpp), and the interim one is moving MMAP_REGION_START and fixing is_vaddr_range_free in src/kernel/syscalls/mm.cpp; all of those are outside this batch and too large to do as a side edit. The dangerous part of the entry (a syscall buffer pointing at the low-half kernel block) is already closed by e99b46d, because UAccess requires HAL_PAGE_USER on every page and the kernel block is not user-mapped. Still open: mmap fails on arm64, and the kernel block lacks UXN.
- 位置：`linker_arm64.ld:19`；相关：`linker_arm64.ld:8`、`src/arch/arm64/boot/start.S:259`、`src/arch/arm64/mm/mmu.cpp:1002`、`src/kernel/syscalls/mm.cpp:22`、`src/include/kernel/task.h:32`
- 证据：linker_arm64.ld: `KERNEL_VIRTUAL_BASE = 0xFFFF000000000000;` 定义后从未使用，`. = KERNEL_PHYS_BASE;`(0x40100000)，所有段 VMA=LMA（readelf: 单个 LOAD 0x40100000 RWE）。start.S:259-260 把同一张 boot_l0_table 同时装入 ttbr0_el1/ttbr1_el1，注释“暂时不使用高半核”。mmu.cpp:1000-1016 明确写着 “The kernel runs at physical addresses (0x40xxxxxx) which are in the TTBR0 region”，每个用户地址空间都复制 L1[1]（0x40000000-0x7FFFFFFF 的 1GB 内核块）。而通用代码 src/kernel/syscalls/mm.cpp:22-23 `#define MMAP_REGION_START 0x40000000 / MMAP_REGION_END 0x70000000`，task.h:32 `USER_SPACE_END 0x0000800000000000`。
- 触发场景：arm64 用户进程调用 mmap()：内核在 0x40000000 起选择虚拟地址，这正是内核代码/数据/堆所在的 1GB 块映射；要么映射失败（mmap 在 arm64 上不可用），要么拆分/替换该块导致内核自身映射被改写而崩溃（未跟踪 map_page 对 block 描述符的处理，故 confidence=medium）。此外任何基于 `addr < USER_SPACE_END` 的用户指针检查都会把 0x40100000 这类内核地址判为合法用户地址，read(fd, (void*)0x40100000, n) 之类的系统调用可直接覆盖内核；块描述符未设 UXN，EL0 还能执行内核代码页。
- 修复方向：按 x86_64 的方式做真正的高半核：.text.boot 保持物理地址，其余段 `. += KERNEL_VIRTUAL_BASE` 并用 AT(ADDR(x) - KERNEL_VIRTUAL_BASE) 指定 LMA，启用 MMU 后跳转到 TTBR1 地址，TTBR0 只留给用户；过渡期至少让 arm64 的 MMAP_REGION/USER_SPACE_END 避开 0x40000000-0x7FFFFFFF 并给内核块加 UXN。
- 复核意见：linker_arm64.ld:19 确实 `. = KERNEL_PHYS_BASE`(0x40100000)，KERNEL_VIRTUAL_BASE 未被使用；start.S:259-260 把同一张 boot_l0_table 装入 TTBR0/TTBR1；mmu.cpp:1000-1016 及 1081 行在每个用户地址空间里复制 L1[1..3] 的 1GB 内核块。mm.cpp:22-23 的 MMAP_REGION 0x40000000-0x70000000 正落在该块内，hal::Mmu::map 在 mmu.cpp:720-721 遇到 1GB block 直接报错返回，所以 arm64 上 mmap 必然失败而不是改写内核映射。src/kernel 中 grep 不到任何 access_ok/copy_from_user，task.h:32 的 USER_SPACE_END 也覆盖 0x40xxxxxx，内核以 EL1 RW 权限可被系统调用缓冲区指针覆盖。 更正：后果应明确为“mmap 在 arm64 上失败”(map 拒绝覆盖 block，不会拆块破坏内核)。另外 mm.cpp:129-150 的 is_vaddr_range_free 按 i686 两级页表(addr>>22, PHYS_TO_VIRT(page_dir_phys))去遍历 arm64 的 L0 表，本身就是错的，mmap/munmap 在 arm64 上整体不可用。内核块 AP=EL1 RW，EL0 不能读写，仅因未设 UXN 而可执行，这一点危害较小；真正的危害是系统调用完全不校验用户指针(所有架构都有)，叠加内核位于低半区。
