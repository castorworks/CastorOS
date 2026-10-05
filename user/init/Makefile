# init - 第一个用户进程：启动模块，并充当名字服务。
# 它要启动的模块以 .incbin 的方式带在自己的映像里（modules.S）。
TARGET = init
SOURCES = init.cpp
ASM_SOURCES = modules.S

MODULE_NAMES = uart blk ramfs diskfs sh
MODULES = $(foreach m,$(MODULE_NAMES),../$(m)/build/$(ARCH)/$(m).elf)
ASM_DEFINES = $(foreach m,$(MODULE_NAMES),-D$(m)_IMAGE='"../$(m)/build/$(ARCH)/$(m).stripped.elf"')

include ../program.mk

# 模块有自己的依赖（ramfs 还依赖启动映像里的程序），所以每次都交给它自己的 Makefile 判断
define MODULE_RULE
../$(1)/build/$$(ARCH)/$(1).elf: FORCE
	@$$(MAKE) --no-print-directory -C ../$(1) ARCH=$$(ARCH)
endef
$(foreach m,$(MODULE_NAMES),$(eval $(call MODULE_RULE,$(m))))

.PHONY: FORCE
FORCE:
