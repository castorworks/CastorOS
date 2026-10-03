# Project Structure

## Directory Layout

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
│   │   │   └── hal.cpp       # HAL implementation
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
│   └── concepts/           # OS concept explanations
├── scripts/                # Build and utility scripts
├── build/                  # Build output (per-architecture)
│   └── $(ARCH)/            # e.g., build/i686/
├── Makefile                # Main build file
├── linker.ld               # i686 linker script
└── grub.cfg                # GRUB configuration
```

## Key Conventions

### HAL (Hardware Abstraction Layer)

- `src/include/hal/hal.h` - Unified interface for all architectures
- `src/arch/$(ARCH)/hal.cpp` - Architecture-specific implementation
- Use `hal_*` functions for portable code

### Header Organization

- Public headers: `src/include/<subsystem>/<file>.h`
- Arch-specific headers: `src/arch/$(ARCH)/include/`
- User-space headers: `user/lib/include/`

### Naming Conventions

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

### Memory Layout (i686)

- Kernel virtual base: `0x80000000` (2GB)
- Kernel physical load: `0x100000` (1MB)
- Use `PHYS_TO_VIRT()` / `VIRT_TO_PHYS()` macros for address conversion
