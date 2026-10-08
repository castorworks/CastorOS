# 测试与调试

## 测试有哪几层

| 层 | 在哪里 | 检查什么 | 怎么跑 |
|----|--------|----------|--------|
| 内核测试 | `src/tests`（`ktest` 框架） | 内核内部的模块：各架构的 HAL、内存管理（PMM、VMM、写时复制、堆）、任务和 fork/exec、系统调用的出错路径、硬件许可、自旋锁、内核库 | `make test`，只在 `KTEST=1` 时编入，开机时在 init 启动之前运行 |
| 用户态自检 | `user/selftest` | 一个程序能观察到的行为：系统调用、IPC、文件、磁盘、网络 | `make test`，开机时由 `rc` 运行 |
| 命令行检查 | `scripts/shell-test.sh` | 需要有人敲键盘才能测的行为：后台任务、Ctrl-C、`kill`、重定向和管道、目录、引号、脚本 | `make test`，宿主机通过串口输入命令（x86 上还在虚拟机的键盘上敲键） |
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
make test ARCH=arm64 SMP=4     # 给虚拟机 4 个 CPU（默认 1 个，最多 8 个；三个架构都认）
make test KBD=usb              # x86：给虚拟机接一个 USB 键盘，敲的键走 usbkbd 而不是 kbd（make run 也认）
make test ARCH=x86_64 QEMU_MEMORY=3G   # 给虚拟机更多内存（默认是 QEMU 的 128MB）；
                               # 超过 1GB 时“高处的物理内存”那组内核测试才有内容
```

`make test` 构建到 `build/<arch>-ktest/`，和普通构建（`build/<arch>/`）分开。每次运行用一块新做的磁盘，根文件系统在上面。`make test LIVE=1` 不接磁盘：根在内存里（从光盘启动时是这样），自检里和磁盘有关的几项跳过不算失败。

### make test 做了什么

内核不会自己关机，所以 `make test` 通过 `scripts/shell-test.sh` 来控制 QEMU：

1. 启动 QEMU，等日志里出现 `sh: ready`（以 `TEST_TIMEOUT` 为上限）。到这里内核测试和用户态自检都已经跑完。
2. 向串口输入一串命令，检查命令行的行为：运行程序、后台任务、Ctrl-C、`kill`、被终止的服务的端口能否重用、程序读键盘输入、重定向和管道、目录、引号、标准错误、脚本。x86 上最后还通过 QEMU 的监视器（`sendkey` 命令）在虚拟机的键盘上敲几行，检查键盘驱动（PS/2 的 `kbd`；`KBD=usb` 时 QEMU 把键送给 USB 键盘，检查的是 `usbkbd`）。最后让 console、uart 和键盘驱动依次像崩溃了一样退出（`selftest restart <名字>`），检查 init 重启它们之后输入照常。每一步等到预期的输出出现为止（每步最多 `STEP_TIMEOUT` 秒，默认 30）。
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

每次推送到 `main`、每个 PR，GitHub Actions 都会对三个架构各跑一遍 `make test ARCH=<arch>`（`.github/workflows/test.yml`），每个架构再带着两个 CPU 跑一遍（`SMP=2`），i686 再把磁盘接成 IDE 硬盘（`DISK_BUS=ide`）、接成 U 盘（`DISK_BUS=usb`）各跑一遍，不接磁盘跑一遍（`LIVE=1`），接上 USB 键盘跑两遍（`KBD=usb`，以及和 `DISK_BUS=usb` 一起：键盘和 U 盘在同一个 USB 2.0 控制器的口上），另外还有一个只跑 `make lib-test` 的任务。

- 用的是 macOS 的 runner 和 Homebrew 的交叉编译器（见 [开发环境搭建](setup.md) 的方法一），所以和在 macOS 上本地开发是同一套工具。加了构建依赖的话，workflow 的 `brew install` 一行也要加。
- 三个架构各是一个独立的任务，一个失败不影响另外两个跑完。
- 每次运行的 `test.log` 和 `shell-test.log` 作为 artifact 上传，失败时先下载它们来看。
- runner 比开发机慢，所以那里把等待的上限放宽了（`TEST_TIMEOUT=600`、`STEP_TIMEOUT=120`）；它们只是上限，不会让通过的运行变慢。

## 手动运行

`make run` 在 QEMU 里运行当前架构，带上磁盘 `disk-<arch>.img`（根文件系统在上面；每次运行前把系统自带的文件换成刚构建的，自己放进去的留着）和一块接 QEMU 用户网络的 virtio-net 网卡。控制台是串口，接到当前终端；命令行能做什么见 [启动映像和命令行](reference/shell.md)。x86 上内核的输出同时写到 VGA 屏幕：`make run QEMU_DISPLAY=cocoa`（macOS；Linux 上是 `gtk` 或 `sdl`）打开 QEMU 的窗口就能看到，在窗口里敲的键走 PS/2 键盘驱动。

直接调用 QEMU 的话，最少只需要 `-kernel`：

```bash
timeout 20 qemu-system-i386 -kernel build/i686/castor.bin -serial stdio -display none
timeout 20 qemu-system-x86_64 -kernel build/x86_64/castor32.elf -serial stdio -display none
timeout 20 qemu-system-aarch64 -M virt -cpu cortex-a72 -kernel build/arm64/castor.bin -serial stdio -display none
```

这样运行不带磁盘和网卡：blk、diskfs、net 会直接退出，根在内存里，selftest 跳过相关的检查。要带上就加（磁盘先用 `make disk` 做出来）：

```bash
# x86
-drive file=disk-i686.img,format=raw,if=none,id=disk0 -device virtio-blk-pci,drive=disk0
-netdev user,id=net0,guestfwd=tcp:10.0.2.100:7-cmd:cat -device virtio-net-pci,netdev=net0

