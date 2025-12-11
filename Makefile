# CastorOS Makefile - Multi-Architecture Support
# ============================================================================
# 快速参考:
#   make                    # 构建 i686 内核
#   make ARCH=arm64         # 构建 ARM64 内核
#   make test               # 构建并运行测试 (8秒超时)
#   make test-all           # 测试所有架构
#   make build-all          # 构建所有架构
# ============================================================================

# ============================================================================
# 架构配置
# ============================================================================

ARCH ?= i686
VALID_ARCHS := i686 x86_64 arm64

ifeq ($(filter $(ARCH),$(VALID_ARCHS)),)
$(error Invalid ARCH=$(ARCH). Valid options: $(VALID_ARCHS))
endif

# ============================================================================
# 调试配置
# ============================================================================

# 测试超时时间 (秒)
TEST_TIMEOUT ?= 8
# 输出行数限制
OUTPUT_LINES ?= 200
# macOS 使用 gtimeout, Linux 使用 timeout
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    TIMEOUT_CMD = gtimeout
    CREATE_SCRIPT = scripts/bootable-img-create-macos.sh
else
    TIMEOUT_CMD = timeout
    CREATE_SCRIPT = scripts/bootable-img-create.sh
endif

# ============================================================================
# 架构特定工具链配置
# ============================================================================

ifeq ($(ARCH),i686)
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
    QEMU_MACHINE =
else ifeq ($(ARCH),x86_64)
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
    QEMU_MACHINE =
else ifeq ($(ARCH),arm64)
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
    QEMU_MACHINE = -M virt -cpu cortex-a72
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

