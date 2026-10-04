# AGENTS.md

本文件为 AI 编码助手（及人类贡献者）提供 CastorOS 的项目上下文与约定。

## Product Overview

CastorOS is an educational operating system designed for learning and experimentation.

- Hobby OS targeting i686 (x86 32-bit) with planned support for x86_64 and ARM64
- Higher-half kernel design with virtual address base at 0x80000000
- Multiboot-compliant bootloader support (GRUB)
- Written in freestanding C++20 with NASM / GNU as assembly for architecture-specific code

### Key Features

- Physical and virtual memory management with paging
- Preemptive multitasking with process/thread support
- VFS layer with FAT32, ramfs, devfs, procfs support
- User-mode execution with system calls
- Synchronization primitives (spinlocks, mutexes, semaphores)
- Network stack (Ethernet, IP, TCP, UDP, DHCP, DNS)
- Device drivers (VGA, keyboard, timer, ATA, PCI, E1000, USB/UHCI)

### Documentation Language

Project documentation is primarily in Chinese (简体中文). Code comments mix Chinese and English.
Historical feature specs (requirements / design / tasks) live in `docs/specs/`.

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

| Arch   | Toolchain Prefix | Assembler | Status      |
|--------|------------------|-----------|-------------|
| i686   | i686-elf-        | NASM      | Primary     |
| x86_64 | x86_64-elf-      | NASM      | Planned     |
| arm64  | aarch64-elf-     | GNU as    | Planned     |

### Common Commands

```bash
# Build kernel (default i686)
make

# Build for specific architecture
make ARCH=i686
make ARCH=x86_64
make ARCH=arm64

# Run in QEMU
make run

# Run without GUI
make run-silent

# Debug with GDB (waits for connection)
# Use gtimeout on macOS: gtimeout 30 make debug-silent
make debug

# Build bootable disk image
make disk

# Run from disk image (includes networking)
make run-disk

# Clean build artifacts
make clean          # Current arch only
make clean-all      # All architectures

# Generate compile_commands.json for IDE
make compile-db

# Show build configuration
make info
```

### User Space

User programs are in `user/` directory with their own Makefiles:

```bash
make shell    # Build user shell
make hello    # Build hello world
make tests    # Build user tests
```

### Testing

Kernel includes a built-in test framework (`ktest`). Tests run automatically during boot.

### Dependencies

- QEMU for emulation
- Cross-compiler toolchain (see `scripts/cross-compiler-install.sh`)
- GRUB tools for bootable images

## Project Structure

### Directory Layout

```
CastorOS/
├── src/                    # Kernel source code
│   ├── arch/               # Architecture-specific code
│   │   ├── i686/           # x86 32-bit implementation
│   │   │   ├── boot/       # Boot code (multiboot, early init)
│   │   │   ├── cpu/        # GDT, IDT setup
│   │   │   ├── interrupt/  # ISR, IRQ handlers
│   │   │   ├── mm/         # Paging implementation
│   │   │   ├── task/       # Context switching
│   │   │   ├── syscall/    # System call entry
│   │   │   └── hal.cpp     # HAL implementation
│   │   ├── x86_64/         # 64-bit x86 (placeholder)
│   │   └── arm64/          # ARM64 (placeholder)
│   ├── drivers/            # Device drivers
│   │   └── usb/            # USB subsystem
│   ├── fs/                 # File systems (VFS, FAT32, ramfs, etc.)
│   ├── kernel/             # Core kernel (task, syscall, shell)
│   │   ├── sync/           # Synchronization primitives
│   │   └── syscalls/       # System call implementations
│   ├── lib/                # Kernel library (kprintf, string, etc.)
│   ├── mm/                 # Memory management (PMM, VMM, heap)
│   ├── net/                # Network stack
│   ├── include/            # Header files (mirrors src/ structure)
│   └── tests/              # Kernel unit tests
├── user/                   # User-space programs
│   ├── lib/                # User-space C library
│   │   ├── include/        # POSIX-like headers
│   │   └── src/            # Library implementation
│   ├── shell/              # User shell
│   ├── helloworld/         # Example program
│   └── tests/              # User-space tests
├── docs/                   # Documentation (Chinese)
│   ├── concepts/           # OS concept explanations
│   └── specs/              # Historical feature specs
├── scripts/                # Build and utility scripts
├── build/                  # Build output (per-architecture)
│   └── $(ARCH)/            # e.g., build/i686/
├── Makefile                # Main build file
├── linker.ld               # i686 linker script
└── grub.cfg                # GRUB configuration
```

### Key Conventions

#### HAL (Hardware Abstraction Layer)

- `src/include/hal/hal.h` - Unified interface for all architectures
- `src/arch/$(ARCH)/hal.cpp` - Architecture-specific implementation
- Use `hal_*` functions for portable code

