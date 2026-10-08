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
number of system calls (32) is stated in this file, `docs/microkernel.md`,
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
- Only for `make iso`: `grub-mkrescue` and `xorriso` (`brew install i686-elf-grub xorriso`)
- A host C++17 compiler (`c++`) for `tools/mkdiskfs`, built automatically by `make run` / `make test`

### Common Commands

```bash
make                    # Build kernel (default i686); also builds and embeds user/init
make ARCH=x86_64
make ARCH=arm64
make build-all

make run                # Run in QEMU, serial console on stdio; attaches disk-<arch>.img (the root
                        # file system: system files refreshed on every run, your own files kept)
                        # and a virtio-net card on QEMU user networking
make run QEMU_DISPLAY=cocoa   # Same with QEMU's window: the VGA screen, keys typed there go to the PS/2 keyboard driver (x86)
make run KBD=usb QEMU_DISPLAY=cocoa   # x86: also attach a USB keyboard (KBD=hub: behind a hub); keys typed in the window then go to usbkbd
make debug              # Same, waiting for GDB on :1234
make iso                # System image for a real PC (x86; needs grub-mkrescue and xorriso): GRUB and
                        # the kernel, followed by a partition holding the root file system. Written
                        # to a hard disk as is, the machine boots from it with its root on that disk
make run-iso            # Boot that image in QEMU as a hard disk: BIOS -> GRUB -> kernel, root on the partition
make run-cd             # Boot it as a CD: the partition cannot be read, the root is in memory
make run-usb            # Boot it from a USB stick: BIOS -> GRUB -> kernel, root on the stick

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
│   ├── drivers/            # Only debug output (serial; on x86 also the screen) and timer (tick)
│   │   ├── x86/            # COM1, VGA text screen, PIT
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
│   ├── console/            # Terminal input service (module, no hardware): takes characters from uart, kbd and usbkbd, decides who gets them
│   ├── uart/               # Serial input driver (module, allowed the serial port): hands characters to console
│   ├── kbd/                # PS/2 keyboard driver (module, allowed the keyboard controller; x86 only): hands characters to console
│   ├── usbkbd/             # USB keyboard driver (module, allowed the UHCI controllers; x86 only): uhci.cpp (USB 1.1 host controller), usbkbd.cpp (ports, hubs, HID boot keyboard): hands characters to console
│   ├── blk/                # Block device driver (module, allowed the disk): blk.cpp (server), virtio_blk.cpp (virtio-pci on x86, virtio-mmio on arm64); x86 only: ata.cpp (IDE disk), ehci.cpp (USB 2.0 host controller) + usb_storage.cpp (USB stick)
│   ├── net/                # Network service (module, allowed the network card): nic.cpp (the card; virtio_net.cpp, and on x86 e1000.cpp for Intel gigabit cards, behind `struct nic` in nic.h), ip.cpp (Ethernet/ARP/IPv4/ICMP), udp.cpp, tcp.cpp, dhcp.cpp, net.cpp (main loop)
│   ├── diskfs/             # File service on the block device (module, no hardware): the root file system
│   ├── ramfs/              # In-memory file service (module, no hardware): /tmp, and the whole tree (from the boot image it holds) when there is no root on disk
│   ├── sh/                 # Command line (module, no hardware): runs programs, background jobs, Ctrl-C
│   ├── selftest/           # User-space self-checks, in /bin, run from /etc/rc at boot
│   ├── ls/ cat/ cp/ rm/ mv/ mkdir/ echo/ write/ grep/ wc/ sleep/ clear/ disk/ ping/ ifconfig/ dns/ http/ echod/ hello/   # Programs, installed in /bin
│   ├── bootfs/             # Static files of the system's tree (etc/rc, usr/share/doc/)
│   ├── program.mk          # Shared build rules for user programs
│   ├── arch.mk             # Compiler and flags shared by the user library and all programs
│   └── linker/             # User linker scripts
├── docs/                   # Documentation (Chinese), see the Documentation section above
├── scripts/                # cross-compiler-install.sh, shell-test.sh (used by make test)
├── tools/                  # Host-side build tools: mkdiskfs.cpp (makes the root file system image)
├── build/                  # Build output: build/<arch>/, build/<arch>-ktest/
├── Makefile
└── linker.ld, linker_x86_64.ld, linker_arm64.ld
```

### User Space

**Library and init.** `user/lib` is the user library (syscall wrappers, printf, string, math).
`user/init` is the first process; its ELF is embedded into the kernel image by
`src/kernel/init_image.S` (`.incbin`), so the kernel needs no disk to get there. init starts the modules and is
the name server (`names.h` in `user/lib`).

