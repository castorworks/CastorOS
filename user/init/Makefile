# init - 第一个用户进程；把它要启动的模块（驱动）以 .incbin 的方式带在自己的映像里
TARGET = init
SOURCES = init.cpp
ASM_SOURCES = modules.S
INCLUDES = -I../uart

UART_ELF = ../uart/build/$(ARCH)/uart.elf
MODULES = $(UART_ELF)
ASM_DEFINES = -DUART_IMAGE='"$(UART_ELF)"'

include ../program.mk

$(UART_ELF): $(wildcard ../uart/*.cpp ../uart/*.h ../uart/Makefile) $(LIBRARY)
	@$(MAKE) --no-print-directory -C ../uart ARCH=$(ARCH)
