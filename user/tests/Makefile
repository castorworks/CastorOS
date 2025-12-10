# CastorOS User Tests - Makefile
# 支持多架构编译: i686, x86_64, arm64

# ============================================================================
# 架构配置
# ============================================================================

# 默认架构
ARCH ?= i686

# 架构特定配置
ifeq ($(ARCH),i686)
    CC = i686-elf-gcc
    LD = i686-elf-ld
    OBJCOPY = i686-elf-objcopy
    ARCH_CFLAGS = -m32 -DARCH_I686
    LDSCRIPT = ../linker/user_i686.ld
else ifeq ($(ARCH),x86_64)
    CC = x86_64-elf-gcc
    LD = x86_64-elf-ld
    OBJCOPY = x86_64-elf-objcopy
    ARCH_CFLAGS = -m64 -DARCH_X86_64 -mcmodel=large -mno-red-zone
    LDSCRIPT = ../linker/user_x86_64.ld
else ifeq ($(ARCH),arm64)
    CC = aarch64-elf-gcc
    LD = aarch64-elf-ld
    OBJCOPY = aarch64-elf-objcopy
    ARCH_CFLAGS = -DARCH_ARM64
    LDSCRIPT = ../linker/user_arm64.ld
else
    $(error Unsupported architecture: $(ARCH). Use i686, x86_64, or arm64)
endif

# ============================================================================
# 编译标志
# ============================================================================

CFLAGS = -std=gnu99 -ffreestanding -nostdlib -nostartfiles \
         -fno-builtin -fno-stack-protector -O0 -g -Wall -Wextra \
         -I../lib/include $(ARCH_CFLAGS)

# ============================================================================
# 目录和文件
# ============================================================================

BUILD_DIR = build/$(ARCH)
LIB_DIR = ../lib
LIBRARY = $(LIB_DIR)/build/$(ARCH)/libuser.a

# 源文件
SOURCES = tests.c
OBJECTS = $(patsubst %.c, $(BUILD_DIR)/%.o, $(SOURCES))

# 目标文件
TARGET = tests
ELF = $(BUILD_DIR)/$(TARGET).elf
BINARY = $(BUILD_DIR)/$(TARGET).bin

# ============================================================================
# 目标
# ============================================================================

.PHONY: all clean clean-all lib info

all: lib $(ELF)
	@echo ""
	@echo "[OK] User tests compiled for $(ARCH): $(ELF)"
	@echo ""

# 构建库
lib:
	@$(MAKE) -C $(LIB_DIR) ARCH=$(ARCH)

$(LIBRARY): lib

# 编译 C 文件
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "[CC] $< -> $@"
	$(CC) $(CFLAGS) -c $< -o $@

# 链接为 ELF
$(ELF): $(OBJECTS) $(LIBRARY)
	@mkdir -p $(dir $@)
	@echo "[LD] $@"
	$(LD) -T $(LDSCRIPT) -nostdlib -o $@ $(OBJECTS) $(LIBRARY)

# 转换为纯二进制（可选）
$(BINARY): $(ELF)
	@echo "[OBJCOPY] $@"
	$(OBJCOPY) -O binary $< $@

# 清理当前架构
clean:
	rm -rf $(BUILD_DIR)
	@echo "[OK] Cleaned tests build files for $(ARCH)"

# 清理所有架构
clean-all:
	rm -rf build
	@echo "[OK] Cleaned all tests build files"

# 显示配置信息
info:
	@echo "=== User Tests Build Configuration ==="
	@echo "Architecture: $(ARCH)"
	@echo "Compiler:     $(CC)"
	@echo "Linker:       $(LD)"
	@echo "CFLAGS:       $(CFLAGS)"
	@echo "Linker Script: $(LDSCRIPT)"
	@echo "Library:      $(LIBRARY)"
	@echo "Output:       $(ELF)"