**Resident modules** are embedded into init the same way (`user/init/modules.S`):

- `user/console`: terminal input service, no hardware; protocol and the
  `console_read`/`read_line` client in `console.h`. Input drivers hand it characters with
  `console_input` (`CONSOLE_INPUT`), so keyboard and serial input take the same path from there
- `user/uart`: serial input driver; exits on a machine without a serial port
- `user/kbd` (x86 only): PS/2 keyboard driver; translates scancodes to characters
- `user/usbkbd` (x86 only): USB keyboard driver on the machine's UHCI controllers (up to 4, all
  in this one process). USB devices come and go and UHCI raises no interrupt for that, so it
  looks at every port four times a second; that is also how it gets a keyboard that the EHCI
  driver in `blk` released to the companion controller after boot. Hubs are handled as more
  ports: the three port operations (look, reset, disable) exist once for a controller's own
  ports and once for a hub's, and everything above them does not tell the two apart. `blk`
  releases high-speed hubs to the companion controller too, so all hubs end up here. Standard USB requests and
  descriptor types shared with `blk` are in `usb.h`; how Shift, Caps Lock and Ctrl act on a key
  is shared with `kbd` in `keys.h`. While it waits (`uhci_sleep`) it keeps acknowledging
  interrupts: its line is usually shared with other drivers and must not stay masked.
- `user/blk`: block device driver, virtio-blk or (x86, when there is no virtio disk) the IDE
  disk on the first channel and a USB stick on a USB 2.0 port; protocol and client in `blk.h`.
  It can serve several disks at once, numbered from 0 (`blk_select`). The devices are behind
  `struct disk` (`user/blk/disk.h`). Several devices live in this one process, so a backend
  never calls `ipc_recv` itself to wait for its interrupt: it registers a handler with
  `disk_on_irq` right after `irq_claim` and waits with `disk_wait`, which hands each interrupt
  to the device it belongs to. PCI configuration space access for init is in `pci.h`.
- `user/net`: network card driver (virtio-net, or on x86 without one an Intel 82540-family
  gigabit card) plus a small ARP/IPv4/ICMP/UDP/TCP stack with a DHCP client;
  protocol and client in `net.h`
- `user/ramfs`: in-memory file service
- `user/diskfs`: file service on top of blk. It never formats: the file system is made at build
  time by `tools/mkdiskfs` (on-disk format in `diskfs_format.h`, shared by both). It looks for
  the file system at sector 0 and in the MBR partitions, and exits without writing anything
  when there is none. selftest writes to the raw disk only when the whole disk is our file
  system. Keep it that way: on a real machine a disk that is not ours is someone's data, and
  nothing that runs at boot may write to it.

**One directory tree.** `/bin` (programs), `/etc/rc`, `/usr/share/doc`, `/home`, `/tmp`. The tree
is laid out once at build time (`user/ramfs/Makefile`, from `user/bootfs/` plus `BOOT_PROGRAMS`)
and used twice: as the root file system image on disk, and as the boot image embedded in ramfs.
Which service a path belongs to is decided in the client library (`user/lib/src/fs.cpp`): with
our file system on the disk the root is diskfs and only `/tmp` is ramfs; without it (no disk, a
foreign disk, booted from CD) everything is ramfs. The library asks once, with
`name_settle("diskfs")`, which waits until diskfs has registered or exited, so the answer does
not depend on timing. There are no `disk:` / `ram:` prefixes.
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

**Programs in /bin.** Other programs (`user/selftest`, `user/ls`, `user/cat`,
`user/cp`, `user/rm`, `user/mv`, `user/mkdir`, `user/echo`, `user/write`, `user/grep`, `user/wc`,
`user/clear`, `user/disk`, `user/ping`, `user/ifconfig`, `user/dns`, `user/http`, `user/echod`, `user/sleep`,
`user/hello`) are the programs in `BOOT_PROGRAMS` (`user/ramfs/Makefile`); they are installed in
`/bin` of the tree. sh looks a bare command name up in `/bin` (a program or script elsewhere
needs a path, e.g. `./tool`), runs it with fork + exec, and runs `/etc/rc` (which starts
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
  it. So code under it needs no locks of its own, and "interrupts off" still means "nobody else".
  The lock follows the task: `schedule()` records how many levels the outgoing task holds
  (`task_t::lock_depth`) and the incoming task restores its own count (`KernelLock::adopt` in
  `finish_switch`), so a task may sleep while "holding" it without blocking other CPUs. The idle
  tasks never hold it. Anything that is "the current X" (task, idle task, interrupt depth, page table; on x86
  also the GDT, TSS and system-call stack) is per CPU, indexed by `hal::Cpu::id()`; do not add a
  global for such state. The lock is taken in C: `syscall_dispatcher`, the x86 `irq*_handler` /
  `isr*_handler` wrappers and `arm64_exception_handler`. A new entry path into the kernel must
  do the same, and anything it touches before that (the x86_64 `syscall_entry` stub) must be
  per CPU. A new user task does not leave the kernel through those paths the first time: it
  starts in `user_task_start` (`sched.cpp`), which calls `finish_switch` like any other task that
  has just been switched in.
- The scheduler does not rely on the kernel lock. Its own lock (`task_lock`, `task_private.h`,
  always taken with interrupts off) protects the run queue and, for every task, `state`, `on_cpu`,
  the wait fields, the IPC fields and the pending kernel messages (`irq_pending`,
  `timer_pending`). Change a task's state only under it, and make a task runnable only with
  `sched_make_ready_locked`: a task whose kernel stack is still in use (`on_cpu`) must not enter
  the run queue — the CPU switching it out enqueues it in `finish_switch`. To wait: under the
  lock check the condition and mark yourself `BLOCKED`, unlock, then `schedule()`. Never touch
  user memory or call anything that takes another lock (kmalloc, VMM, kprintf) while holding it.
- Some system calls run without the kernel lock (`syscall_unlocked` in `src/kernel/syscall.cpp`;
  today `mmap`, `munmap`, `brk`, the four IPC calls and a few read-only ones). Locked is the
  default and the safe choice. To move a call out, its whole path may touch only: the caller's own state that nobody
  else reads or writes concurrently; subsystems with a lock of their own (PMM, VMM page-table
  operations, the kernel heap, console output, the scheduler); and globals where a stale read is
  harmless. Think about what other processes, holding the kernel lock, can do
  to the caller meanwhile (that is why `mem_grant` maps into a separate address range from the
  target's own `mmap`). Conversely, code on those paths — everything under `src/mm/`,
  `kprintf`/`klog` — can no longer assume the kernel lock is held: protect new shared state
  there with its own lock.
- `kernel::Scheduler` is implemented in two files: `sched.cpp` (run queue, idle task, `schedule()`,
  timer tick, yield/sleep/block/wakeup) and `task.cpp` (task table, creating and exiting processes,
  kill, privilege queries). What they share is in `src/kernel/task_private.h`; besides them only
  `ipc.cpp` and `user_irq.cpp` include it, because they change task states under the scheduler
  lock.
- Prefer the RAII guard `sync::SpinlockIrqGuard` over manual lock/unlock pairs. The spinlock is the
  only lock in the kernel: nothing needed a mutex or a semaphore, so they were removed.
- Kernel code that nothing calls gets deleted together with its tests, not kept "for later". To
  find it: build with `-ffunction-sections -fdata-sections` and link with
  `--gc-sections --print-gc-sections`; whatever the linker would drop is unreachable.
- The kernel must not touch floating-point or SIMD registers — it is built with `-mno-sse` /
  `-mgeneral-regs-only` and they are not saved on kernel entry — so no `float`/`double` in `src/`.
  User programs may use them: the scheduler saves and restores those registers whenever it
  switches user tasks (`hal::UserContext::fp_save` / `fp_restore`, state in `task_t::fp_state`).
- Console output (`kprintf`, and the `console_write` system call behind user `printf`) goes to
  the serial port and, on x86, also to the VGA text screen (`drivers::Screen`,
  `src/drivers/x86/screen.cpp`). The screen is output only; keyboard input is `user/kbd` and `user/usbkbd`. It
  understands three escape sequences (colour, cursor position, clear screen) and drops the rest.
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
- **`user/selftest`** runs inside the system from `/etc/rc`; add checks there for anything a program
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
make test DISK_BUS=ide         # x86: attach the disk as an IDE drive instead of virtio-blk (also for make run)
make test DISK_BUS=usb         # x86: attach it as a USB stick on an EHCI controller
make test NET=e1000            # x86: an Intel gigabit card instead of virtio-net (also for make run)
make test KBD=usb              # x86: attach a USB keyboard (UHCI); the typed keys then exercise usbkbd instead of kbd.
                               # With DISK_BUS=usb the keyboard and the stick share one EHCI controller with a companion
make test KBD=hub              # x86: the same with the keyboard behind a USB hub
make test LIVE=1               # no disk: the root is in memory; the disk-related selftest checks may be skipped
make test ARCH=x86_64 QEMU_MEMORY=3G   # more memory for the VM (default is QEMU's 128MB); the
                               # "high physical memory" kernel tests only have content above 1GB
```

The kernel does not power off by itself: `make test` starts QEMU through
`scripts/shell-test.sh`, waits for `sh: ready` in the log (at most `TEST_TIMEOUT`), then types a
series of commands into the serial port to check the command line (running programs, background
jobs, Ctrl-C, `kill`, whether the port of a killed service can be reused, programs reading
keyboard input, redirection and pipes, directories, quoting, standard error, scripts, Tab
completion; on x86 also
a few lines typed on the VM's PS/2 keyboard through the QEMU monitor's `sendkey`; at the end
console, uart and the keyboard driver are made to exit with `selftest restart <name>` and input must work
again after init restarts them). Each step waits until
the expected output appears (at most `STEP_TIMEOUT` seconds per step, default 30), and QEMU is
stopped when the steps are done.

- Full log: `build/<arch>-ktest/test.log`
- Shell check results: `build/<arch>-ktest/shell-test.log`, one line per check,
  `shelltest: <name>: ok|FAILED`

At the end the `Total/Passed/Failed tests` counts of all modules are summed. `make test` returns
non-zero if a test case failed, user space did not come up, the selftest did not pass (no
`selftest: all passed`), the selftest skipped anything (the test environment has the disk, the
network card and the echo service; with `LIVE=1` the "no disk" skips are allowed), or not all
shell checks passed. Each run boots from a freshly made 4MB root disk (`build/<arch>-ktest/test-disk.img`).

When the host is extremely loaded (load in the hundreds), QEMU may print nothing for tens of
seconds and selftest checks with a time limit may time out; look at `uptime` before deciding
something is really broken. Do not run builds and tests in parallel.

### CI

Every push to `main` and every pull request runs `make test` for each architecture on GitHub
Actions (`.github/workflows/test.yml`, three jobs on macOS runners with the Homebrew cross
compilers), plus `make lib-test`. The logs of each run (`test.log`, `shell-test.log`) are
uploaded as artifacts. Each architecture also runs with `SMP=2`, and i686 once more with
`DISK_BUS=ide`, `DISK_BUS=usb`, `LIVE=1`, `KBD=usb`, `DISK_BUS=usb KBD=hub` and `NET=e1000`. If you add a build dependency, add it to the workflow's `brew install`
line too.

### Running by Hand

```bash
# These commands attach no disk and no network card (blk, diskfs and net exit at once, the root is
# in memory, selftest skips the related checks). To attach them (make disk builds the image), add:
#   x86:   -drive file=disk-i686.img,format=raw,if=none,id=disk0 -device virtio-blk-pci,drive=disk0
#          -netdev user,id=net0,guestfwd=tcp:10.0.2.100:7-cmd:cat -device virtio-net-pci,netdev=net0
#   arm64: the same, with the device names virtio-blk-device / virtio-net-device
timeout 20 qemu-system-i386 -kernel build/i686/castor.bin -serial stdio -display none
timeout 20 qemu-system-x86_64 -kernel build/x86_64/castor32.elf -serial stdio -display none
timeout 20 qemu-system-aarch64 -M virt -cpu cortex-a72 -kernel build/arm64/castor.bin -serial stdio -display none

# GDB
make debug                     # QEMU waits for a connection; in another terminal: gdb build/i686/castor.bin -ex 'target remote :1234'
```

The console is the serial port; whatever is written to QEMU's standard input reaches sh through
the uart driver and the console service. On x86 the same output is also on the VGA screen and the PS/2 keyboard works
(`-display cocoa` instead of `-display none` to see it). In sh:

- `help`, `jobs` and `kill <pid>` are built in; everything else (`ls`, `cat <file>`,
  `write <file> [text]`, ...) is a program.
- A trailing `&` runs a program in the background; Ctrl-C (0x03) kills the foreground program.
  While a foreground program runs, input belongs to it; Ctrl-D (0x04) at the start of a line
  means end of input.
- `cmd < in > out 2> err`, `cmd >> out`, `cmd1 | cmd2` and `"arguments with spaces"` work.
- Tab completes the word at the end of the line: a command name (builtins and `/bin`), a path,
  a directory after `cd`, a job's PID after `kill`. A second Tab lists the candidates.
- A text file is run as a script; `$1`-`$9` are its arguments.

### Troubleshooting

1. **`timeout` not found (used for manual runs)**: `brew install coreutils` (macOS; `make test`
   itself does not need it)
2. **Cross compiler not found**: `brew install i686-elf-gcc x86_64-elf-gcc aarch64-elf-gcc` on
   macOS, `scripts/cross-compiler-install.sh` on Ubuntu/Debian
3. **QEMU not found**: `brew install qemu`
4. **QEMU prints nothing or hangs**: check the host load first (`uptime`); do not run builds and
   tests in parallel
