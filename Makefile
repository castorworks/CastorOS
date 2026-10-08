# CastorOS Makefile
# ============================================================================
# 快速参考:
#   make                    # 构建 i686 内核（内嵌 user/init）
#   make ARCH=arm64         # 构建 ARM64 内核
#   make run                # 在 QEMU 中运行（串口控制台）
#   make test               # 构建带内核测试的版本并运行（内核测试、用户态自检、命令行检查）
#   make build-all          # 构建所有架构
# ============================================================================

ARCH ?= i686
VALID_ARCHS := i686 x86_64 arm64

ifeq ($(filter $(ARCH),$(VALID_ARCHS)),)
$(error Invalid ARCH=$(ARCH). Valid options: $(VALID_ARCHS))
endif

# KTEST=1: 把 src/tests 编进内核，启动时运行（make test 会自动设置）
KTEST ?= 0

# 测试最多等多少秒。内核不会自己关机：命令行一就绪测试就结束，这只是卡住时的上限
TEST_TIMEOUT ?= 180

# ============================================================================
# 架构特定工具链配置
# ============================================================================

ifeq ($(ARCH),i686)
    CC = i686-elf-gcc
    CXX = i686-elf-g++
    LD = i686-elf-ld
    AS = nasm
    OBJCOPY = i686-elf-objcopy
    ARCH_CFLAGS = -m32
    ARCH_LDFLAGS = -T linker.ld -nostdlib
    ARCH_ASFLAGS = -f elf32 -g -F dwarf
    ARCH_DEFINE = -DARCH_I686
    QEMU = qemu-system-i386
    DRIVER_DIR = x86
else ifeq ($(ARCH),x86_64)
    CC = x86_64-elf-gcc
    CXX = x86_64-elf-g++
    LD = x86_64-elf-ld
    AS = nasm
    OBJCOPY = x86_64-elf-objcopy
    ARCH_CFLAGS = -m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2
    ARCH_LDFLAGS = -T linker_x86_64.ld -nostdlib
    ARCH_ASFLAGS = -f elf64 -g -F dwarf
    ARCH_DEFINE = -DARCH_X86_64
    QEMU = qemu-system-x86_64
    DRIVER_DIR = x86
else ifeq ($(ARCH),arm64)
    CC = aarch64-elf-gcc
    CXX = aarch64-elf-g++
    LD = aarch64-elf-ld
    AS = aarch64-elf-as
    OBJCOPY = aarch64-elf-objcopy
    ARCH_CFLAGS = -mcpu=cortex-a72 -mgeneral-regs-only
    ARCH_LDFLAGS = -T linker_arm64.ld -nostdlib
    ARCH_ASFLAGS =
    ARCH_DEFINE = -DARCH_ARM64
    QEMU = qemu-system-aarch64
    DRIVER_DIR = arm
    QEMU_MACHINE = -M virt -cpu cortex-a72
endif

# ============================================================================
# 编译标志
# ============================================================================

COMMON_FLAGS = -ffreestanding -O0 -g -Wall -Wextra \
         -Isrc/include -Isrc/arch/$(ARCH)/include \
         $(ARCH_CFLAGS) $(ARCH_DEFINE)
ifeq ($(KTEST),1)
    COMMON_FLAGS += -DKTEST
endif
# CFLAGS 仅用于预处理/汇编 .S 文件
CFLAGS = $(COMMON_FLAGS)
CXXFLAGS = -std=gnu++20 $(COMMON_FLAGS) \
           -fno-exceptions -fno-rtti -fno-threadsafe-statics \
           -fno-asynchronous-unwind-tables -fno-unwind-tables
# 自动生成头文件依赖 (.d)
DEPFLAGS = -MMD -MP
LDFLAGS = $(ARCH_LDFLAGS)
ASFLAGS = $(ARCH_ASFLAGS)

# ============================================================================
# 目录与输出
# ============================================================================

