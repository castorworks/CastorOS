# AGENTS.md

本文件为 AI 编码助手（及人类贡献者）提供 CastorOS 的项目上下文与约定。

## Product Overview

CastorOS is an educational microkernel for learning and experimentation.

- Targets i686, x86_64 and ARM64; all three build, boot and pass the kernel tests in QEMU
- The kernel contains only CPU/interrupt setup, memory management, scheduling, sync
  primitives and a 28-call syscall interface (process, memory, debug output, synchronous IPC,
  shared memory, uptime/timer, and I/O port / device memory / DMA / IRQ access for privileged
  user-space drivers)
- File systems, networking, device drivers and shells are **not** in the kernel; they are
  meant to come back as user-space modules (see `docs/microkernel.md`). Do not add them
  to `src/`.
- Higher-half kernel (i686 virtual base 0x80000000)
- Written in freestanding C++20 with NASM / GNU as assembly for architecture-specific code

### Documentation Language

Project documentation is primarily in Chinese (简体中文). Code comments mix Chinese and English.
`docs/microkernel.md` describes the current structure and `docs/concepts/` explains the
mechanisms behind it. `docs/history/` is a development log written before the microkernel
cut; its code listings no longer match the tree.

## Technology Stack

### Build System

- GNU Make with multi-architecture support
- Language: freestanding C++20 (no exceptions, no RTTI); assembly for boot/entry stubs
- Cross-compiler toolchain: `i686-elf-g++`, `x86_64-elf-g++`, `aarch64-elf-g++`
- Assembler: NASM (x86) or GNU as (ARM64)
- Linker scripts: `linker.ld` (i686), `linker_x86_64.ld`, `linker_arm64.ld`

### Compiler Flags

```
CXXFLAGS = -std=gnu++20 -ffreestanding -O0 -g -Wall -Wextra \
           -fno-exceptions -fno-rtti -fno-threadsafe-statics
```

- Freestanding environment (no standard library; minimal C++ runtime in `src/lib/cxxrt.cpp`)
- Symbols shared with assembly must be declared `extern "C"`
- Debug symbols enabled
- Strict warnings

### Target Architectures

| Arch   | Toolchain Prefix | Assembler | Boot                          |
|--------|------------------|-----------|-------------------------------|
| i686   | i686-elf-        | NASM      | Multiboot1, `qemu -kernel`    |
| x86_64 | x86_64-elf-      | NASM      | Multiboot1 via `castor32.elf` |
| arm64  | aarch64-elf-     | GNU as    | `-M virt -kernel`, DTB        |

### Common Commands

```bash
make                    # Build kernel (default i686); also builds and embeds user/init
make ARCH=x86_64
make ARCH=arm64
make build-all

make run                # Run in QEMU, serial console on stdio; attaches disk.img (created on
                        # first use) and a virtio-net card on QEMU user networking
make debug              # Same, waiting for GDB on :1234

make test               # Build with in-kernel tests (KTEST=1), boot, and check kernel tests + selftest + shell checks
make test-all

make clean              # Current arch only
make clean-all          # Everything, including user/ builds
make compile-db         # compile_commands.json (needs compiledb)
make info
```

### User Space

`user/lib` is the user library (syscall wrappers, printf, string). `user/init` is the first
process; its ELF is embedded into the kernel image by `src/kernel/init_image.S` (`.incbin`),
so there is no disk image. init starts the modules and is the name server (`names.h` in
`user/lib`). Resident modules are embedded into init the same way (`user/init/modules.S`):
`user/uart` (privileged serial input driver; protocol and the `console_read`/`read_line`
client in `console.h`), `user/blk` (privileged virtio-blk driver,
protocol and client in `blk.h`), `user/net` (privileged virtio-net driver plus a small
ARP/IPv4/ICMP/UDP/TCP stack with a DHCP client; protocol and client in `net.h`), `user/ramfs` (in-memory file
service), `user/diskfs`
(persistent file service on top of blk; files are addressed with a `disk:` prefix) and
`user/sh` (command line). Both file services share the protocol in `fs.h` and the server
skeleton in `fs_server.h`; virtio drivers share `virtio.h`; servers that take a shared buffer
from each client use `clients.h`. Other programs (`user/selftest`, `user/ls`, `user/cat`,
`user/cp`, `user/rm`, `user/echo`, `user/write`, `user/grep`, `user/wc`, `user/disk`, `user/ping`, `user/ifconfig`, `user/dns`,
`user/http`, `user/echod`, `user/sleep`, `user/hello`) go into the boot image: a ustar archive of `user/bootfs/*` plus the programs
in `BOOT_PROGRAMS` (`user/ramfs/Makefile`), embedded in ramfs and unpacked at startup. sh
runs them with fork + exec, and runs the `rc` file (which starts `selftest`) at boot.
To add a program, create `user/<name>/` and add it to `BOOT_PROGRAMS`. Every user program's
Makefile just sets `TARGET`/`SOURCES` and includes `user/program.mk`. The kernel Makefile
rebuilds all of it when `user/` changes.

