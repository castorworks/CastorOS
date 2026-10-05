# CastorOS

> CastorOS is an operating system designed for learning and fun.

一个用于学习的微内核：内核只做 CPU/中断、内存管理、任务调度、系统调用和进程间通信，驱动等其余功能都放在用户态。
结构说明见 [docs/microkernel.md](./docs/microkernel.md)。

## 支持的架构

| 架构 | 描述 | 启动方式 |
|------|------|----------|
| i686 | Intel x86 32位 | Multiboot1 |
| x86_64 | AMD64/Intel 64位 | Multiboot1（ELF32 外壳） |
| arm64 | ARM AArch64 | QEMU `-M virt`，DTB |

## 开发语言

内核与用户态程序均使用 freestanding C++20 编写（`-std=gnu++20 -fno-exceptions -fno-rtti`），引导与中断入口等少量代码使用汇编。
与汇编互相调用的符号需声明为 `extern "C"`；内核的最小 C++ 运行时位于 `src/lib/cxxrt.cpp`。

## 构建与运行

参考 [docs/00-environment.md](./docs/00-environment.md) 安装交叉编译器和 QEMU。

```bash
make                    # 构建内核（默认 i686），自动构建并内嵌 user/init
make ARCH=x86_64
make ARCH=arm64
make build-all          # 所有架构

make run                # 在 QEMU 中运行，串口控制台接到当前终端
make debug              # 同上，等待 GDB 连接 :1234

make test               # 构建带内核测试的版本 (KTEST=1) 并运行
make test-all           # 所有架构

make clean              # 清理当前架构
make clean-all
make help
```

内核启动后加载内嵌的 `user/init`：它启动用户态的串口驱动 `user/uart`、块设备驱动 `user/blk`、网络服务 `user/net`、内存文件系统 `user/ramfs`、磁盘文件系统 `user/diskfs` 和命令行 `user/sh`，自己充当名字服务。ramfs 从构建时打好的启动映像里装载文件和程序；sh 先执行 `rc`（运行用户态自检 `selftest`），然后接受命令：

```
> ls
   24960  cat
   24960  cp
   16820  hello
   ...
> hello one two
hello from pid 17 (parent 7)
  argv[1] = one
  argv[2] = two
> write disk:note.txt saved on disk
> cp hello disk:hello
> ls disk:
      14  disk:note.txt
   16820  disk:hello
> ping 10.0.2.2
reply from 10.0.2.2: seq=1 time<10ms
...
> http example.com
connecting to example.com (104.20.23.154) port 80
HTTP/1.1 200 OK
...
```

带 `disk:` 前缀的文件在磁盘（`disk.img`，`make run` 第一次运行时创建）上，重启后还在；磁盘上的程序同样可以直接运行（`disk:hello`）。

键盘输入经 串口中断 → uart 驱动 → IPC 到达 sh；文件操作经 IPC 和共享缓冲区交给文件服务，磁盘文件再经块设备服务到 virtio-blk 驱动；网络请求交给 `user/net`（virtio-net 驱动加 ARP/IPv4/ICMP/UDP/TCP 协议栈，启动时用 DHCP 取地址，接 QEMU 的用户网络）；运行程序是从文件服务读出 ELF 后 `fork` + `exec`，这一行的其余部分作为参数传给 `main(argc, argv)`。

## 目录

```
src/arch/      架构相关代码 (i686, x86_64, arm64)
src/mm/        PMM / VMM / 内核堆
src/kernel/    调度、系统调用、ELF 加载、同步原语
src/drivers/   串口和时钟
src/lib/       kprintf / klog / 字符串 / C++ 运行时
src/tests/     内核测试 (KTEST=1)
user/lib/      用户态库
user/init/     第一个用户进程：启动模块 + 名字服务
user/uart/     用户态串口输入驱动
user/blk/      用户态 virtio-blk 块设备驱动
user/net/      用户态网络服务（virtio-net 驱动 + 协议栈）
user/ramfs/    内存文件系统服务（内嵌启动映像）
user/diskfs/   磁盘文件系统服务
user/sh/       命令行
user/selftest/ 用户态自检程序（在启动映像里）
user/ls/ cat/ cp/ rm/ echo/ disk/ ping/ ifconfig/ dns/ http/ hello/   小程序（在启动映像里）
user/bootfs/   启动映像里的静态文件
docs/          文档
```

## 文档

+ [微内核结构](./docs/microkernel.md)：内核边界、启动流程、系统调用表、IPC、共享内存、名字服务、文件服务、块设备、网络、启动映像、硬件访问、如何加模块
+ [开发环境搭建](./docs/00-environment.md)
+ [概念讲解](./docs/concepts/00-overview.md)：引导、高半核、内存管理、中断、进程、同步、系统调用背后的原理
+ [历史开发记录](./docs/history/README.md)：早期单内核阶段的分步记录，代码已对不上，背景知识仍可参考

文件系统、网络、USB、图形等内核子系统的旧实现保留在 git 历史里（`ef63e55` 及之前）。

## Git 提交格式

+ `feat` 添加了新特性
+ `fix` 修复问题
+ `style` 无逻辑改动的代码风格调整
+ `perf` 性能/优化
+ `refactor` 重构
+ `revert` 回滚提交
+ `test` 测试
+ `docs` 文档
+ `chore` 依赖或者脚手架调整
+ `workflow` 工作流优化
+ `ci` 持续集成
+ `types` 类型定义
+ `wip` 开发中
