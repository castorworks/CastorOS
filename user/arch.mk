# 用户态的编译器和编译选项：用户库（lib/Makefile）和每个程序（program.mk）共用这一份，
# 两边编出来的代码必须用同一套选项（调用约定、能不能用浮点寄存器）。
#
# 提供：CROSS（工具链前缀）、CC / CXX / LD / AR、ARCH_CFLAGS（架构相关的选项，汇编也用）、
#       USER_CXXFLAGS（C++ 的公共选项，使用者再加自己的 -I）

ARCH ?= i686

ifeq ($(ARCH),i686)
    CROSS = i686-elf-
    ARCH_CFLAGS = -m32 -DARCH_I686
else ifeq ($(ARCH),x86_64)
    CROSS = x86_64-elf-
    ARCH_CFLAGS = -m64 -DARCH_X86_64 -mcmodel=large -mno-red-zone
else ifeq ($(ARCH),arm64)
    CROSS = aarch64-elf-
    ARCH_CFLAGS = -DARCH_ARM64
else
    $(error Unsupported architecture: $(ARCH). Use i686, x86_64, or arm64)
endif

CC = $(CROSS)gcc
CXX = $(CROSS)g++
LD = $(CROSS)ld
AR = $(CROSS)ar

USER_CXXFLAGS = -std=gnu++20 -ffreestanding -nostdlib -nostartfiles \
                -fno-builtin -fno-stack-protector -O0 -g -Wall -Wextra \
                $(ARCH_CFLAGS) \
                -fno-exceptions -fno-rtti -fno-threadsafe-statics \
                -fno-asynchronous-unwind-tables -fno-unwind-tables