### Testing

Kernel tests (`src/tests`, `ktest` framework) are compiled in only with `KTEST=1` and run during
boot, before init starts. `make test` builds into `build/<arch>-ktest/`. User-space behaviour
is checked in two places: `user/selftest` (runs inside the system from `rc`; add checks there
for anything a program can observe) and `scripts/shell-test.sh` (drives the command line over
the serial port from the host; add checks there for anything that needs typed input, such as
job control). Patterns in shell-test.sh must not assume a line starts at column 0 unless the
shell is known to be idle: output of background programs follows the `> ` prompt.

### Dependencies

- QEMU for emulation
- Cross-compiler toolchain (see `scripts/cross-compiler-install.sh`)

## Project Structure

### Directory Layout

```
CastorOS/
├── src/                    # Kernel source code
│   ├── arch/               # Architecture-specific code
│   │   ├── i686/           # boot, cpu (GDT/IDT), interrupt, mm, task, syscall, hal.cpp
│   │   ├── x86_64/
│   │   └── arm64/          # also dtb/ (device tree parsing)
│   ├── drivers/            # Only serial (debug output) and timer (tick)
│   │   ├── x86/            # COM1, PIT
│   │   └── arm/            # PL011, ARM Generic Timer
│   ├── kernel/             # task.cpp (scheduler), syscall.cpp, ipc.cpp, user_irq.cpp, elf.cpp, ...
│   │   ├── sync/           # The spinlock
│   │   └── syscalls/       # process.cpp, mm.cpp
│   ├── lib/                # Kernel library (kprintf, klog, string, cxxrt)
│   ├── mm/                 # Memory management (PMM, VMM, heap)
│   ├── include/            # Header files (mirrors src/ structure)
│   └── tests/              # Kernel unit tests (KTEST=1)
├── user/                   # User-space programs
│   ├── lib/                # User library
│   ├── init/               # First user process: starts modules, name service
│   ├── uart/               # Serial input driver (privileged module)
│   ├── blk/                # virtio-blk driver (privileged module): virtio-pci on x86, virtio-mmio on arm64
│   ├── net/                # Network service (privileged module): virtio-net + ARP/IPv4/ICMP/UDP/TCP
│   ├── diskfs/             # Persistent file service on the block device (unprivileged module)
│   ├── ramfs/              # In-memory file service (unprivileged module), holds the boot image
│   ├── sh/                 # Command line (unprivileged module): runs programs, background jobs, Ctrl-C
│   ├── selftest/           # User-space self-checks, in the boot image, run from rc at boot
│   ├── ls/ cat/ cp/ rm/ echo/ write/ grep/ wc/ sleep/ disk/ ping/ ifconfig/ dns/ http/ echod/ hello/   # Programs in the boot image
│   ├── bootfs/             # Static files for the boot image (rc, readme.txt)
│   ├── program.mk          # Shared build rules for user programs
│   └── linker/             # User linker scripts
├── docs/                   # Documentation (Chinese)
├── scripts/                # cross-compiler-install.sh, shell-test.sh (used by make test)
├── build/                  # Build output: build/<arch>/, build/<arch>-ktest/
├── Makefile
└── linker.ld, linker_x86_64.ld, linker_arm64.ld
```

### Key Conventions

#### HAL (Hardware Abstraction Layer)