#### Header Organization

- Public headers: `src/include/<subsystem>/<file>.h`
- Arch-specific headers: `src/arch/$(ARCH)/include/`
- User-space headers: `user/lib/include/`

#### Naming Conventions

- Kernel subsystems: namespace + class, e.g. `mm::Pmm::alloc_frame()`, `fs::Vfs::open()`,
  `net::Tcp::input()`, `kernel::Scheduler::yield()`, `drivers::Timer::get_uptime_ms()`.
  Singleton modules use static member functions; `sync::Spinlock`/`Mutex`/`Semaphore` use real members.
- Data types with operations keep their functions as static members of the struct
  (`net::Netbuf::alloc()`, `net::Netdev::transmit(dev, buf)`, `fs::Blockdev::read(dev, ...)`,
  `kernel::FdTable::alloc(table, ...)`); pointer parameters stay NULL-tolerant.
- Polymorphism uses virtual interfaces with stateless singleton implementations:
  `fs::NodeOps` (VFS nodes), `fs::BlockdevOps` (block devices), `net::NetdevOps` (NICs).
- Prefer RAII guards (`sync::SpinlockIrqGuard`, `sync::MutexGuard`) over manual lock/unlock pairs.
- Still C-style free functions: syscall handlers (`sys_*`), `socket_*`, `interrupts_*`, kernel shell,
  `kprintf`/`klog`/string library, `kmalloc()`/`kfree()`, and all of user space (POSIX-style API).
- Inside a member function, call a same-named global function with `::name()` (unqualified names
  bind to the class member first).
- Do not declare functions with block-scope `extern` inside member functions; include the header.
- HAL: `hal::Category::action()` (e.g., `hal::Cpu::init()`, `hal::Mmu::map()`), selected per architecture at compile time
- Test cases: `test_<name>` with `TEST_CASE()` macro
- Assembly files: `.asm` (NASM) or `.S` (GNU as for ARM64)

#### Memory Layout (i686)

- Kernel virtual base: `0x80000000` (2GB)
- Kernel physical load: `0x100000` (1MB)
- Use `PHYS_TO_VIRT()` / `VIRT_TO_PHYS()` macros for address conversion

## 调试指南

### 超时配置

- 默认测试超时: 8 秒 (可通过 `TEST_TIMEOUT` 调整)
- 统一使用 `timeout` 命令 (macOS 需安装 coreutils)
- 输出限制: 200 行 (可通过 `OUTPUT_LINES` 调整)

### 快速测试命令

```bash
# 单架构测试 (推荐)
make test                      # 测试 i686 (默认)
make test ARCH=x86_64          # 测试 x86_64
make test ARCH=arm64           # 测试 ARM64

# 测试所有架构
make test-all

# 自定义超时
make test TEST_TIMEOUT=15      # 15秒超时
```

### 调试命令

```bash
# 捕获输出到文件
make debug-capture             # 输出保存到 build/$(ARCH)/debug.log

# GDB 调试 (等待连接)
make debug                     # 带 GUI
make debug-silent              # 无 GUI

# 手动调试命令 (参考，需先运行 make disk ARCH=xxx)
# i686: 使用 GRUB 磁盘镜像
timeout 8 qemu-system-i386 -hda build/i686/bootable.img -serial stdio -display none 2>&1 | head -200

# x86_64: 使用 GRUB 磁盘镜像
timeout 15 qemu-system-x86_64 -hda build/x86_64/bootable.img -serial stdio -display none 2>&1 | head -200

# arm64: 使用 -M virt 机器类型 (直接 -kernel)
timeout 8 qemu-system-aarch64 -M virt -cpu cortex-a72 -kernel build/arm64/castor.bin -nographic 2>&1 | head -200
```

### 构建验证

```bash
# 仅编译检查
make check                     # 当前架构
make build-all                 # 所有架构

# 查看配置
make info                      # 显示当前配置
make sources                   # 列出源文件
```

### 常见问题

1. **timeout 未找到**: `brew install coreutils` (macOS)
2. **交叉编译器未找到**: 运行 `scripts/cross-compiler-install.sh`
3. **QEMU 未找到**: `brew install qemu`

### 架构特定说明

| 架构 | QEMU | 启动方式 | 状态 |
|------|------|----------|------|
| i686 | qemu-system-i386 | -hda 磁盘镜像 | 主要 |
| x86_64 | qemu-system-x86_64 | -hda 磁盘镜像 | 开发中 |
| arm64 | qemu-system-aarch64 | -M virt -kernel | 开发中 |

**注意**: i686/x86_64 测试时会自动构建 GRUB 磁盘镜像。
