# Technology Stack

## Build System

- GNU Make with multi-architecture support
- Cross-compiler toolchain: `i686-elf-gcc`, `x86_64-elf-gcc`, `aarch64-elf-gcc`
- Assembler: NASM (x86) or GNU as (ARM64)
- Linker scripts: `linker.ld` (i686), `linker_x86_64.ld`, `linker_arm64.ld`

## Compiler Flags

```
CFLAGS = -std=gnu99 -ffreestanding -O0 -g -Wall -Wextra
```

- Freestanding environment (no standard library)
- Debug symbols enabled
- Strict warnings

## Target Architectures

| Arch   | Toolchain Prefix | Assembler | Status      |
|--------|------------------|-----------|-------------|
| i686   | i686-elf-        | NASM      | Primary     |
| x86_64 | x86_64-elf-      | NASM      | Planned     |
| arm64  | aarch64-elf-     | GNU as    | Planned     |

## Common Commands

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

## User Space

User programs are in `user/` directory with their own Makefiles:

```bash
make shell    # Build user shell
make hello    # Build hello world
make tests    # Build user tests
```

## Testing

Kernel includes a built-in test framework (`ktest`). Tests run automatically during boot.

## Dependencies

- QEMU for emulation
- Cross-compiler toolchain (see `scripts/cross-compiler-install.sh`)
- GRUB tools for bootable images
