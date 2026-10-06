# 测试与调试

## 测试有哪几层

| 层 | 在哪里 | 检查什么 | 怎么跑 |
|----|--------|----------|--------|
| 内核测试 | `src/tests`（`ktest` 框架） | 内核内部的模块：各架构的 HAL、内存管理（PMM、VMM、写时复制、堆）、任务和 fork/exec、系统调用的出错路径、硬件许可、自旋锁、内核库 | `make test`，只在 `KTEST=1` 时编入，开机时在 init 启动之前运行 |
| 用户态自检 | `user/selftest` | 一个程序能观察到的行为：系统调用、IPC、文件、磁盘、网络 | `make test`，开机时由 `rc` 运行 |
| 命令行检查 | `scripts/shell-test.sh` | 需要有人敲键盘才能测的行为：后台任务、Ctrl-C、`kill`、重定向和管道、目录、引号、脚本 | `make test`，宿主机通过串口输入命令 |
| 用户库的宿主机测试 | `user/lib/tests/lib_test.cpp` | 用户库里不依赖内核的部分：`printf` 一族、字符串、数学函数 | `make lib-test`，几秒钟，不需要交叉编译器和 QEMU |

加检查时按这张表选地方：纯库代码放宿主机测试（最快）；程序能观察到的放自检；要输入的放命令行检查。新的内核测试模块要在 `src/tests/framework/test_runner.cpp` 里登记，没登记的模块不会运行。

## 运行测试

```bash
make test                      # i686：构建 KTEST=1 内核并运行，命令行检查做完即结束（通常十几秒）
make test ARCH=x86_64
make test ARCH=arm64
make test-all                  # 三个架构都跑完，有任何一个失败就返回非零
make lib-test                  # 用户库的宿主机测试

make test TEST_TIMEOUT=300     # 机器很忙时放宽上限（默认 180 秒）
make test ARCH=arm64 SMP=4     # 给虚拟机 4 个 CPU（默认 1 个；目前只有 arm64 会把其余的启动起来）
make test ARCH=x86_64 QEMU_MEMORY=3G   # 给虚拟机更多内存（默认是 QEMU 的 128MB）；
                               # 超过 1GB 时“高处的物理内存”那组内核测试才有内容
```

`make test` 构建到 `build/<arch>-ktest/`，和普通构建（`build/<arch>/`）分开。

### make test 做了什么

内核不会自己关机，所以 `make test` 通过 `scripts/shell-test.sh` 来控制 QEMU：

1. 启动 QEMU，等日志里出现 `sh: ready`（以 `TEST_TIMEOUT` 为上限）。到这里内核测试和用户态自检都已经跑完。
2. 向串口输入一串命令，检查命令行的行为：运行程序、后台任务、Ctrl-C、`kill`、被终止的服务的端口能否重用、程序读键盘输入、重定向和管道、目录、引号、标准错误、脚本。每一步等到预期的输出出现为止（每步最多 `STEP_TIMEOUT` 秒，默认 30）。
3. 做完就结束 QEMU，汇总各模块的 `Total/Passed/Failed tests` 计数。

出现下面任何一种情况，`make test` 返回非零：

- 内核测试有失败的用例；
- 用户态没有起来（没有 `sh: ready`）；
- 用户态自检没有通过（没有 `selftest: all passed`）；
- 自检跳过了任何一项。测试环境里磁盘（每次新建的 2MB 临时磁盘）、网卡、回显服务都在，所以出现 skipped 也算失败；
- 命令行检查没有全部通过。

### 日志

| 文件 | 内容 |
|------|------|
| `build/<arch>-ktest/test.log` | QEMU 的全部输出 |
| `build/<arch>-ktest/shell-test.log` | 命令行检查的结果，每项一行 `shelltest: <名字>: ok\|FAILED` |

### 机器很忙的时候

主机负载极高时（load 上百），QEMU 可能几十秒没有任何输出，自检里有时间上限的检查也可能超时。先看 `uptime` 再判断是不是真的坏了；不要并行跑构建和测试。

## 持续集成

每次推送到 `main`、每个 PR，GitHub Actions 都会对三个架构各跑一遍 `make test ARCH=<arch>`（`.github/workflows/test.yml`），arm64 再带着两个 CPU 跑一遍（`SMP=2`），另外还有一个只跑 `make lib-test` 的任务。

- 用的是 macOS 的 runner 和 Homebrew 的交叉编译器（见 [开发环境搭建](setup.md) 的方法一），所以和在 macOS 上本地开发是同一套工具。加了构建依赖的话，workflow 的 `brew install` 一行也要加。
- 三个架构各是一个独立的任务，一个失败不影响另外两个跑完。
- 每次运行的 `test.log` 和 `shell-test.log` 作为 artifact 上传，失败时先下载它们来看。
- runner 比开发机慢，所以那里把等待的上限放宽了（`TEST_TIMEOUT=600`、`STEP_TIMEOUT=120`）；它们只是上限，不会让通过的运行变慢。

## 手动运行

`make run` 在 QEMU 里运行当前架构，带上磁盘 `disk.img`（第一次运行时创建）和一块接 QEMU 用户网络的 virtio-net 网卡。控制台是串口，接到当前终端；命令行能做什么见 [启动映像和命令行](reference/shell.md)。

直接调用 QEMU 的话，最少只需要 `-kernel`：

```bash
timeout 20 qemu-system-i386 -kernel build/i686/castor.bin -serial stdio -display none
timeout 20 qemu-system-x86_64 -kernel build/x86_64/castor32.elf -serial stdio -display none
timeout 20 qemu-system-aarch64 -M virt -cpu cortex-a72 -kernel build/arm64/castor.bin -serial stdio -display none
```

这样运行不带磁盘和网卡：blk、diskfs、net 会直接退出，selftest 跳过相关的检查。要带上就加：

```bash
# x86
-drive file=disk.img,format=raw,if=none,id=disk0 -device virtio-blk-pci,drive=disk0
-netdev user,id=net0,guestfwd=tcp:10.0.2.100:7-cmd:cat -device virtio-net-pci,netdev=net0

# arm64：同上，设备名换成 virtio-blk-device / virtio-net-device
```

向 QEMU 的标准输入写入的内容经 uart 驱动送到 sh，所以也可以用管道喂命令；Ctrl-C 是字节 `0x03`，Ctrl-D 是 `0x04`。

## GDB

```bash
make debug                     # QEMU 启动后停住，等 GDB 连接 :1234
gdb build/i686/castor.bin -ex 'target remote :1234'    # 在另一个终端里
```

## 检查构建

```bash
make check                     # 构建当前架构，显示内核映像的大小
make build-all                 # 构建所有架构
make info                      # 显示当前配置（编译器、QEMU、编译选项、源文件数）
make sources                   # 列出源文件
make help                      # 列出所有目标
```
