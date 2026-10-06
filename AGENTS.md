# AGENTS.md

Project context and conventions for AI coding assistants and human contributors working on
CastorOS. This file is written in English; everything under `docs/` is in Chinese.

## Product Overview

CastorOS is an educational microkernel for learning and experimentation.

- Targets i686, x86_64 and ARM64; all three build, boot and pass the kernel tests in QEMU
- The kernel contains only CPU/interrupt setup, memory management, scheduling, sync
  primitives and a 32-call syscall interface (process, memory, debug output, synchronous IPC,
  shared memory, uptime/timer, and I/O port / device memory / DMA / IRQ access, platform
  device lookup and per-device hardware permissions for user-space drivers)
- File systems, networking, device drivers and shells are **not** in the kernel; they are
  user-space modules (see `docs/microkernel.md`). Do not add them to `src/`.
- Higher-half kernel (i686 virtual base 0x80000000)
- Written in freestanding C++20 with NASM / GNU as assembly for architecture-specific code

## Documentation

Project documentation is in Chinese (简体中文). Code comments mix Chinese and English.

| Where | What |
|-------|------|
| `README.md` | Entry point: what it is, quick start, directory list |
| `docs/README.md` | Index of all documents, and the writing conventions |
| `docs/setup.md` | Toolchain and QEMU installation |
| `docs/testing.md` | What `make test` does, logs, CI, manual QEMU runs, GDB |
| `docs/microkernel.md` | Current structure: kernel boundary, boot, init and modules, adding a module |
| `docs/reference/` | One page per part: `syscalls.md`, `ipc.md`, `hardware.md`, `drivers.md`, `fs.md`, `net.md`, `shell.md`, `floating-point.md` |
| `docs/concepts/` | The mechanisms behind it (boot, paging, interrupts, scheduling, ...) |

All of them describe the current tree and are kept in sync with the code; there is no archive
of outdated documents (the old development log, `docs/history/`, was deleted and lives in git
history only). When a change alters behaviour, limits or an interface, update the matching page in
`docs/reference/` (each section ends with its known limits, introduced by “当前的限制”). The
number of system calls (31) is stated in this file, `docs/microkernel.md`,
`docs/reference/syscalls.md`, `docs/README.md` and `docs/concepts/07-system-calls.md`; change
them together. Writing conventions (one paragraph per line, no hard wraps in Chinese text, one fact
in one place) are listed at the end of `docs/README.md`.

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
           -fno-exceptions -fno-rtti -fno-threadsafe-statics \
           -fno-asynchronous-unwind-tables -fno-unwind-tables
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

### Dependencies

- QEMU for emulation
- Cross-compiler toolchain, GCC 10 or newer for `-std=gnu++20`: Homebrew on macOS,
  `scripts/cross-compiler-install.sh` (builds all three targets from source) on Ubuntu/Debian

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
make lib-test           # Host-side tests of the user library (no cross compiler or QEMU)

