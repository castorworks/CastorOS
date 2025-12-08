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
│   │   │   └── hal.c       # HAL implementation
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
- `src/arch/$(ARCH)/hal.c` - Architecture-specific implementation
- Use `hal_*` functions for portable code

### Header Organization

- Public headers: `src/include/<subsystem>/<file>.h`
- Arch-specific headers: `src/arch/$(ARCH)/include/`
- User-space headers: `user/lib/include/`

### Naming Conventions

- Kernel functions: `subsystem_action()` (e.g., `pmm_alloc_frame()`, `vfs_open()`)
- HAL functions: `hal_category_action()` (e.g., `hal_cpu_init()`, `hal_mmu_map()`)
- Test cases: `test_<name>` with `TEST_CASE()` macro
- Assembly files: `.asm` (NASM) or `.S` (GNU as for ARM64)

### Memory Layout (i686)

- Kernel virtual base: `0x80000000` (2GB)
- Kernel physical load: `0x100000` (1MB)
- Use `PHYS_TO_VIRT()` / `VIRT_TO_PHYS()` macros for address conversion