# arm64：同上，设备名换成 virtio-blk-device / virtio-net-device
```

向 QEMU 的标准输入写入的内容经 uart 驱动和 console 服务送到 sh，所以也可以用管道喂命令；Ctrl-C 是字节 `0x03`，Ctrl-D 是 `0x04`。

## 在真机上运行

`qemu -kernel` 是 QEMU 自己把内核装进内存，真机上要有引导程序来做这件事。`make iso`（i686 或 x86_64）做出完整的系统映像 `build/<arch>/castor.iso`：前面是 GRUB 和内核，后面跟着一个 16MB 的分区，里面是根文件系统。需要 `grub-mkrescue` 和 `xorriso`（macOS：`brew install i686-elf-grub xorriso`）。它有三种用法：

- **写进 U 盘**：把映像原样写到 U 盘上（`dd if=build/i686/castor.iso of=/dev/<那个 U 盘>`，会覆盖整个 U 盘），插在机器的 USB 2.0 口上，从 U 盘启动。根在 U 盘的那个分区上，改的东西都留得下来；机器自己的硬盘不受影响。机器的固件不能从 U 盘启动的话，用光盘启动、U 盘插着：内核是从光盘来的，根照样在 U 盘上。
- **写进硬盘**：把映像原样写到机器的硬盘上（`dd if=build/i686/castor.iso of=/dev/<那块硬盘>`），从硬盘启动。根在硬盘的那个分区上，改的东西都留得下来。**这会覆盖整块硬盘，上面原有的系统和数据就没有了。** 硬盘要接在能运行 `dd` 的机器上写：拆下来接转接线，或者在那台机器上用别的系统的启动盘来写。
- **刻成光盘**：从光盘启动。光盘上的分区读不到（没有光驱的驱动），没有插着系统 U 盘的话根在内存里，用的是内核带着的那一份文件，什么都留不下来。这样启动不往硬盘上写任何东西，适合先看一眼硬件认得对不对。

`make run-iso` 在 QEMU 里把映像当硬盘启动（BIOS → 硬盘上的 GRUB → 内核，根在分区上；运行时改的东西写回映像文件），`make run-usb` 把它当 U 盘启动，`make run-cd` 把它当光盘启动；加 `QEMU_DISPLAY=cocoa` 看屏幕。写进真机之前先用它们各走一遍。

真机上能用的是屏幕（VGA 文本模式）、PS/2 键盘、USB 键盘、IDE 硬盘和 USB 2.0 口上的 U 盘。机器有串口的话串口控制台照常可用（COM1，38400 8N1），没有就自动不用。

**盘上原有的东西。** 系统启动时不往一块不是它自己的盘上写任何东西：`diskfs` 从不格式化，找不到自己的文件系统就退出；自检只在整块盘都是我们的文件系统时才做写入测试，否则只读。会写盘的只有 `disk write`（直接写扇区，不写编号时写的是第 0 块盘，机器有硬盘的话那就是硬盘），以及对根所在的那块盘上文件的正常修改。

当前的限制：只支持 BIOS 启动（UEFI 启动的机器没有 VGA 文本模式，屏幕上什么都看不到）；硬盘只认 IDE（SATA 和 NVMe 没有驱动）；U 盘要直接插在 USB 2.0 口上（见 [用户态驱动](reference/drivers.md#usb-20-和-u-盘) 的限制）；USB 键盘要直接插在机器的口上、开机前插好，机器的 USB 1.1 控制器要是 UHCI（见 [同一页](reference/drivers.md#usb-键盘usbkbd) 的限制）；根分区的大小是做映像时定的（`ROOT_SIZE_MB`，默认 16），不会随硬盘变大；系统里没有把自己装到硬盘上的命令；网卡只有 virtio 的驱动，真机上 `net` 找不到设备直接退出，没有网络；只在 QEMU 里验证过。

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
