# CastorOS Userland Library - Makefile

# 工具链配置
CC = i686-elf-gcc
AR = i686-elf-ar

# 编译标志
CFLAGS = -m32 -std=gnu99 -ffreestanding -nostdlib -nostartfiles \
         -fno-builtin -fno-stack-protector -O0 -g -Wall -Wextra \
         -Iinclude

# 目录
SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build

# 源文件
SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SOURCES))

# 库文件
LIBRARY = $(BUILD_DIR)/libuserland.a

# 默认目标
all: $(LIBRARY)
	@echo ""
	@echo "[OK] Userland library built: $(LIBRARY)"
	@echo ""

# 创建库
$(LIBRARY): $(OBJECTS)
	@mkdir -p $(BUILD_DIR)
	$(AR) rcs $@ $(OBJECTS)

# 编译源文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c $< -o $@

# 清理
clean:
	rm -rf $(BUILD_DIR)
	@echo "[OK] Cleaned userland library build files"

.PHONY: all clean

