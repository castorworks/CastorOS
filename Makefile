# CastorOS Makefile

# 工具链配置
CC = i686-elf-gcc
LD = i686-elf-ld
AS = nasm

# 编译标志
CFLAGS = -std=gnu99 -ffreestanding -O0 -g -Wall -Wextra -Isrc/include
LDFLAGS = -T linker.ld -nostdlib
ASFLAGS = -f elf32 -g -F dwarf

# 目录
SRC_DIR = src
BUILD_DIR = build

# 内核文件
KERNEL = $(BUILD_DIR)/castor.bin
DISK_IMAGE = $(BUILD_DIR)/bootable.img

# Shell 文件
SHELL_ELF = userland/shell/shell.elf

# 源文件
C_SOURCES = $(wildcard $(SRC_DIR)/drivers/*.c) \
	$(wildcard $(SRC_DIR)/fs/*.c) \
	$(wildcard $(SRC_DIR)/kernel/*.c) \
	$(wildcard $(SRC_DIR)/kernel/sync/*.c) \
	$(wildcard $(SRC_DIR)/kernel/syscalls/*.c) \
	$(wildcard $(SRC_DIR)/lib/*.c) \
	$(wildcard $(SRC_DIR)/mm/*.c) \
	$(wildcard $(SRC_DIR)/net/*.c) \
	$(wildcard $(SRC_DIR)/tests/*.c) \
	$(wildcard $(SRC_DIR)/tests/kernel/*.c)

ASM_SOURCES = $(wildcard $(SRC_DIR)/boot/*.asm) \
	$(wildcard $(SRC_DIR)/kernel/*.asm)

# 目标文件
C_OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_SOURCES))
ASM_OBJECTS = $(patsubst $(SRC_DIR)/%.asm, $(BUILD_DIR)/%.o, $(ASM_SOURCES))

# 所有目标文件
OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

# 操作系统检测
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	CREATE_SCRIPT = scripts/bootable-img-create-macos.sh
else
	CREATE_SCRIPT = scripts/bootable-img-create.sh
endif

# ============================================================================
# 内核
# ============================================================================

# 伪目标声明
.PHONY: all clean run vrun debug compile-db help shell

all: $(KERNEL)

# 链接内核
$(KERNEL): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)
	@echo "CastorOS kernel built: $@"

# 编译 C 文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# 编译汇编文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# 运行 QEMU
run: $(KERNEL)
	qemu-system-i386 -kernel $(KERNEL) -serial stdio

run-silent: $(KERNEL)
	qemu-system-i386 -kernel $(KERNEL) -serial stdio -display none

# 调试模式（等待 GDB 连接）
debug: $(KERNEL)
	qemu-system-i386 -kernel $(KERNEL) -serial stdio -s -S

debug-silent: $(KERNEL)
	qemu-system-i386 -kernel $(KERNEL) -serial stdio -s -S -display none

# 编译 Shell
shell:
	@$(MAKE) -C userland/shell

# 编译 Hello World
hello:
	@$(MAKE) -C userland/helloworld

# 编译 Userland Tests
tests:
	@$(MAKE) -C userland/tests

# 创建可启动的磁盘镜像
disk: $(KERNEL) shell hello
	@bash $(CREATE_SCRIPT)

run-disk: disk
	qemu-system-i386 -hda $(DISK_IMAGE) -serial stdio

debug-disk: disk
	qemu-system-i386 -hda $(DISK_IMAGE) -serial stdio -s -S

# 清理
clean:
	rm -rf $(BUILD_DIR)
	@echo "Cleanup complete"

# ============================================================================
# 编译数据库（用于 IDE 智能提示）
# ============================================================================

# 生成完整的 compile_commands.json
.PHONY: compile-db
compile-db:
	@bash scripts/merge-compile-commands.sh

# 显示帮助
help:
	@echo "CastorOS Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all          - Build kernel and shell (default)"
	@echo "  run          - Run kernel in QEMU"
	@echo "  run-silent   - Run kernel in QEMU (with GUI)"
	@echo "  debug        - Start QEMU in debug mode"
	@echo "  debug-silent - Start QEMU in debug mode (with GUI)"
	@echo "  compile-db   - Generate compile_commands.json for IDE"
	@echo "  clean        - Clean build files"
	@echo "  help         - Show this help message"