make check              # Build the current arch and show the kernel image
make clean              # Current arch only
make clean-all          # Everything, including user/ builds
make compile-db         # compile_commands.json (needs compiledb)
make info               # Current configuration
make sources            # List source files
```

## Project Structure

### Directory Layout

```
CastorOS/
├── src/                    # Kernel source code
│   ├── arch/               # Architecture-specific code
│   │   ├── i686/           # boot, cpu (GDT/IDT), interrupt, mm, task, syscall, hal.cpp
│   │   ├── x86_64/
│   │   └── arm64/          # also dtb/ (device tree parser: memory, GIC, timer, UART, device list)
│   ├── drivers/            # Only serial (debug output) and timer (tick)
│   │   ├── x86/            # COM1, PIT
│   │   └── arm/            # PL011, ARM Generic Timer
│   ├── kernel/             # sched.cpp (scheduler), task.cpp (task table, process lifecycle), smp.cpp (kernel lock, starting the other CPUs), syscall.cpp, ipc.cpp, user_irq.cpp, hw_access.cpp, elf.cpp, ...
│   │   ├── sync/           # The spinlock
│   │   └── syscalls/       # process.cpp, mm.cpp
│   ├── lib/                # Kernel library (kprintf, klog, string, cxxrt)
│   ├── mm/                 # Memory management (PMM, VMM, heap)
│   ├── include/            # Header files (mirrors src/ structure)
│   └── tests/              # Kernel unit tests (KTEST=1)
├── user/                   # User-space programs
│   ├── lib/                # User library
│   ├── init/               # First user process (the only privileged one): starts modules, assigns devices, name service
│   ├── uart/               # Serial input driver (module, allowed the serial port)
│   ├── blk/                # virtio-blk driver (module, allowed the disk): virtio-pci on x86, virtio-mmio on arm64
│   ├── net/                # Network service (module, allowed the network card): nic.cpp (virtio-net), ip.cpp (Ethernet/ARP/IPv4/ICMP), udp.cpp, tcp.cpp, dhcp.cpp, net.cpp (main loop)
│   ├── diskfs/             # Persistent file service on the block device (module, no hardware)
│   ├── ramfs/              # In-memory file service (module, no hardware), holds the boot image
│   ├── sh/                 # Command line (module, no hardware): runs programs, background jobs, Ctrl-C
│   ├── selftest/           # User-space self-checks, in the boot image, run from rc at boot
│   ├── ls/ cat/ cp/ rm/ mv/ mkdir/ echo/ write/ grep/ wc/ sleep/ disk/ ping/ ifconfig/ dns/ http/ echod/ hello/   # Programs in the boot image
│   ├── bootfs/             # Static files for the boot image (rc, readme.txt, docs/)
│   ├── program.mk          # Shared build rules for user programs
│   ├── arch.mk             # Compiler and flags shared by the user library and all programs
│   └── linker/             # User linker scripts
├── docs/                   # Documentation (Chinese), see the Documentation section above
├── scripts/                # cross-compiler-install.sh, shell-test.sh (used by make test)
├── build/                  # Build output: build/<arch>/, build/<arch>-ktest/
├── Makefile
└── linker.ld, linker_x86_64.ld, linker_arm64.ld
```

### User Space

**Library and init.** `user/lib` is the user library (syscall wrappers, printf, string, math).
`user/init` is the first process; its ELF is embedded into the kernel image by
`src/kernel/init_image.S` (`.incbin`), so there is no disk image. init starts the modules and is
the name server (`names.h` in `user/lib`).

**Resident modules** are embedded into init the same way (`user/init/modules.S`):

- `user/uart`: serial input driver; protocol and the `console_read`/`read_line` client in
  `console.h`
- `user/blk`: virtio-blk driver; protocol and client in `blk.h`
- `user/net`: virtio-net driver plus a small ARP/IPv4/ICMP/UDP/TCP stack with a DHCP client;
  protocol and client in `net.h`
- `user/ramfs`: in-memory file service
- `user/diskfs`: persistent file service on top of blk; files are addressed with a `disk:` prefix
- `user/sh`: command line

**Privilege and devices.** Only init is privileged. Every module drops privilege before it
starts; for a driver, init first finds its device (fixed ports or a PCI scan on x86,
`device_find("<compatible>", index, &info)` on arm64, which answers from the device tree) and
records its ports or device memory and its interrupt line in the child's allow-list with
`hw_allow` (the `allow_*` functions in `user/init/init.cpp`; `virtio_allow` in `virtio.h`).
init also watches the modules: one that exits after it registered its service name is started
again (device re-granted, name handed over), at most 5 times. Client libraries drop their
connection when a request fails and look the service up again on the next one, so a service
must be able to start from nothing and a client must tolerate one failed request. A driver does not look for its device and does not hard-code an address: it reads what it was
allowed with `hw_find` (`virtio_open` for virtio devices) and can touch nothing else. On x86 the
ports a process is allowed are opened in the TSS I/O permission bitmap while it runs, so
`io_read`/`io_write` in the user library execute `in`/`out` directly for those ports and only
fall back to the system call for the rest.

**Shared code.** Both file services share the protocol in `fs.h` and the server skeleton in
`fs_server.h`, which also owns the directory rules (a backend only stores a flat table of full
paths). The current directory, relative paths, `.` and `..` are resolved in the client library
(`user/lib/src/fs.cpp`); a server only ever sees full paths from the root; virtio drivers
share `virtio.h`; servers that take a shared buffer from each client use `clients.h`.

**Programs in the boot image.** Other programs (`user/selftest`, `user/ls`, `user/cat`,
`user/cp`, `user/rm`, `user/mv`, `user/mkdir`, `user/echo`, `user/write`, `user/grep`, `user/wc`,
`user/disk`, `user/ping`, `user/ifconfig`, `user/dns`, `user/http`, `user/echod`, `user/sleep`,
`user/hello`) go into the boot image: a ustar archive of `user/bootfs/` (subdirectories become
directories) plus the programs in `BOOT_PROGRAMS` (`user/ramfs/Makefile`), embedded in ramfs and
unpacked at startup. sh runs them with fork + exec, and runs the `rc` file (which starts
`selftest`) at boot.

**Build.** To add a program, create `user/<name>/` and add it to `BOOT_PROGRAMS`. Every user
program's Makefile just sets `TARGET`/`SOURCES` and includes `user/program.mk`. Compiler and
flags for all of user space (library and programs) are in `user/arch.mk`; change them there,
nowhere else. The kernel Makefile rebuilds all of it when `user/` changes.

## Conventions

### HAL (Hardware Abstraction Layer)

- `src/include/hal/hal.h` - Unified interface for all architectures
- `src/arch/$(ARCH)/hal.cpp` - Architecture-specific implementation
- Portable code goes through the HAL: `hal::Category::action()` (e.g., `hal::Cpu::init()`,
  `hal::Mmu::map()`), selected per architecture at compile time. A few C-style helpers
  (`hal_arch_name()`, the `hal_*_barrier()` functions) are in `hal.h` as well.
- Page tables: the walk, map/unmap/protect, and creating, cloning (COW) and destroying address
  spaces are written once in `src/mm/pagetable.cpp`. Each architecture only describes its entry
  format through the functions in `src/include/hal/pt.h` (implemented at the end of
  `src/arch/<arch>/mm/*.cpp`). Do not add a per-architecture table walk; extend `pt.h` instead.
- Register names and the layout of the saved syscall frame appear only in
  `src/arch/<arch>/task/user_context.cpp` (`hal::UserContext`: initial context of a task, the
  child's context on fork, redirecting the syscall return on exec, the FP/SIMD state) and
  `src/arch/<arch>/syscall/` (`hal::Syscall::arg6`). The types themselves (`cpu_context_t`,
  `hal_fp_state_t`) and the user address-space limits are in `src/arch/<arch>/include/task_context.h`,
  which `kernel/task.h` includes. `task.cpp`, `sched.cpp`, `process.cpp`, `loader.cpp`,
  `syscall.cpp` and `kernel/task.h` have no `#if ARCH_*`. An address space is always passed around
  as the physical address of its top-level table (`uintptr_t`), never as a pointer to it.
- `src/mm/vmm.cpp` has no `#if ARCH_*`: address-space creation, cloning (COW), teardown, extending
  the kernel's direct mapping and the i686 kernel-mapping sync are `hal::Mmu` functions implemented
  per architecture. New architecture-dependent memory code goes behind a `hal::Mmu` function, not
  into a conditional in generic code.

### Header Organization

- Public headers: `src/include/<subsystem>/<file>.h`
- Arch-specific headers: `src/arch/$(ARCH)/include/`
- User-space headers: `user/lib/include/`

### Naming and Code Style

- Kernel subsystems: namespace + class, e.g. `mm::Pmm::alloc_frame()`,
  `kernel::Scheduler::yield()`, `syscall::Process::fork()`, `drivers::Timer::get_uptime_ms()`.
  Singleton modules use static member functions; `sync::Spinlock` uses real members.
- Still C-style free functions: syscall wrappers (`sys_*_wrapper`), `kprintf`/`klog`/string library,
  `kmalloc()`/`kfree()`, and all of user space (POSIX-style API).
- Inside a member function, call a same-named global function with `::name()` (unqualified names
  bind to the class member first).
- Do not declare functions with block-scope `extern` inside member functions; include the header.
- Assembly files: `.asm` (NASM) or `.S` (GNU as for ARM64)

### Kernel

- Several CPUs (`docs/reference/smp.md`), on all three architectures. The rule that keeps the rest
  of the kernel unchanged: a CPU holds the kernel lock (`kernel::KernelLock`, `src/kernel/smp.cpp`)
  whenever it executes kernel code — taken on entry from user mode, released before returning to
  it and while the idle task waits. So kernel data needs no locks of its own, and "interrupts off"
  still means "nobody else". The lock belongs to the CPU, not the task: it is held across a context
  switch. Anything that is "the current X" (task, idle task, interrupt depth, page table; on x86
  also the GDT, TSS and system-call stack) is per CPU, indexed by `hal::Cpu::id()`; do not add a
  global for such state. The lock is taken in C: `syscall_dispatcher`, the x86 `irq*_handler` /
  `isr*_handler` wrappers and `arm64_exception_handler`. A new entry path into the kernel must
  do the same, and anything it touches before that (the x86_64 `syscall_entry` stub) must be
  per CPU. A new user task does not leave the kernel through those paths the first time: it
  starts in `user_task_start` (`sched.cpp`), which releases the lock.
- Some system calls run without the kernel lock (`syscall_unlocked` in `src/kernel/syscall.cpp`;
  today `mmap`, `munmap`, `brk` and a few read-only ones). Locked is the default and the safe
  choice. To move a call out, its whole path may touch only: the caller's own state that nobody
  else reads or writes concurrently; subsystems with a lock of their own (PMM, VMM page-table
  operations, the kernel heap, console output); and globals where a stale read is harmless. It
  must not sleep or schedule. Think about what other processes, holding the kernel lock, can do
  to the caller meanwhile (that is why `mem_grant` maps into a separate address range from the
  target's own `mmap`). Conversely, code on those paths — everything under `src/mm/`,
  `kprintf`/`klog` — can no longer assume the kernel lock is held: protect new shared state
  there with its own lock.
- `kernel::Scheduler` is implemented in two files: `sched.cpp` (run queue, idle task, `schedule()`,
  timer tick, yield/sleep/block/wakeup) and `task.cpp` (task table, creating and exiting processes,
  kill, privilege queries). What they share is in `src/kernel/task_private.h`; nothing else
  includes it.
- Prefer the RAII guard `sync::SpinlockIrqGuard` over manual lock/unlock pairs. The spinlock is the
  only lock in the kernel: nothing needed a mutex or a semaphore, so they were removed.
- Kernel code that nothing calls gets deleted together with its tests, not kept "for later". To
  find it: build with `-ffunction-sections -fdata-sections` and link with
  `--gc-sections --print-gc-sections`; whatever the linker would drop is unreachable.
- The kernel must not touch floating-point or SIMD registers — it is built with `-mno-sse` /
  `-mgeneral-regs-only` and they are not saved on kernel entry — so no `float`/`double` in `src/`.
  User programs may use them: the scheduler saves and restores those registers whenever it
  switches user tasks (`hal::UserContext::fp_save` / `fp_restore`, state in `task_t::fp_state`).
- Syscall numbers live in `src/include/kernel/syscall.h` and must match `user/lib/include/syscall.h`.
- Program arguments travel through the argument page at the top of the user stack region
  (`USER_ARGS_ADDR` / `user_args_t` in `kernel/task.h`, mirrored in `user/lib/src/crt0.cpp`).
- Memory layout (i686): kernel virtual base `0x80000000` (2GB), kernel physical load `0x100000`
  (1MB). Use the `PHYS_TO_VIRT()` / `VIRT_TO_PHYS()` macros for address conversion.

### User Programs

- Usage text and error messages go to standard error with `eprintf`, never `printf`: with
  `cmd > file` or a pipe they must still reach the screen.
- Programs write output with `printf`/`write_out` and read input with `read_line`/`read_input`
  (`stdio.h`), never with `console_write`/`console_read` directly, so that `cmd > file` and
  `cmd1 | cmd2` work. Standard input/output is a user-library concept: sh passes a hidden last
  argument (starting with `\x01`) that `crt0` strips; pipes are plain synchronous IPC between
  the two programs. The kernel knows nothing about it.

## Testing

`docs/testing.md` has the same material for human readers, in Chinese.

### Where Checks Go

- **Kernel tests** (`src/tests`, `ktest` framework) are compiled in only with `KTEST=1` and run
  during boot, before init starts. `make test` builds into `build/<arch>-ktest/`.
  Test cases are `test_<name>` with the `TEST_CASE()` macro. A test module only runs its cases;
  the runner (`run_all_tests`) resets the counters before each module and prints its summary
  afterwards. Do not call `unittest_init()` / `unittest_print_summary()` in a module, and register
  every new module in `src/tests/framework/test_runner.cpp` — a module that is not listed there never runs.
- **`user/selftest`** runs inside the system from `rc`; add checks there for anything a program
  can observe.
- **`scripts/shell-test.sh`** drives the command line over the serial port from the host; add
  checks there for anything that needs typed input, such as job control. Patterns in
  shell-test.sh must not assume a line starts at column 0 unless the shell is known to be idle:
  output of background programs follows the `> ` prompt.
- **Host-side library tests**: the parts of `user/lib` that do not need the kernel (printf
  family, string and math functions) are also tested on the host. `make lib-test` compiles them
  with the host compiler and runs `user/lib/tests/lib_test.cpp` in a couple of seconds, with no
  cross compiler and no QEMU. Add checks there for pure library code; it is the fastest loop
  there is.

### Running Tests

```bash
make test                      # i686: build the KTEST=1 kernel and run it; ends when the shell checks are done (usually ten-odd seconds)
make test ARCH=x86_64
make test ARCH=arm64
make test-all                  # all three architectures; non-zero if any of them fails
make test TEST_TIMEOUT=300     # raise the limit on a busy machine (default 180 seconds)
make test ARCH=arm64 SMP=4     # give the VM 4 CPUs (default 1, at most 8; all three architectures)
make test ARCH=x86_64 QEMU_MEMORY=3G   # more memory for the VM (default is QEMU's 128MB); the
                               # "high physical memory" kernel tests only have content above 1GB
```

The kernel does not power off by itself: `make test` starts QEMU through
`scripts/shell-test.sh`, waits for `sh: ready` in the log (at most `TEST_TIMEOUT`), then types a
series of commands into the serial port to check the command line (running programs, background
jobs, Ctrl-C, `kill`, whether the port of a killed service can be reused, programs reading
keyboard input, redirection and pipes, directories, quoting, standard error, scripts). Each step waits until
the expected output appears (at most `STEP_TIMEOUT` seconds per step, default 30), and QEMU is
stopped when the steps are done.

- Full log: `build/<arch>-ktest/test.log`
- Shell check results: `build/<arch>-ktest/shell-test.log`, one line per check,
  `shelltest: <name>: ok|FAILED`

At the end the `Total/Passed/Failed tests` counts of all modules are summed. `make test` returns
non-zero if a test case failed, user space did not come up, the selftest did not pass (no
`selftest: all passed`), the selftest skipped anything (the test environment has the disk, the
network card and the echo service), or not all shell checks passed.

When the host is extremely loaded (load in the hundreds), QEMU may print nothing for tens of
seconds and selftest checks with a time limit may time out; look at `uptime` before deciding
something is really broken. Do not run builds and tests in parallel.

### CI

Every push to `main` and every pull request runs `make test` for each architecture on GitHub
Actions (`.github/workflows/test.yml`, three jobs on macOS runners with the Homebrew cross
compilers), plus `make lib-test`. The logs of each run (`test.log`, `shell-test.log`) are
uploaded as artifacts. If you add a build dependency, add it to the workflow's `brew install`
line too.

### Running by Hand

```bash
# These commands attach no disk and no network card (blk, diskfs and net exit at once, selftest
# skips the related checks). To attach them, add:
#   x86:   -drive file=disk.img,format=raw,if=none,id=disk0 -device virtio-blk-pci,drive=disk0
#          -netdev user,id=net0,guestfwd=tcp:10.0.2.100:7-cmd:cat -device virtio-net-pci,netdev=net0
#   arm64: the same, with the device names virtio-blk-device / virtio-net-device
timeout 20 qemu-system-i386 -kernel build/i686/castor.bin -serial stdio -display none
timeout 20 qemu-system-x86_64 -kernel build/x86_64/castor32.elf -serial stdio -display none
timeout 20 qemu-system-aarch64 -M virt -cpu cortex-a72 -kernel build/arm64/castor.bin -serial stdio -display none

# GDB
make debug                     # QEMU waits for a connection; in another terminal: gdb build/i686/castor.bin -ex 'target remote :1234'
```

The console is the serial port; whatever is written to QEMU's standard input reaches sh through
the uart driver. In sh:

- `help`, `jobs` and `kill <pid>` are built in; everything else (`ls`, `cat <file>`,
  `write <file> [text]`, ...) is a program.
- A trailing `&` runs a program in the background; Ctrl-C (0x03) kills the foreground program.
  While a foreground program runs, input belongs to it; Ctrl-D (0x04) at the start of a line
  means end of input.
- `cmd < in > out 2> err`, `cmd >> out`, `cmd1 | cmd2` and `"arguments with spaces"` work.
- A text file is run as a script; `$1`-`$9` are its arguments.

### Troubleshooting

1. **`timeout` not found (used for manual runs)**: `brew install coreutils` (macOS; `make test`
   itself does not need it)
2. **Cross compiler not found**: `brew install i686-elf-gcc x86_64-elf-gcc aarch64-elf-gcc` on
   macOS, `scripts/cross-compiler-install.sh` on Ubuntu/Debian
3. **QEMU not found**: `brew install qemu`
4. **QEMU prints nothing or hangs**: check the host load first (`uptime`); do not run builds and
   tests in parallel
