# CastorOS User Library - Makefile
# 支持多架构编译: i686, x86_64, arm64

# ============================================================================
# 架构配置
# ============================================================================

# 默认架构
ARCH ?= i686

# 架构特定配置
ifeq ($(ARCH),i686)
    CC = i686-elf-gcc
    AR = i686-elf-ar
    AS = i686-elf-gcc
    ARCH_CFLAGS = -m32 -DARCH_I686
    ARCH_ASFLAGS = -m32
    ARCH_DIR = i686
else ifeq ($(ARCH),x86_64)
    CC = x86_64-elf-gcc
    AR = x86_64-elf-ar
    AS = x86_64-elf-gcc
    ARCH_CFLAGS = -m64 -DARCH_X86_64 -mcmodel=large -mno-red-zone
    ARCH_ASFLAGS = -m64
    ARCH_DIR = x86_64
else ifeq ($(ARCH),arm64)
    CC = aarch64-elf-gcc
    AR = aarch64-elf-ar
    AS = aarch64-elf-gcc
    ARCH_CFLAGS = -DARCH_ARM64
    ARCH_ASFLAGS =
    ARCH_DIR = arm64
else
    $(error Unsupported architecture: $(ARCH). Use i686, x86_64, or arm64)
endif

# ============================================================================
# 编译标志
# ============================================================================

CFLAGS = -std=gnu99 -ffreestanding -nostdlib -nostartfiles \
         -fno-builtin -fno-stack-protector -O0 -g -Wall -Wextra \
         -Iinclude $(ARCH_CFLAGS)

ASFLAGS = $(ARCH_ASFLAGS)

# ============================================================================
# 目录
# ============================================================================

SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build/$(ARCH)
ARCH_SRC_DIR = $(SRC_DIR)/arch/$(ARCH_DIR)

# ============================================================================
# 源文件
# ============================================================================

# 通用 C 源文件
C_SOURCES = $(wildcard $(SRC_DIR)/*.c)
C_OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_SOURCES))

# 架构特定汇编源文件
ASM_SOURCES = $(wildcard $(ARCH_SRC_DIR)/*.S)
ASM_OBJECTS = $(patsubst $(ARCH_SRC_DIR)/%.S, $(BUILD_DIR)/arch/%.o, $(ASM_SOURCES))

# 所有目标文件
OBJECTS = $(C_OBJECTS) $(ASM_OBJECTS)

# ============================================================================
# 库文件
# ============================================================================

LIBRARY = $(BUILD_DIR)/libuser.a

# ============================================================================
# 目标
# ============================================================================

.PHONY: all clean info

all: $(LIBRARY)
	@echo ""
	@echo "[OK] User library built for $(ARCH): $(LIBRARY)"
	@echo ""

# 创建库
$(LIBRARY): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $(OBJECTS)

# 编译 C 源文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $< -> $@"
	$(CC) $(CFLAGS) -c $< -o $@

# 编译架构特定汇编文件
$(BUILD_DIR)/arch/%.o: $(ARCH_SRC_DIR)/%.S
	@mkdir -p $(dir $@)
	@echo "[AS] $< -> $@"
	$(AS) $(ASFLAGS) -c $< -o $@

# 清理
clean:
	rm -rf build/$(ARCH)
	@echo "[OK] Cleaned user library build files for $(ARCH)"

clean-all:
	rm -rf build
	@echo "[OK] Cleaned all user library build files"

# 显示配置信息
info:
	@echo "=== User Library Build Configuration ==="
	@echo "Architecture: $(ARCH)"
	@echo "Compiler:     $(CC)"
	@echo "Archiver:     $(AR)"
	@echo "Assembler:    $(AS)"
	@echo "CFLAGS:       $(CFLAGS)"
	@echo "ASFLAGS:      $(ASFLAGS)"
	@echo "C Sources:    $(C_SOURCES)"
	@echo "ASM Sources:  $(ASM_SOURCES)"
	@echo "Objects:      $(OBJECTS)"
	@echo "Library:      $(LIBRARY)"