ifeq ($(ARCH),arm64)
    COMMON_C_SOURCES = $(SRC_DIR)/lib/string.c \
        $(SRC_DIR)/lib/libgcc_stub.c \
        $(wildcard $(SRC_DIR)/drivers/arm/*.c) \
        $(wildcard $(SRC_DIR)/drivers/platform/*.c)
else
    COMMON_C_SOURCES = $(wildcard $(SRC_DIR)/drivers/common/*.c) \
        $(wildcard $(SRC_DIR)/drivers/platform/*.c) \
        $(wildcard $(SRC_DIR)/drivers/x86/*.c) \
        $(wildcard $(SRC_DIR)/drivers/x86/usb/*.c) \
        $(wildcard $(SRC_DIR)/fs/*.c) \
        $(wildcard $(SRC_DIR)/kernel/*.c) \
        $(wildcard $(SRC_DIR)/kernel/sync/*.c) \
        $(wildcard $(SRC_DIR)/kernel/syscalls/*.c) \
        $(wildcard $(SRC_DIR)/lib/*.c) \
        $(wildcard $(SRC_DIR)/mm/*.c) \
        $(wildcard $(SRC_DIR)/net/*.c) \
        $(wildcard $(SRC_DIR)/tests/*.c) \
        $(wildcard $(SRC_DIR)/tests/kernel/*.c) \
        $(wildcard $(SRC_DIR)/tests/pbt/*.c)
endif

ifeq ($(ARCH),arm64)
    ARCH_C_SOURCES = $(ARCH_DIR)/hal.c \
        $(ARCH_DIR)/hal_caps.c \
        $(ARCH_DIR)/stubs.c \
        $(ARCH_DIR)/boot/boot_info.c \
        $(ARCH_DIR)/interrupt/exception.c \
        $(ARCH_DIR)/interrupt/gic.c \
        $(ARCH_DIR)/interrupt/hal_irq.c \
        $(ARCH_DIR)/mm/mmu.c \
        $(ARCH_DIR)/mm/fault.c \
        $(ARCH_DIR)/mm/pgtable.c \
        $(ARCH_DIR)/task/context.c \
        $(ARCH_DIR)/syscall/syscall.c \
        $(ARCH_DIR)/syscall/hal_syscall.c \
        $(ARCH_DIR)/dtb/dtb.c
else
    ARCH_C_SOURCES = $(wildcard $(ARCH_DIR)/*.c) \
        $(wildcard $(ARCH_DIR)/boot/*.c) \
        $(wildcard $(ARCH_DIR)/cpu/*.c) \
        $(wildcard $(ARCH_DIR)/interrupt/*.c) \
        $(wildcard $(ARCH_DIR)/mm/*.c) \
        $(wildcard $(ARCH_DIR)/task/*.c) \
        $(wildcard $(ARCH_DIR)/syscall/*.c)
endif

ifeq ($(ARCH),i686)
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
    COMMON_ASM_SOURCES =
    ARCH_ASM_SOURCES = $(wildcard $(ARCH_DIR)/*.S) \
        $(wildcard $(ARCH_DIR)/boot/*.S) \
        $(wildcard $(ARCH_DIR)/cpu/*.S) \
        $(wildcard $(ARCH_DIR)/interrupt/*.S) \
        $(wildcard $(ARCH_DIR)/task/*.S) \
        $(wildcard $(ARCH_DIR)/syscall/*.S)
endif

C_SOURCES = $(COMMON_C_SOURCES) $(ARCH_C_SOURCES)
ASM_SOURCES = $(COMMON_ASM_SOURCES) $(ARCH_ASM_SOURCES)

# ============================================================================
# 目标文件生成
# ============================================================================

C_OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_SOURCES))

ifeq ($(ARCH),arm64)
    ASM_OBJECTS = $(patsubst $(SRC_DIR)/%.S, $(BUILD_DIR)/%.o, $(ASM_SOURCES))
else
    ASM_OBJECTS = $(patsubst $(SRC_DIR)/%.asm, $(BUILD_DIR)/%.o, $(ASM_SOURCES))
endif

OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

# ============================================================================
# 主要构建目标
# ============================================================================

.PHONY: all clean run debug info help
.PHONY: test test-all build-all check
.PHONY: shell hello tests disk

all: $(KERNEL)

$(KERNEL): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)
	@echo "✓ CastorOS kernel built: $(KERNEL)"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ============================================================================
# 测试目标 (带超时)
# ============================================================================

# 快速测试: 构建并运行，自动超时
test: $(KERNEL)
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo "Testing $(ARCH) (timeout: $(TEST_TIMEOUT)s)"
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
ifeq ($(ARCH),arm64)
	@$(TIMEOUT_CMD) $(TEST_TIMEOUT) $(QEMU) $(QEMU_FLAGS) $(KERNEL) -nographic 2>&1 | head -$(OUTPUT_LINES) || true
else
	@$(TIMEOUT_CMD) $(TEST_TIMEOUT) $(QEMU) $(QEMU_FLAGS) $(KERNEL) -serial stdio -display none 2>&1 | head -$(OUTPUT_LINES) || true
endif
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# 测试所有架构
test-all:
	@echo "╔══════════════════════════════════════╗"
	@echo "║     Testing All Architectures        ║"
	@echo "╚══════════════════════════════════════╝"
	@for arch in $(VALID_ARCHS); do \
		echo ""; \
		$(MAKE) test ARCH=$$arch || true; \
	done
	@echo ""
	@echo "✓ All architecture tests completed"

# 构建所有架构
build-all:
	@echo "Building all architectures..."
	@for arch in $(VALID_ARCHS); do \
		echo "━━━ Building $$arch ━━━"; \
		$(MAKE) ARCH=$$arch || exit 1; \
	done
	@echo "✓ All architectures built successfully"

# 快速检查: 仅编译，不运行
check: $(KERNEL)
	@echo "✓ $(ARCH) build OK: $(KERNEL)"
	@ls -lh $(KERNEL)

# ============================================================================
# 运行目标
# ============================================================================

run: $(KERNEL)
ifeq ($(ARCH),arm64)
	$(QEMU) $(QEMU_FLAGS) $(KERNEL) -nographic -serial mon:stdio
else ifeq ($(ARCH),x86_64)
	@$(MAKE) run-disk ARCH=x86_64
else
	$(QEMU) $(QEMU_FLAGS) $(KERNEL) -serial stdio
endif

run-silent: $(KERNEL)
ifeq ($(ARCH),arm64)
	$(QEMU) $(QEMU_FLAGS) $(KERNEL) -nographic -serial mon:stdio
else ifeq ($(ARCH),x86_64)
	@$(MAKE) run-disk ARCH=x86_64
else
	$(QEMU) $(QEMU_FLAGS) $(KERNEL) -serial stdio -display none
endif

# ============================================================================
# 调试目标
# ============================================================================

debug: $(KERNEL)
ifeq ($(ARCH),arm64)
	$(QEMU) $(QEMU_FLAGS) $(KERNEL) -nographic -serial mon:stdio -s -S
else ifeq ($(ARCH),x86_64)
	@$(MAKE) debug-disk ARCH=x86_64
else
	$(QEMU) $(QEMU_FLAGS) $(KERNEL) -serial stdio -s -S
endif

debug-silent: $(KERNEL)
ifeq ($(ARCH),arm64)
	$(QEMU) $(QEMU_FLAGS) $(KERNEL) -nographic -serial mon:stdio -s -S
else ifeq ($(ARCH),x86_64)
	@$(MAKE) debug-disk ARCH=x86_64
else
	$(QEMU) $(QEMU_FLAGS) $(KERNEL) -serial stdio -s -S -display none
endif

# 带超时的调试输出捕获
debug-capture: $(KERNEL)
	@echo "Capturing $(ARCH) output ($(TEST_TIMEOUT)s)..."
ifeq ($(ARCH),arm64)
	@$(TIMEOUT_CMD) $(TEST_TIMEOUT) $(QEMU) $(QEMU_FLAGS) $(KERNEL) -nographic 2>&1 | tee $(BUILD_DIR)/debug.log | head -$(OUTPUT_LINES)
else
	@$(TIMEOUT_CMD) $(TEST_TIMEOUT) $(QEMU) $(QEMU_FLAGS) $(KERNEL) -serial stdio -display none 2>&1 | tee $(BUILD_DIR)/debug.log | head -$(OUTPUT_LINES)
endif
	@echo "Output saved to $(BUILD_DIR)/debug.log"

# ============================================================================
# 用户空间构建
# ============================================================================

shell:
	@$(MAKE) -C user/shell ARCH=$(ARCH)

hello:
	@$(MAKE) -C user/helloworld ARCH=$(ARCH)

tests:
	@$(MAKE) -C user/tests ARCH=$(ARCH)

user-all: shell hello tests
	@echo "✓ All user programs built for $(ARCH)"

user-clean:
	@$(MAKE) -C user/shell clean-all
	@$(MAKE) -C user/helloworld clean-all
	@$(MAKE) -C user/tests clean-all
	@$(MAKE) -C user/lib clean-all
	@echo "✓ User programs cleaned"

# ============================================================================
# 磁盘镜像
# ============================================================================

disk: $(KERNEL) shell hello tests
	@ARCH=$(ARCH) bash $(CREATE_SCRIPT)

run-disk: disk
ifeq ($(ARCH),i686)
	qemu-system-i386 -hda $(DISK_IMAGE) -serial stdio -netdev user,id=net0 -device e1000,netdev=net0
else ifeq ($(ARCH),x86_64)
	qemu-system-x86_64 -hda $(DISK_IMAGE) -serial stdio -netdev user,id=net0 -device e1000,netdev=net0
else ifeq ($(ARCH),arm64)
	qemu-system-aarch64 $(QEMU_MACHINE) -hda $(DISK_IMAGE) -nographic -serial mon:stdio
endif

debug-disk: disk
ifeq ($(ARCH),i686)
	qemu-system-i386 -hda $(DISK_IMAGE) -serial stdio -netdev user,id=net0 -device e1000,netdev=net0 -s -S
else ifeq ($(ARCH),x86_64)
	qemu-system-x86_64 -hda $(DISK_IMAGE) -serial stdio -netdev user,id=net0 -device e1000,netdev=net0 -s -S
else ifeq ($(ARCH),arm64)
	qemu-system-aarch64 $(QEMU_MACHINE) -hda $(DISK_IMAGE) -nographic -serial mon:stdio -s -S
endif

# ============================================================================
# 清理
# ============================================================================

clean:
	rm -rf build/$(ARCH)
	@echo "✓ Cleaned $(ARCH)"

clean-all:
	rm -rf build
	@echo "✓ Cleaned all architectures"

distclean: clean-all user-clean
	@echo "✓ Full clean completed"

# ============================================================================
# 工具
# ============================================================================

compile-db:
	@ARCH=$(ARCH) bash scripts/merge-compile-commands.sh

# 显示配置信息
info:
	@echo "╔══════════════════════════════════════╗"
	@echo "║     CastorOS Build Configuration     ║"
	@echo "╚══════════════════════════════════════╝"
	@echo "Architecture:  $(ARCH)"
	@echo "Compiler:      $(CC)"
	@echo "Linker:        $(LD)"
	@echo "Assembler:     $(AS)"
	@echo "QEMU:          $(QEMU)"
	@echo "Build Dir:     $(BUILD_DIR)"
	@echo "Kernel:        $(KERNEL)"
	@echo "Timeout:       $(TEST_TIMEOUT)s"
	@echo ""
	@echo "Source files:  $(words $(C_SOURCES)) C, $(words $(ASM_SOURCES)) ASM"

# 列出源文件
sources:
	@echo "=== C Sources ($(words $(C_SOURCES))) ==="
	@for f in $(C_SOURCES); do echo "  $$f"; done
	@echo ""
	@echo "=== ASM Sources ($(words $(ASM_SOURCES))) ==="
	@for f in $(ASM_SOURCES); do echo "  $$f"; done

# ============================================================================
# 帮助
# ============================================================================

help:
	@echo "CastorOS Build System"
	@echo ""
	@echo "Usage: make [target] [ARCH=i686|x86_64|arm64] [TEST_TIMEOUT=8]"
	@echo ""
	@echo "Build:"
	@echo "  all          Build kernel (default)"
	@echo "  build-all    Build all architectures"
	@echo "  check        Build and verify"
	@echo "  disk         Create bootable disk image"
	@echo ""
	@echo "Test:"
	@echo "  test         Build and run with timeout ($(TEST_TIMEOUT)s)"
	@echo "  test-all     Test all architectures"
	@echo ""
	@echo "Run:"
	@echo "  run          Run in QEMU"
	@echo "  run-silent   Run without GUI"
	@echo "  run-disk     Run from disk image"
	@echo ""
	@echo "Debug:"
	@echo "  debug        Start QEMU with GDB server"
	@echo "  debug-silent Debug without GUI"
	@echo "  debug-capture Capture output to file"
	@echo ""
	@echo "User Space:"
	@echo "  shell        Build shell"
	@echo "  hello        Build hello world"
	@echo "  tests        Build user tests"
	@echo "  user-all     Build all user programs"
	@echo ""
	@echo "Clean:"
	@echo "  clean        Clean current arch"
	@echo "  clean-all    Clean all architectures"
	@echo "  distclean    Clean everything"
	@echo ""
	@echo "Info:"
	@echo "  info         Show build configuration"
	@echo "  sources      List source files"
	@echo "  compile-db   Generate compile_commands.json"
	@echo ""
	@echo "Examples:"
	@echo "  make                          # Build i686"
	@echo "  make ARCH=arm64 test          # Test ARM64"
	@echo "  make test-all                 # Test all archs"
	@echo "  make TEST_TIMEOUT=15 test     # Custom timeout"
