# CastorOS Makefile
# ============================================================================
# 快速参考:
#   make                    # 构建 i686 内核（内嵌 user/init）
#   make ARCH=arm64         # 构建 ARM64 内核
#   make run                # 在 QEMU 中运行（串口控制台）
#   make test               # 构建带内核测试的版本并运行
#   make build-all          # 构建所有架构
# ============================================================================

ARCH ?= i686
VALID_ARCHS := i686 x86_64 arm64

ifeq ($(filter $(ARCH),$(VALID_ARCHS)),)
$(error Invalid ARCH=$(ARCH). Valid options: $(VALID_ARCHS))
endif

# KTEST=1: 把 src/tests 编进内核，启动时运行（make test 会自动设置）
KTEST ?= 0

# 测试超时时间 (秒)
TEST_TIMEOUT ?= 60
# timeout 命令 (macOS 需要安装 coreutils: brew install coreutils)
TIMEOUT_CMD = timeout

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
INIT_DEPS = $(wildcard user/program.mk user/linker/*.ld user/bootfs/* \
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

.PHONY: all clean clean-all run debug test run-test test-all build-all check init info sources compile-db help

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
	$(CC) $(CFLAGS) -DINIT_IMAGE='"$(INIT_ELF)"' -c $< -o $@

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

QEMU_RUN = $(QEMU) $(QEMU_MACHINE) -kernel $(BOOT_IMAGE) -serial stdio -display none

run: $(BOOT_IMAGE)
	$(QEMU_RUN)

# 等待 GDB 连接 (target remote :1234)
debug: $(BOOT_IMAGE)
	$(QEMU_RUN) -s -S

# 构建带内核测试的版本并运行。内核不会自己关机：到超时为止，完整日志写入
# $(BUILD_DIR)/test.log，这里只汇总各测试模块的计数。
test:
	@$(MAKE) --no-print-directory run-test ARCH=$(ARCH) KTEST=1

run-test: $(BOOT_IMAGE)
	@echo "━━━ $(ARCH): running kernel tests (timeout $(TEST_TIMEOUT)s) ━━━"
	-@$(TIMEOUT_CMD) $(TEST_TIMEOUT) $(QEMU_RUN) < /dev/null > $(BUILD_DIR)/test.log 2>&1
	@awk 'function num(key,  s) { if (!match($$0, key ": *[0-9]+")) return 0; \
	         s = substr($$0, RSTART, RLENGTH); sub(/.*: */, "", s); return s + 0 } \
	     { t += num("Total tests"); p += num("Passed tests"); f += num("Failed tests") } \
	     /sh: ready/ { booted = 1 } /selftest: all passed/ { selftest_passed = 1 } \
	     END { printf "$(ARCH): %d tests, %d passed, %d failed; user space %s (log: $(BUILD_DIR)/test.log)\n", \
	               t, p, f, booted ? (selftest_passed ? "started, selftest passed" : "started, selftest FAILED") : "NOT started"; \
	           exit (t == 0 || f > 0 || !booted || !selftest_passed) }' $(BUILD_DIR)/test.log

test-all:
	@for arch in $(VALID_ARCHS); do \
		$(MAKE) --no-print-directory test ARCH=$$arch || true; \
	done

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
	@echo "  run            Run in QEMU (serial console on stdio)"
	@echo "  debug          Run in QEMU waiting for GDB on :1234"
	@echo "  test           Build with in-kernel tests (KTEST=1) and run with a timeout"
	@echo "  test-all       test for every architecture"
	@echo "  clean          Clean current arch;  clean-all: everything"
	@echo "  info / sources / compile-db"
