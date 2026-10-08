# CastorOS

[![test](https://github.com/castorworks/CastorOS/actions/workflows/test.yml/badge.svg)](https://github.com/castorworks/CastorOS/actions/workflows/test.yml)

> CastorOS is an operating system designed for learning and fun.

一个用于学习的微内核：内核只做 CPU/中断、内存管理、任务调度、系统调用和进程间通信，驱动、文件系统、网络、命令行都是用户态程序。结构说明见 [docs/microkernel.md](docs/microkernel.md)，全部文档的索引见 [docs/](docs/README.md)。

## 支持的架构

| 架构 | 描述 | 启动方式 |
|------|------|----------|
| i686 | Intel x86 32位 | Multiboot1 |
| x86_64 | AMD64/Intel 64位 | Multiboot1（ELF32 外壳） |
| arm64 | ARM AArch64 | QEMU `-M virt`，DTB |

三个架构都能用上机器的多个 CPU（最多 8 个，`make run SMP=4`）：用户进程在各个 CPU 上同时运行，内核自己由一把锁保护，见 [多个 CPU](docs/reference/smp.md)。

## 开发语言

内核与用户态程序均使用 freestanding C++20 编写（`-std=gnu++20 -fno-exceptions -fno-rtti`），引导与中断入口等少量代码使用汇编。与汇编互相调用的符号需声明为 `extern "C"`；内核的最小 C++ 运行时位于 `src/lib/cxxrt.cpp`。

## 构建与运行

先按 [开发环境搭建](docs/setup.md) 安装交叉编译器和 QEMU。

```bash
make                    # 构建内核（默认 i686），自动构建并内嵌 user/init
make ARCH=x86_64
make ARCH=arm64
make build-all          # 所有架构

make run                # 在 QEMU 中运行，串口控制台接到当前终端
make debug              # 同上，等待 GDB 连接 :1234
make iso                # 系统映像（x86）：GRUB、内核和根文件系统，可以原样写进真机的硬盘

make test               # 构建带内核测试的版本 (KTEST=1) 并运行：内核测试 + 用户态自检 + 命令行检查
make test-all           # 所有架构；任何一个失败就返回非零
make lib-test           # 用户库的宿主机测试（printf、字符串、数学函数），不需要交叉编译器和 QEMU

make clean              # 清理当前架构
make clean-all          # 所有架构，包括 user/ 下的构建结果
make help
```

测试的细节、日志的位置、手动运行 QEMU 和 GDB 调试见 [测试与调试](docs/testing.md)。

## 运行起来是什么样

内核启动后加载内嵌的 `user/init`：它启动用户态的终端输入服务 `user/console`、串口驱动 `user/uart`、键盘驱动 `user/kbd`（x86）、块设备驱动 `user/blk`、网络服务 `user/net`、内存文件系统 `user/ramfs`、磁盘文件系统 `user/diskfs` 和命令行 `user/sh`，自己充当名字服务。文件都在一棵目录树里（`/bin`、`/etc`、`/home`、`/tmp`……），根在磁盘上；sh 先执行 `/etc/rc`（运行用户态自检 `selftest`），然后接受命令：

```text
> ls /
          bin/
          etc/
          home/
          tmp/
          usr/
> hello one two
hello from pid 17 (parent 7)
  argv[1] = one
  argv[2] = two
> cd /home
> write note.txt saved on disk
> ls
      14  note.txt
> ping 10.0.2.2
reply from 10.0.2.2: seq=1 time<10ms
...
> http example.com
connecting to example.com (104.20.23.154) port 80
HTTP/1.1 200 OK
...
```

命令行支持重定向和管道：`cmd > file`、`cmd < file`、`cmd 2> file`、`cmd1 | cmd2`（如 `ls | grep sh | wc`），`"带 空格"` 的参数用引号。文本文件可以当脚本运行（每行一条命令，`$1`-`$9` 是参数）。行尾加 `&` 让程序在后台运行（`jobs` 查看，`kill <pid>` 终止），Ctrl-C 终止前台程序。

除了 `/tmp`（在内存里），整棵树都在磁盘上（`make run` 用的是 `disk-<arch>.img`），改的东西重启后还在。没有磁盘、或者磁盘上没有这个系统时，根退回到内存里，用的是内核带着的那一份。`make iso` 做出的系统映像可以原样写进一台 PC 的硬盘或者 U 盘，从它启动。

这些都发生在用户态：键盘输入经串口中断 → uart 驱动 → console 服务 → IPC 到达 sh（PC 的键盘则是键盘中断 → kbd 驱动 → console 服务）；文件操作经 IPC 和共享缓冲区交给文件服务，磁盘文件再经块设备服务到 virtio-blk 驱动；网络请求交给 `user/net`（virtio-net 驱动加 ARP/IPv4/ICMP/UDP/TCP 协议栈，启动时用 DHCP 取地址，接 QEMU 的用户网络）；运行程序是从文件服务读出 ELF 后 `fork` + `exec`，这一行的其余部分作为参数传给 `main(argc, argv)`。

## 目录

```text
src/arch/      架构相关代码 (i686, x86_64, arm64)
src/mm/        物理页 (PMM)、通用页表、地址空间与写时复制 (VMM)、内核堆
src/kernel/    调度、系统调用、IPC、中断转发、ELF 加载、自旋锁
src/drivers/   内核里仅有的驱动：调试输出（串口，x86 上还有 VGA 屏幕）和时钟
src/lib/       kprintf / klog / 字符串 / C++ 运行时
src/include/   头文件（按子系统分目录）
src/tests/     内核测试 (KTEST=1)

user/lib/      用户态库
user/init/     第一个用户进程：启动模块 + 名字服务
user/console/  终端输入服务
user/uart/     串口输入驱动
user/kbd/      PS/2 键盘驱动（x86）
user/blk/      块设备驱动（virtio-blk，x86 上还有 IDE 硬盘和 USB 2.0 的 U 盘）
user/net/      网络服务（virtio-net 驱动 + 协议栈）
user/ramfs/    内存文件系统服务（/tmp；没有磁盘时也是根，内嵌启动映像）
user/diskfs/   磁盘文件系统服务（根文件系统）
user/sh/       命令行
user/selftest/ 用户态自检程序（在 /bin 里）
user/ls/ cat/ cp/ rm/ mv/ mkdir/ echo/ write/ grep/ wc/ sleep/ clear/ disk/ ping/ ifconfig/ dns/ http/ echod/ hello/
               小程序（在 /bin 里）
user/bootfs/   系统文件树里的静态文件（etc/rc、usr/share/doc）
tools/         宿主机上运行的构建工具（mkdiskfs：做根文件系统的映像）

scripts/       交叉编译器安装脚本、make test 用的命令行检查脚本
docs/          文档
build/         构建输出：build/<arch>/、build/<arch>-ktest/
```

## 文档

- [开发环境搭建](docs/setup.md)：交叉编译器、QEMU、编辑器
- [测试与调试](docs/testing.md)：`make test` 做了什么、日志、持续集成、手动运行、GDB
- [微内核结构](docs/microkernel.md)：内核的边界、启动流程、init 和模块、如何加模块
- [各部分的说明](docs/README.md#reference各部分的说明)：系统调用表、IPC、硬件访问、驱动、文件服务、网络、命令行、浮点数
- [概念讲解](docs/concepts/README.md)：引导、高半核、内存管理、中断、进程、同步、系统调用背后的原理

给 AI 编码助手和贡献者的约定在 [AGENTS.md](AGENTS.md)。

文件系统、网络、USB、图形等内核子系统的旧实现保留在 git 历史里（`ef63e55` 及之前）。早期单内核阶段的分步开发记录（`docs/history/`）也在 git 历史里（`b3393de` 及之前）。

## Git 提交格式

提交信息以类型开头，例如 `fix: ...`、`feat(lib): ...`：

- `feat` 添加了新特性
- `fix` 修复问题
- `style` 无逻辑改动的代码风格调整
- `perf` 性能/优化
- `refactor` 重构
- `revert` 回滚提交
- `test` 测试
- `docs` 文档
- `chore` 依赖或者脚手架调整
- `workflow` 工作流优化
- `ci` 持续集成
- `types` 类型定义
- `wip` 开发中
