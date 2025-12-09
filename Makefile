# CastorOS Makefile - Multi-Architecture Support

# ============================================================================
# 架构配置
# ============================================================================

# 默认架构为 i686 (向后兼容)
ARCH ?= i686

# 验证架构选择
VALID_ARCHS := i686 x86_64 arm64
ifeq ($(filter $(ARCH),$(VALID_ARCHS)),)
$(error Invalid ARCH=$(ARCH). Valid options: $(VALID_ARCHS))
endif

# ============================================================================
# 架构特定工具链配置
# ============================================================================

ifeq ($(ARCH),i686)
    # i686 (x86 32-bit) 配置
    CC = i686-elf-gcc
    LD = i686-elf-ld
    AS = nasm
    OBJCOPY = i686-elf-objcopy
    ARCH_CFLAGS = -m32
    ARCH_LDFLAGS = -T linker.ld -nostdlib
    ARCH_ASFLAGS = -f elf32 -g -F dwarf
    ARCH_DEFINE = -DARCH_I686
    QEMU = qemu-system-i386
    QEMU_FLAGS = -kernel
else ifeq ($(ARCH),x86_64)
    # x86_64 (64-bit) 配置
    CC = x86_64-elf-gcc
    LD = x86_64-elf-ld
    AS = nasm
    OBJCOPY = x86_64-elf-objcopy
    ARCH_CFLAGS = -m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2
    ARCH_LDFLAGS = -T linker_x86_64.ld -nostdlib
    ARCH_ASFLAGS = -f elf64 -g -F dwarf
    ARCH_DEFINE = -DARCH_X86_64
    QEMU = qemu-system-x86_64
    QEMU_FLAGS = -kernel
else ifeq ($(ARCH),arm64)
    # ARM64 (AArch64) 配置
    CC = aarch64-elf-gcc
    LD = aarch64-elf-ld
    AS = aarch64-elf-as
    OBJCOPY = aarch64-elf-objcopy
    ARCH_CFLAGS = -mcpu=cortex-a72
    ARCH_LDFLAGS = -T linker_arm64.ld -nostdlib
    ARCH_ASFLAGS =
    ARCH_DEFINE = -DARCH_ARM64
    QEMU = qemu-system-aarch64
    QEMU_FLAGS = -M virt -cpu cortex-a72 -kernel
endif

# ============================================================================
# 通用编译标志
# ============================================================================

CFLAGS = -std=gnu99 -ffreestanding -O0 -g -Wall -Wextra \
         -Isrc/include -Isrc/arch/$(ARCH)/include \
         $(ARCH_CFLAGS) $(ARCH_DEFINE)
LDFLAGS = $(ARCH_LDFLAGS)
ASFLAGS = $(ARCH_ASFLAGS)

# ============================================================================
# 目录配置
# ============================================================================

SRC_DIR = src
BUILD_DIR = build/$(ARCH)
ARCH_DIR = $(SRC_DIR)/arch/$(ARCH)

# ============================================================================
# 输出文件
# ============================================================================

KERNEL = $(BUILD_DIR)/castor.bin
DISK_IMAGE = $(BUILD_DIR)/bootable.img

# ============================================================================
# 源文件收集
# ============================================================================

