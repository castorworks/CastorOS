# CastorOS User Library - Makefile
# 支持多架构编译: i686, x86_64, arm64

# 编译器和编译选项和用户程序共用一份（../arch.mk）
include ../arch.mk

CXXFLAGS = $(USER_CXXFLAGS) -Iinclude

# ============================================================================
# 目录
# ============================================================================

SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build/$(ARCH)
ARCH_SRC_DIR = $(SRC_DIR)/arch/$(ARCH)

# ============================================================================
# 源文件
# ============================================================================

# 通用 C++ 源文件
C_SOURCES = $(wildcard $(SRC_DIR)/*.cpp)
C_OBJECTS = $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(C_SOURCES))

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

.PHONY: all clean clean-all info

all: $(LIBRARY)
	@echo ""
	@echo "[OK] User library built for $(ARCH): $(LIBRARY)"
	@echo ""

# 创建库
$(LIBRARY): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $(OBJECTS)

# 编译 C++ 源文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "[CXX] $< -> $@"
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

# 编译架构特定汇编文件
$(BUILD_DIR)/arch/%.o: $(ARCH_SRC_DIR)/%.S
	@mkdir -p $(dir $@)
	@echo "[AS] $< -> $@"
	$(CC) $(ARCH_CFLAGS) -c $< -o $@

-include $(OBJECTS:.o=.d)

# ============================================================================
# 宿主机测试：库里不依赖内核的部分（printf 一族、字符串函数、数学函数）用宿主机的编译器编译、
# 直接在开发机上运行（tests/lib_test.cpp）。不需要交叉编译器和 QEMU
# ============================================================================

HOST_CXX ?= c++
HOST_TEST = build/host/lib_test
HOST_TEST_SOURCES = tests/lib_test.cpp src/stdio.cpp src/string.cpp src/math.cpp

.PHONY: host-test
host-test: $(HOST_TEST)
	@$(HOST_TEST)

# -fno-builtin：不让宿主机的编译器把 memcpy、strlen 这些换成它自己的实现，要测的是库里的
$(HOST_TEST): $(HOST_TEST_SOURCES) $(wildcard include/*.h)
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=gnu++20 -O0 -g -Wall -Wextra -fno-builtin -Iinclude -o $@ $(HOST_TEST_SOURCES)

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
	@echo "Compiler:     $(CXX)"
	@echo "Archiver:     $(AR)"
	@echo "CXXFLAGS:     $(CXXFLAGS)"
	@echo "C Sources:    $(C_SOURCES)"
	@echo "ASM Sources:  $(ASM_SOURCES)"
	@echo "Objects:      $(OBJECTS)"
	@echo "Library:      $(LIBRARY)"
