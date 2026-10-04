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

参考 [docs/00-environment.md](./docs/00-environment.md) 安装交叉编译器和 QEMU（不需要 GRUB）。

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

内核启动后加载内嵌的 `user/init`：它演示 `mmap`、`fork`/`waitpid`、IPC 和特权，然后启动用户态串口驱动 `user/uart`，把驱动通过 IPC 送来的输入回显出来。

## 目录

```
src/arch/      架构相关代码 (i686, x86_64, arm64)
src/mm/        PMM / VMM / 内核堆
src/kernel/    调度、系统调用、ELF 加载、同步原语
src/drivers/   串口和时钟
src/lib/       kprintf / klog / 字符串 / C++ 运行时
src/tests/     内核测试 (KTEST=1)
user/lib/      用户态库
user/init/     第一个用户进程
user/uart/     用户态串口输入驱动（第一个模块）
docs/          文档
```

## 文档

+ [微内核结构](./docs/microkernel.md)：内核边界、启动流程、系统调用表、IPC、硬件访问、如何加模块
+ [概念讲解](./docs/concepts/00-overview.md)
+ 开发过程记录（写于精简为微内核之前，其中提到的 GRUB 磁盘镜像、VGA、shell 等已不在代码里）：
  [环境](./docs/00-environment.md)、[引导](./docs/01-boot.md)、[基础设施](./docs/02-infrastructure.md)、
  [内存管理](./docs/03-mm.md)、[任务管理](./docs/05-task.md)、[用户模式](./docs/09-usermode.md)、
  [同步机制](./docs/10-sync.md)、[C++ 重构](./docs/19-cpp-migration.md)

文件系统、网络、USB、图形等子系统的实现保留在 git 历史里（`ef63e55` 及之前）。

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