- `src/include/hal/hal.h` - Unified interface for all architectures
- `src/arch/$(ARCH)/hal.cpp` - Architecture-specific implementation
- Use `hal_*` functions for portable code
- Page tables: the walk, map/unmap/protect, and creating, cloning (COW) and destroying address
  spaces are written once in `src/mm/pagetable.cpp`. Each architecture only describes its entry
  format through the functions in `src/include/hal/pt.h` (implemented at the end of
  `src/arch/<arch>/mm/*.cpp`). Do not add a per-architecture table walk; extend `pt.h` instead.
- Register names and the layout of the saved syscall frame appear only in
  `src/arch/<arch>/task/user_context.cpp` (`hal::UserContext`: initial context of a task, the
  child's context on fork, redirecting the syscall return on exec). `task.cpp`, `process.cpp` and
  `loader.cpp` have no `#if ARCH_*`. An address space is always passed around as the physical
  address of its top-level table (`uintptr_t`), never as a pointer to it.
- `src/mm/vmm.cpp` has no `#if ARCH_*`: address-space creation, cloning (COW), teardown, extending
  the kernel's direct mapping and the i686 kernel-mapping sync are `hal::Mmu` functions implemented
  per architecture. New architecture-dependent memory code goes behind a `hal::Mmu` function, not
  into a conditional in generic code.

#### Header Organization

- Public headers: `src/include/<subsystem>/<file>.h`
- Arch-specific headers: `src/arch/$(ARCH)/include/`
- User-space headers: `user/lib/include/`

#### Naming Conventions

- Kernel subsystems: namespace + class, e.g. `mm::Pmm::alloc_frame()`,
  `kernel::Scheduler::yield()`, `syscall::Process::fork()`, `drivers::Timer::get_uptime_ms()`.
  Singleton modules use static member functions; `sync::Spinlock` uses real members.
- Prefer the RAII guard `sync::SpinlockIrqGuard` over manual lock/unlock pairs. The spinlock is the
  only lock in the kernel: nothing needed a mutex or a semaphore, so they were removed.
- Kernel code that nothing calls gets deleted together with its tests, not kept "for later". To
  find it: build with `-ffunction-sections -fdata-sections` and link with
  `--gc-sections --print-gc-sections`; whatever the linker would drop is unreachable.
- Still C-style free functions: syscall wrappers (`sys_*_wrapper`), `kprintf`/`klog`/string library,
  `kmalloc()`/`kfree()`, and all of user space (POSIX-style API).
- User programs are built without FP/SIMD (`-mno-sse` / `-mgeneral-regs-only`): the kernel does not
  save those registers across context switches.
- Program arguments travel through the argument page at the top of the user stack region
  (`USER_ARGS_ADDR` / `user_args_t` in `kernel/task.h`, mirrored in `user/lib/src/crt0.cpp`).
- Usage text and error messages go to standard error with `eprintf`, never `printf`: with
  `cmd > file` or a pipe they must still reach the screen.
- Programs write output with `printf`/`write_out` and read input with `read_line`/`read_input`
  (`stdio.h`), never with `console_write`/`console_read` directly, so that `cmd > file` and
  `cmd1 | cmd2` work. Standard input/output is a user-library concept: sh passes a hidden last
  argument (starting with `\x01`) that `crt0` strips; pipes are plain synchronous IPC between
  the two programs. The kernel knows nothing about it.
- Syscall numbers live in `src/include/kernel/syscall.h` and must match `user/lib/include/syscall.h`.
- Inside a member function, call a same-named global function with `::name()` (unqualified names
  bind to the class member first).
- Do not declare functions with block-scope `extern` inside member functions; include the header.
- HAL: `hal::Category::action()` (e.g., `hal::Cpu::init()`, `hal::Mmu::map()`), selected per architecture at compile time
- Test cases: `test_<name>` with `TEST_CASE()` macro. A test module only runs its cases; the runner
  (`run_all_tests`) resets the counters before each module and prints its summary afterwards. Do
  not call `unittest_init()` / `unittest_print_summary()` in a module, and register every new
  module in `test_runner.cpp` — a module that is not listed there never runs.
- Assembly files: `.asm` (NASM) or `.S` (GNU as for ARM64)

#### Memory Layout (i686)

- Kernel virtual base: `0x80000000` (2GB)
- Kernel physical load: `0x100000` (1MB)
- Use `PHYS_TO_VIRT()` / `VIRT_TO_PHYS()` macros for address conversion

## 调试指南

### 测试

```bash
make test                      # i686：构建 KTEST=1 内核并运行，命令行检查做完即结束（通常十几秒）
make test ARCH=x86_64
make test ARCH=arm64
make test-all
make test TEST_TIMEOUT=300     # 机器很忙时放宽上限（默认 180 秒）
make test ARCH=x86_64 QEMU_MEMORY=3G   # 给虚拟机更多内存（默认是 QEMU 的 128MB）；
                               # 超过 1GB 时"高处的物理内存"那组内核测试才有内容
```

内核不会自己关机：`make test` 通过 `scripts/shell-test.sh` 启动 QEMU，等日志里出现 `sh: ready`
（以 `TEST_TIMEOUT` 为上限），然后向串口输入一串命令检查命令行的行为（运行程序、后台任务、
Ctrl-C、`kill`、被终止的服务的端口能否重用、程序读键盘输入、重定向和管道、引号、标准错误、脚本），每一步等到预期的输出出现为止（每步最多
`STEP_TIMEOUT` 秒，默认 30），做完就结束 QEMU。完整日志写到 `build/<arch>-ktest/test.log`，
命令行检查的结果写到 `build/<arch>-ktest/shell-test.log`（每项一行 `shelltest: <名字>: ok|FAILED`）。
最后汇总各模块的 `Total/Passed/Failed tests` 计数；有失败用例、用户态没有起来、用户态自检
没有通过（没有 `selftest: all passed`）、自检跳过了任何一项（测试环境里磁盘、网卡、回显服务
都在）或者命令行检查没有全部通过时返回非零。

主机负载极高时（load 上百），QEMU 可能几十秒没有任何输出，自检里有时间上限的检查也可能超时；
先看 `uptime` 再判断是不是真的坏了。

### 手动运行

```bash
# 下面的命令不带磁盘和网卡（blk、diskfs、net 会直接退出，selftest 跳过相关的检查）；要带上就加：
#   x86:   -drive file=disk.img,format=raw,if=none,id=disk0 -device virtio-blk-pci,drive=disk0
#          -netdev user,id=net0,guestfwd=tcp:10.0.2.100:7-cmd:cat -device virtio-net-pci,netdev=net0
#   arm64: 同上，设备名换成 virtio-blk-device / virtio-net-device
# 控制台是串口；向 QEMU 的标准输入写入的内容经 uart 驱动送到 sh
# （help、jobs、kill <pid> 是内置命令；其余如 ls、cat <file>、write <file> [text] 是程序；
#   行尾加 & 后台运行，Ctrl-C（0x03）终止前台程序；前台程序运行期间输入归它，
#   行首的 Ctrl-D（0x04）表示输入结束；cmd < in > out 2> err、cmd >> out、cmd1 | cmd2、
#   "带 空格 的参数" 可用；文本文件当作脚本执行，$1-$9 是参数）
timeout 20 qemu-system-i386 -kernel build/i686/castor.bin -serial stdio -display none
timeout 20 qemu-system-x86_64 -kernel build/x86_64/castor32.elf -serial stdio -display none
timeout 20 qemu-system-aarch64 -M virt -cpu cortex-a72 -kernel build/arm64/castor.bin -serial stdio -display none

# GDB
make debug                     # QEMU 等待连接，另一个终端: gdb build/i686/castor.bin -ex 'target remote :1234'
```

### 构建验证

```bash
make check                     # 当前架构
make build-all                 # 所有架构
make info                      # 显示当前配置
make sources                   # 列出源文件
```

### 常见问题

1. **手动运行用的 timeout 未找到**: `brew install coreutils` (macOS；`make test` 本身不需要)
2. **交叉编译器未找到**: 运行 `scripts/cross-compiler-install.sh`
3. **QEMU 未找到**: `brew install qemu`
4. **QEMU 无输出或卡住**: 先看主机负载（`uptime`），不要并行跑构建和测试
