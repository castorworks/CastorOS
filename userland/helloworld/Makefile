# CastorOS Hello World Example - Makefile

# 工具链配置
CC = i686-elf-gcc
LD = i686-elf-ld
OBJCOPY = i686-elf-objcopy

# 编译标志
CFLAGS = -m32 -std=gnu99 -ffreestanding -nostdlib -nostartfiles \
         -fno-builtin -fno-stack-protector -O0 -g -Wall -Wextra \
         -I../lib/include

# 链接脚本
LDSCRIPT = user.ld

# 库文件
LIB_DIR = ../lib
LIBRARY = $(LIB_DIR)/build/libuserland.a

# 源文件
SOURCES = hello.c
OBJECTS = $(SOURCES:.c=.o)

# 目标文件
TARGET = hello
ELF = $(TARGET).elf
BINARY = $(TARGET).bin

# 默认目标
all: $(LIBRARY) $(ELF)
	@echo ""
	@echo "[OK] Hello World compiled successfully: $(ELF)"
	@echo ""

# 构建库
$(LIBRARY):
	@$(MAKE) -C $(LIB_DIR)

# 编译 C 文件
%.o: %.c
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c $< -o $@

# 链接为 ELF
$(ELF): $(OBJECTS) $(LIBRARY)
	@echo "[LD] $@"
	$(LD) -T $(LDSCRIPT) -nostdlib -o $@ $(OBJECTS) $(LIBRARY)

# 转换为纯二进制（可选）
$(BINARY): $(ELF)
	@echo "[OBJCOPY] $@"
	$(OBJCOPY) -O binary $< $@

# 清理
clean:
	rm -f $(OBJECTS) $(ELF) $(BINARY)
	@echo "[OK] Cleaned hello build files"

.PHONY: all clean