SRC_DIR = src
ARCH_DIR = $(SRC_DIR)/arch/$(ARCH)
ifeq ($(KTEST),1)
    BUILD_DIR = build/$(ARCH)-ktest
else
    BUILD_DIR = build/$(ARCH)
endif

KERNEL = $(BUILD_DIR)/castor.bin

# QEMU 的 -kernel 只接受 32 位 multiboot ELF：x86_64 内核转一份 ELF32 外壳
ifeq ($(ARCH),x86_64)
    BOOT_IMAGE = $(BUILD_DIR)/castor32.elf
else
    BOOT_IMAGE = $(KERNEL)
endif

# 第一个用户进程，以 .incbin 嵌入内核 (src/kernel/init_image.S)
INIT_ELF = user/init/build/$(ARCH)/init.elf
INIT_DEPS = $(wildcard user/program.mk user/arch.mk user/linker/*.ld $(shell find user/bootfs -type f) \
              user/*/Makefile user/*/*.cpp user/*/*.h user/*/*.S \
              user/lib/src/*.cpp user/lib/src/arch/$(ARCH)/*.S user/lib/include/*.h)

# ============================================================================
# 源文件
# ============================================================================

C_SOURCES = $(wildcard $(SRC_DIR)/kernel/*.cpp) \
    $(wildcard $(SRC_DIR)/kernel/sync/*.cpp) \
    $(wildcard $(SRC_DIR)/kernel/syscalls/*.cpp) \
    $(wildcard $(SRC_DIR)/mm/*.cpp) \
    $(wildcard $(SRC_DIR)/lib/*.cpp) \
    $(wildcard $(SRC_DIR)/drivers/$(DRIVER_DIR)/*.cpp) \
    $(wildcard $(ARCH_DIR)/*.cpp) \
    $(wildcard $(ARCH_DIR)/*/*.cpp)

ifeq ($(KTEST),1)
    C_SOURCES += $(wildcard $(SRC_DIR)/tests/framework/*.cpp) \
        $(wildcard $(SRC_DIR)/tests/pbt/*.cpp) \
        $(wildcard $(SRC_DIR)/tests/lib/*.cpp) \
        $(wildcard $(SRC_DIR)/tests/mm/*.cpp) \
        $(wildcard $(SRC_DIR)/tests/kernel/*.cpp) \
        $(wildcard $(SRC_DIR)/tests/arch/*.cpp) \
        $(wildcard $(SRC_DIR)/tests/arch/$(ARCH)/*.cpp)
endif

ifeq ($(ARCH),arm64)
    ASM_SOURCES = $(wildcard $(ARCH_DIR)/*/*.S)
    ASM_OBJECTS = $(patsubst $(SRC_DIR)/%.S, $(BUILD_DIR)/%.o, $(ASM_SOURCES))
else
    ASM_SOURCES = $(wildcard $(ARCH_DIR)/*/*.asm)
    ASM_OBJECTS = $(patsubst $(SRC_DIR)/%.asm, $(BUILD_DIR)/%.o, $(ASM_SOURCES))
endif

C_OBJECTS = $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(C_SOURCES))
INIT_OBJECT = $(BUILD_DIR)/kernel/init_image.o

OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS) $(INIT_OBJECT)

# ============================================================================
# 构建
# ============================================================================

.PHONY: all clean clean-all disk run debug iso run-iso run-cd run-usb test run-test test-all build-all check init info sources compile-db help

all: $(BOOT_IMAGE)

$(KERNEL): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)
	@echo "✓ CastorOS kernel built: $(KERNEL)"

$(BUILD_DIR)/castor32.elf: $(KERNEL)
	$(OBJCOPY) -O elf32-i386 $< $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(INIT_ELF): $(INIT_DEPS)
	@$(MAKE) -C user/init ARCH=$(ARCH)

$(INIT_OBJECT): $(SRC_DIR)/kernel/init_image.S $(INIT_ELF)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DINIT_IMAGE='"$(INIT_ELF:.elf=.stripped.elf)"' -c $< -o $@

init: $(INIT_ELF)

-include $(OBJECTS:.o=.d)

build-all:
	@for arch in $(VALID_ARCHS); do \
		echo "━━━ Building $$arch ━━━"; \
		$(MAKE) ARCH=$$arch || exit 1; \
	done
	@echo "✓ All architectures built successfully"

check: $(BOOT_IMAGE)
	@ls -lh $(KERNEL)

# ============================================================================
# 运行 / 调试 / 测试（控制台是串口，接到终端）
# ============================================================================

# QEMU_MEMORY=4G 之类：给虚拟机的内存（不写就是 QEMU 的默认值 128MB），run 和 test 都认
# SMP=2 之类：给虚拟机几个 CPU（不写就是 1 个，内核最多用 8 个）
# QEMU_DISPLAY=cocoa 之类：打开 QEMU 的窗口（不写就没有窗口）。x86 上窗口里是 VGA 屏幕，
# 在窗口里敲的键走 PS/2 键盘驱动（KBD=usb 时走 USB 键盘驱动）；arm64 没有屏幕
QEMU_DISPLAY ?= none
QEMU_BASE = $(QEMU) $(QEMU_MACHINE) $(if $(QEMU_MEMORY),-m $(QEMU_MEMORY)) $(if $(SMP),-smp $(SMP)) -kernel $(BOOT_IMAGE) -serial stdio -display $(QEMU_DISPLAY)

# virtio 设备：x86 挂在 PCI 上，arm64 挂在 virtio-mmio 上
ifeq ($(ARCH),arm64)
    VIRTIO_BLK = virtio-blk-device
else
    VIRTIO_BLK = virtio-blk-pci
endif
# DISK_BUS=ide 或 usb（只有 x86）：磁盘作为 IDE 硬盘、或者插在 USB 2.0 口上的 U 盘接上，
# 而不是 virtio-blk；run 和 test 都认。真机上是这两种，用它们来跑那两个驱动
# KBD=usb（只有 x86）：再接一个 USB 键盘；run 和 test 都认。之后在 QEMU 窗口里敲的键（和
# make test 敲的键）到的是它，不是 PS/2 键盘。它插在一个 USB 1.1 控制器的口上；和
# DISK_BUS=usb 一起用时，键盘和 U 盘插在同一个 USB 2.0 控制器的口上，键盘由它的伙伴控制器
# 接手：真机上是这样的（usb_version=1：QEMU 的键盘默认是高速设备，真键盘不是）
QEMU_EHCI = usb-ehci,id=ehci
ifeq ($(KBD),usb)
ifeq ($(DISK_BUS)$(LIVE),usb)
QEMU_EHCI = ich9-usb-ehci1,id=ehci -device ich9-usb-uhci1,masterbus=ehci.0,firstport=0
QEMU_KBD = -device usb-kbd,bus=ehci.0,usb_version=1
else
QEMU_KBD = -device piix3-usb-uhci,id=uhci -device usb-kbd,bus=uhci.0
endif
endif
ifeq ($(DISK_BUS),ide)
qemu_disk = -drive file=$(1),format=raw,if=ide
else ifeq ($(DISK_BUS),usb)
qemu_disk = -device $(QEMU_EHCI) -drive file=$(1),format=raw,if=none,id=disk0 -device usb-storage,bus=ehci.0,drive=disk0
else
qemu_disk = -drive file=$(1),format=raw,if=none,id=disk0 -device $(VIRTIO_BLK),drive=disk0
endif

# virtio-net 网卡，接 QEMU 的用户网络（来宾 10.0.2.15，网关 10.0.2.2，DNS 10.0.2.3）
ifeq ($(ARCH),arm64)
    VIRTIO_NET = virtio-net-device
else
    VIRTIO_NET = virtio-net-pci
endif
# guestfwd：来宾连 10.0.2.100:7 时 QEMU 启动一个 cat，得到一个回显服务（selftest 用它测 TCP）
QEMU_NET = -netdev user,id=net0,guestfwd=tcp:10.0.2.100:7-cmd:cat -device $(VIRTIO_NET),netdev=net0

# 根文件系统在磁盘上。整个系统的文件树（/bin、/etc……）是构建用户态时在 $(ROOT_TREE) 里
# 摆好的（user/ramfs/Makefile），tools/mkdiskfs 把它做成磁盘文件系统的映像。
ROOT_TREE = user/ramfs/build/$(ARCH)/bootfs
MKDISKFS = build/host/mkdiskfs
HOSTCXX ?= c++

$(MKDISKFS): tools/mkdiskfs.cpp user/lib/include/diskfs_format.h
	@mkdir -p $(dir $@)
	$(HOSTCXX) -std=c++17 -O1 -Wall -Wextra -o $@ $<

# make run 用的磁盘：每个架构一个（上面的程序是那个架构的），make clean 不删它。每次运行前
# 把系统自带的文件换成刚构建的，自己放进去的文件留着
DISK ?= disk-$(ARCH).img
DISK_SIZE_MB ?= 16
# make test 用的磁盘：每次重新做（4MB，小到 selftest 可以把它写满）
TEST_DISK = $(BUILD_DIR)/test-disk.img
TEST_DISK_SIZE_MB = 4
# make test 在 x86 上还要在虚拟机的键盘上敲键：通过 QEMU 的监视器（一对管道，
# scripts/shell-test.sh 创建），arm64 没有键盘
ifneq ($(ARCH),arm64)
    TEST_MONITOR = $(BUILD_DIR)/monitor
endif

QEMU_RUN = $(QEMU_BASE) $(call qemu_disk,$(DISK)) $(QEMU_NET) $(QEMU_KBD)

disk: $(BOOT_IMAGE) $(MKDISKFS)
	@$(MKDISKFS) $(DISK) $(DISK_SIZE_MB) $(ROOT_TREE)

run: disk
	$(QEMU_RUN)

# 等待 GDB 连接 (target remote :1234)
debug: disk
	$(QEMU_RUN) -s -S

# ============================================================================
# 系统映像（真机用）
# ============================================================================
# qemu -kernel 是 QEMU 自己把内核装进内存；真机上要有引导程序来做这件事。make iso 做出
# 完整的系统映像：前面是 GRUB 和内核（一个可以引导的 ISO），后面跟着一个分区，里面是根文件
# 系统。原样写进硬盘（dd），用 BIOS 方式启动的 PC 就从它启动，根在那个分区上。
# 刻成光盘也能启动，但光盘上的分区读不到（没有光驱的驱动），那时根在内存里。
# 需要 grub-mkrescue（Homebrew 的 i686-elf-grub 里叫 i686-elf-grub-mkrescue）和 xorriso。
ISO = $(BUILD_DIR)/castor.iso
ISO_ROOT = $(BUILD_DIR)/iso
ROOT_IMAGE = $(BUILD_DIR)/root.img
ROOT_SIZE_MB ?= 16
GRUB_MKRESCUE ?= $(shell command -v i686-elf-grub-mkrescue || command -v grub-mkrescue)

iso: $(ISO)

$(ISO): $(BOOT_IMAGE) $(MKDISKFS)
ifeq ($(ARCH),arm64)
	$(error make iso is for PCs: use ARCH=i686 or ARCH=x86_64)
endif
	@test -n "$(GRUB_MKRESCUE)" || { echo "grub-mkrescue not found (macOS: brew install i686-elf-grub xorriso)"; exit 1; }
	@rm -rf $(ISO_ROOT) && mkdir -p $(ISO_ROOT)/boot/grub
	@cp $(BOOT_IMAGE) $(ISO_ROOT)/boot/castor
	@printf 'set timeout=0\nmenuentry "CastorOS" {\n    multiboot /boot/castor\n}\n' > $(ISO_ROOT)/boot/grub/grub.cfg
	@rm -f $(ROOT_IMAGE) && $(MKDISKFS) $(ROOT_IMAGE) $(ROOT_SIZE_MB) $(ROOT_TREE)
	@$(GRUB_MKRESCUE) -o $@ $(ISO_ROOT) -- -append_partition 2 0x83 $(ROOT_IMAGE) \
	     2> $(BUILD_DIR)/iso.log || { cat $(BUILD_DIR)/iso.log; exit 1; }
	@echo "✓ System image: $@"

# 像真机那样启动。run-iso：映像就是硬盘（BIOS -> 硬盘上的 GRUB -> 内核，根在硬盘的分区上；
# 运行时改的东西写回映像文件）。run-cd：映像是光盘，根在内存里。run-usb：映像在 U 盘上
QEMU_ISO = $(QEMU) $(if $(QEMU_MEMORY),-m $(QEMU_MEMORY)) $(if $(SMP),-smp $(SMP)) -serial stdio -display $(QEMU_DISPLAY)
run-iso: $(ISO)
	$(QEMU_ISO) -drive file=$(ISO),format=raw,if=ide

run-cd: $(ISO)
	$(QEMU_ISO) -cdrom $(ISO)

# 映像写在 U 盘上，从 U 盘启动：BIOS -> U 盘上的 GRUB -> 内核，根在 U 盘的分区上
run-usb: $(ISO)
	$(QEMU_ISO) -device usb-ehci,id=ehci -drive file=$(ISO),format=raw,if=none,id=stick \
	     -device usb-storage,bus=ehci.0,drive=stick,bootindex=0

# 构建带内核测试的版本并运行：等命令行就绪后，scripts/shell-test.sh 再向串口输入一串命令，
# 检查命令行的行为（后台任务、Ctrl-C、kill）。完整日志写入 $(BUILD_DIR)/test.log，
# 命令行检查的结果写入 $(BUILD_DIR)/shell-test.log，这里只汇总。
# LIVE=1：不接磁盘。根就在内存里（从光盘启动、或者磁盘上没有我们的系统时是这样），
# 自检里和磁盘有关的几项跳过不算失败。
test:
	@$(MAKE) --no-print-directory run-test ARCH=$(ARCH) KTEST=1

run-test: $(BOOT_IMAGE) $(MKDISKFS)
	@echo "━━━ $(ARCH): running kernel tests, user-space selftest and shell checks$(if $(LIVE), (no disk: root in memory)) ━━━"
	@rm -f $(TEST_DISK) && $(MKDISKFS) $(TEST_DISK) $(TEST_DISK_SIZE_MB) $(ROOT_TREE) > /dev/null
	@MONITOR=$(TEST_MONITOR) KBD_DRIVER=$(if $(QEMU_KBD),usbkbd,kbd) scripts/shell-test.sh $(BUILD_DIR)/test.log $(BUILD_DIR)/shell-test.log $(TEST_TIMEOUT) \
	     $(QEMU_BASE) $(if $(LIVE),,$(call qemu_disk,$(TEST_DISK))) $(QEMU_NET) $(QEMU_KBD) $(if $(TEST_MONITOR),-monitor pipe:$(TEST_MONITOR))
	@grep -a "FAILED" $(BUILD_DIR)/shell-test.log || true
	@awk -v live=$(if $(LIVE),1,0) 'function num(key,  s) { if (!match($$0, key ": *[0-9]+")) return 0; \
	         s = substr($$0, RSTART, RLENGTH); sub(/.*: */, "", s); return s + 0 } \
	     { t += num("Total tests"); p += num("Passed tests"); f += num("Failed tests") } \
	     /\[ FAIL \]/ { fail_lines++ } \
	     /sh: ready/ { booted = 1 } /selftest: all passed/ { selftest_passed = 1 } /selftest: .*skipped/ && !(live && /\(no disk\)/) { skipped = 1 } \
	     /shelltest: all passed/ { shell_passed = 1 } \
	     END { printf "$(ARCH): %d tests, %d passed, %d failed; user space %s (log: $(BUILD_DIR)/test.log)\n", \
	               t, p, f, booted ? (selftest_passed ? (skipped ? "started, selftest SKIPPED some checks" : "started, selftest passed") : "started, selftest FAILED") \
	                                 (shell_passed ? ", shell checks passed" : ", shell checks FAILED") : "NOT started"; \
	           exit (t == 0 || f > 0 || fail_lines > 0 || t != p || !booted || !selftest_passed || skipped || !shell_passed) }' \
	     $(BUILD_DIR)/test.log $(BUILD_DIR)/shell-test.log