# 通用源文件 (架构无关)
COMMON_C_SOURCES = $(wildcard $(SRC_DIR)/drivers/*.c) \
	$(wildcard $(SRC_DIR)/drivers/usb/*.c) \
	$(wildcard $(SRC_DIR)/fs/*.c) \
	$(wildcard $(SRC_DIR)/kernel/*.c) \
	$(wildcard $(SRC_DIR)/kernel/sync/*.c) \
	$(wildcard $(SRC_DIR)/kernel/syscalls/*.c) \
	$(wildcard $(SRC_DIR)/lib/*.c) \
	$(wildcard $(SRC_DIR)/mm/*.c) \
	$(wildcard $(SRC_DIR)/net/*.c) \
	$(wildcard $(SRC_DIR)/tests/*.c) \
	$(wildcard $(SRC_DIR)/tests/kernel/*.c)

# 架构特定源文件
ARCH_C_SOURCES = $(wildcard $(ARCH_DIR)/*.c) \
	$(wildcard $(ARCH_DIR)/boot/*.c) \
	$(wildcard $(ARCH_DIR)/cpu/*.c) \
	$(wildcard $(ARCH_DIR)/interrupt/*.c) \
	$(wildcard $(ARCH_DIR)/mm/*.c) \
	$(wildcard $(ARCH_DIR)/task/*.c) \
	$(wildcard $(ARCH_DIR)/syscall/*.c)

# 汇编源文件 (架构特定)
ifeq ($(ARCH),i686)
    # i686 使用架构特定目录
    COMMON_ASM_SOURCES = $(wildcard $(SRC_DIR)/kernel/*.asm)
    ARCH_ASM_SOURCES = $(wildcard $(ARCH_DIR)/*.asm) \
        $(wildcard $(ARCH_DIR)/boot/*.asm) \
        $(wildcard $(ARCH_DIR)/cpu/*.asm) \
        $(wildcard $(ARCH_DIR)/interrupt/*.asm) \
        $(wildcard $(ARCH_DIR)/task/*.asm) \
        $(wildcard $(ARCH_DIR)/syscall/*.asm)
else ifeq ($(ARCH),x86_64)
    COMMON_ASM_SOURCES =
    ARCH_ASM_SOURCES = $(wildcard $(ARCH_DIR)/*.asm) \
        $(wildcard $(ARCH_DIR)/boot/*.asm) \
        $(wildcard $(ARCH_DIR)/cpu/*.asm) \
        $(wildcard $(ARCH_DIR)/interrupt/*.asm) \
        $(wildcard $(ARCH_DIR)/task/*.asm) \
        $(wildcard $(ARCH_DIR)/syscall/*.asm)
else ifeq ($(ARCH),arm64)
    # ARM64 使用 GNU as 语法 (.S 文件)
    COMMON_ASM_SOURCES =
    ARCH_ASM_SOURCES = $(wildcard $(ARCH_DIR)/*.S) \
        $(wildcard $(ARCH_DIR)/boot/*.S) \
        $(wildcard $(ARCH_DIR)/cpu/*.S) \
        $(wildcard $(ARCH_DIR)/interrupt/*.S) \
        $(wildcard $(ARCH_DIR)/task/*.S) \
        $(wildcard $(ARCH_DIR)/syscall/*.S)
endif

# 合并所有源文件
C_SOURCES = $(COMMON_C_SOURCES) $(ARCH_C_SOURCES)
ASM_SOURCES = $(COMMON_ASM_SOURCES) $(ARCH_ASM_SOURCES)

# ============================================================================
# 目标文件生成
# ============================================================================

# C 目标文件
C_OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_SOURCES))

# 汇编目标文件 (处理 .asm 和 .S 文件)
ifeq ($(ARCH),arm64)
    ASM_OBJECTS = $(patsubst $(SRC_DIR)/%.S, $(BUILD_DIR)/%.o, $(ASM_SOURCES))
else
    ASM_OBJECTS = $(patsubst $(SRC_DIR)/%.asm, $(BUILD_DIR)/%.o, $(ASM_SOURCES))
endif

# 所有目标文件
OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

# ============================================================================
# 操作系统检测
# ============================================================================

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	CREATE_SCRIPT = scripts/bootable-img-create-macos.sh
else
	CREATE_SCRIPT = scripts/bootable-img-create.sh
endif

# ============================================================================
# 构建目标
# ============================================================================

.PHONY: all clean run vrun debug compile-db help shell info

all: $(KERNEL)

# 显示当前架构配置信息
info:
	@echo "CastorOS Build Configuration"
	@echo "============================"
	@echo "Architecture: $(ARCH)"
	@echo "Compiler:     $(CC)"
	@echo "Linker:       $(LD)"
	@echo "Assembler:    $(AS)"
	@echo "CFLAGS:       $(CFLAGS)"
	@echo "LDFLAGS:      $(LDFLAGS)"
	@echo "ASFLAGS:      $(ASFLAGS)"
	@echo "Build Dir:    $(BUILD_DIR)"
	@echo "Kernel:       $(KERNEL)"

# 链接内核
$(KERNEL): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)
	@echo "CastorOS kernel built for $(ARCH): $@"

# 编译 C 文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# 编译 NASM 汇编文件 (i686, x86_64)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# 编译 GNU as 汇编文件 (ARM64)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ============================================================================
# 运行和调试
# ============================================================================

# 运行 QEMU
# Note: x86_64 requires GRUB bootloader, so we use disk image
run: $(KERNEL)
ifeq ($(ARCH),arm64)
	$(QEMU) $(QEMU_FLAGS) $(KERNEL) -nographic -serial mon:stdio
else ifeq ($(ARCH),x86_64)
	@echo "x86_64 requires bootable disk image with GRUB..."
	@$(MAKE) run-disk ARCH=x86_64
else
	$(QEMU) $(QEMU_FLAGS) $(KERNEL) -serial stdio
endif

run-silent: $(KERNEL)
ifeq ($(ARCH),arm64)
	$(QEMU) $(QEMU_FLAGS) $(KERNEL) -nographic -serial mon:stdio
else ifeq ($(ARCH),x86_64)
	@echo "x86_64 requires bootable disk image with GRUB..."
	@$(MAKE) run-disk ARCH=x86_64
else
	$(QEMU) $(QEMU_FLAGS) $(KERNEL) -serial stdio -display none
endif

# 调试模式（等待 GDB 连接）
debug: $(KERNEL)
ifeq ($(ARCH),arm64)
	$(QEMU) $(QEMU_FLAGS) $(KERNEL) -nographic -serial mon:stdio -s -S
else ifeq ($(ARCH),x86_64)
	@echo "x86_64 requires bootable disk image with GRUB..."
	@$(MAKE) debug-disk ARCH=x86_64
else
	$(QEMU) $(QEMU_FLAGS) $(KERNEL) -serial stdio -s -S
endif

debug-silent: $(KERNEL)
ifeq ($(ARCH),arm64)
	$(QEMU) $(QEMU_FLAGS) $(KERNEL) -nographic -serial mon:stdio -s -S
else ifeq ($(ARCH),x86_64)
	@echo "x86_64 requires bootable disk image with GRUB..."
	@$(MAKE) debug-disk ARCH=x86_64
else
	$(QEMU) $(QEMU_FLAGS) $(KERNEL) -serial stdio -s -S -display none
endif

# ============================================================================
# 用户空间构建
# ============================================================================

# 编译 Shell
shell:
	@$(MAKE) -C user/shell ARCH=$(ARCH)

# 编译 Hello World
hello:
	@$(MAKE) -C user/helloworld ARCH=$(ARCH)

# 编译用户态测试
tests:
	@$(MAKE) -C user/tests ARCH=$(ARCH)

# ============================================================================
# 磁盘镜像
# ============================================================================

# 创建可启动的磁盘镜像
disk: $(KERNEL) shell hello tests
	@ARCH=$(ARCH) bash $(CREATE_SCRIPT)

run-disk: disk
ifeq ($(ARCH),i686)
	qemu-system-i386 -hda $(DISK_IMAGE) -serial stdio -netdev user,id=net0 -device e1000,netdev=net0
else ifeq ($(ARCH),x86_64)
	qemu-system-x86_64 -hda $(DISK_IMAGE) -serial stdio -netdev user,id=net0 -device e1000,netdev=net0
else ifeq ($(ARCH),arm64)
	qemu-system-aarch64 -M virt -cpu cortex-a72 -hda $(DISK_IMAGE) -nographic -serial mon:stdio
endif

debug-disk: disk
ifeq ($(ARCH),i686)
	qemu-system-i386 -hda $(DISK_IMAGE) -serial stdio -netdev user,id=net0 -device e1000,netdev=net0 -s -S
else ifeq ($(ARCH),x86_64)
	qemu-system-x86_64 -hda $(DISK_IMAGE) -serial stdio -netdev user,id=net0 -device e1000,netdev=net0 -s -S
else ifeq ($(ARCH),arm64)
	qemu-system-aarch64 -M virt -cpu cortex-a72 -hda $(DISK_IMAGE) -nographic -serial mon:stdio -s -S
endif

# ============================================================================
# 清理
# ============================================================================

clean:
	rm -rf build/$(ARCH)
	@echo "Cleanup complete for $(ARCH)"

clean-all:
	rm -rf build
	@echo "Cleanup complete for all architectures"

# ============================================================================
# 编译数据库（用于 IDE 智能提示）
# ============================================================================

.PHONY: compile-db
compile-db:
	@ARCH=$(ARCH) bash scripts/merge-compile-commands.sh

# ============================================================================
# 帮助信息
# ============================================================================

help:
	@echo "CastorOS Build System - Multi-Architecture Support"
	@echo ""
	@echo "Usage: make [target] [ARCH=i686|x86_64|arm64]"
	@echo ""
	@echo "Architectures:"
	@echo "  i686    - Intel x86 32-bit (default)"
	@echo "  x86_64  - AMD64/Intel 64-bit"
	@echo "  arm64   - ARM AArch64"
	@echo ""
	@echo "Build targets:"
	@echo "  all          - Build kernel (default)"
	@echo "  info         - Show build configuration"
	@echo "  clean        - Clean build files for current ARCH"
	@echo "  clean-all    - Clean build files for all architectures"
	@echo ""
	@echo "Run targets:"
	@echo "  run          - Run kernel in QEMU"
	@echo "  run-silent   - Run kernel in QEMU (no GUI)"
	@echo "  debug        - Start QEMU in debug mode"
	@echo "  debug-silent - Start QEMU in debug mode (no GUI)"
	@echo ""
	@echo "User space targets:"
	@echo "  shell        - Build shell"
	@echo "  hello        - Build hello world"
	@echo "  tests        - Build user tests"
	@echo ""
	@echo "Disk targets:"
	@echo "  disk         - Create bootable disk image"
	@echo "  run-disk     - Run from disk image"
	@echo "  debug-disk   - Debug from disk image"
	@echo ""
	@echo "Other targets:"
	@echo "  compile-db   - Generate compile_commands.json"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make                    # Build for i686 (default)"
	@echo "  make ARCH=x86_64        # Build for x86_64"
	@echo "  make ARCH=arm64 run     # Build and run for ARM64"