# 用户库的宿主机测试：printf 一族和字符串函数，用宿主机的编译器编译后直接运行，
# 不需要交叉编译器和 QEMU（user/lib/tests/lib_test.cpp）
lib-test:
	@$(MAKE) --no-print-directory -C user/lib host-test

# 每个架构都跑完（一个失败了不影响后面的），最后只要有失败的就返回非零
test-all:
	@failed=""; \
	for arch in $(VALID_ARCHS); do \
		$(MAKE) --no-print-directory test ARCH=$$arch || failed="$$failed $$arch"; \
	done; \
	if [ -n "$$failed" ]; then echo "test-all: FAILED on:$$failed"; exit 1; fi

# ============================================================================
# 清理
# ============================================================================

clean:
	rm -rf build/$(ARCH) build/$(ARCH)-ktest user/*/build/$(ARCH)

clean-all:
	rm -rf build user/*/build

# ============================================================================
# 工具
# ============================================================================

# 生成 compile_commands.json（需要 compiledb: pip3 install compiledb）
compile-db:
	compiledb -o compile_commands.json $(MAKE) -Bn ARCH=$(ARCH) KTEST=1

info:
	@echo "Architecture:  $(ARCH)"
	@echo "Compiler:      $(CXX)"
	@echo "QEMU:          $(QEMU)"
	@echo "Kernel:        $(KERNEL)"
	@echo "Init:          $(INIT_ELF)"
	@echo "KTEST:         $(KTEST)"
	@echo "CXXFLAGS:      $(CXXFLAGS)"
	@echo "Source files:  $(words $(C_SOURCES)) C++, $(words $(ASM_SOURCES)) ASM"

sources:
	@for f in $(C_SOURCES) $(ASM_SOURCES); do echo "$$f"; done

help:
	@echo "Usage: make [target] [ARCH=i686|x86_64|arm64]"
	@echo ""
	@echo "  all (default)  Build the kernel with user/init embedded"
	@echo "  build-all      Build all architectures"
	@echo "  run            Run in QEMU (serial console on stdio; QEMU_DISPLAY=cocoa opens the screen)"
	@echo "  run KBD=usb    x86: also attach a USB keyboard (test KBD=usb checks its driver)"
	@echo "  debug          Run in QEMU waiting for GDB on :1234"
	@echo "  iso            System image for a real PC: GRUB, kernel and the root file system; write it to a hard disk"
	@echo "  run-iso/run-cd/run-usb  Boot that image in QEMU as a hard disk / as a CD (root in memory) / from a USB stick"
	@echo "  test           Build with in-kernel tests (KTEST=1), boot, and check the results"
	@echo "  test-all       test for every architecture"
	@echo "  lib-test       Run the user library's host-side tests (no cross compiler or QEMU needed)"
	@echo "  clean          Clean current arch;  clean-all: everything"
	@echo "  info / sources / compile-db"
