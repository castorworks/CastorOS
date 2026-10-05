# ramfs - 内存文件系统服务。启动映像（一个 ustar 归档）嵌在它的映像里：
# user/bootfs 下的文件原样放入，BOOT_PROGRAMS 里的程序去掉调试信息后以不带 .elf 的名字放入。
TARGET = ramfs
SOURCES = ramfs.cpp
ASM_SOURCES = bootfs.S

BOOT_PROGRAMS = ls cat cp rm echo hello disk selftest

BOOTFS_DIR = build/$(ARCH)/bootfs
BOOTFS_TAR = build/$(ARCH)/bootfs.tar
BOOT_ELFS = $(foreach p,$(BOOT_PROGRAMS),../$(p)/build/$(ARCH)/$(p).elf)
BOOT_FILES = $(wildcard ../bootfs/*)

MODULES = $(BOOTFS_TAR)
ASM_DEFINES = -DBOOTFS_IMAGE='"$(BOOTFS_TAR)"'

include ../program.mk

# 每次都交给程序自己的 Makefile 判断是否需要重建
define PROGRAM_RULE
../$(1)/build/$$(ARCH)/$(1).elf: FORCE
	@$$(MAKE) --no-print-directory -C ../$(1) ARCH=$$(ARCH)
endef
$(foreach p,$(BOOT_PROGRAMS),$(eval $(call PROGRAM_RULE,$(p))))

.PHONY: FORCE
FORCE:

# COPYFILE_DISABLE: 不让 macOS 的 tar 往归档里加 ._ 文件
$(BOOTFS_TAR): $(BOOT_ELFS) $(BOOT_FILES)
	@rm -rf $(BOOTFS_DIR) && mkdir -p $(BOOTFS_DIR)
	@cp $(BOOT_FILES) $(BOOTFS_DIR)/
	@for p in $(BOOT_PROGRAMS); do cp ../$$p/build/$(ARCH)/$$p.stripped.elf $(BOOTFS_DIR)/$$p; done
	@cd $(BOOTFS_DIR) && COPYFILE_DISABLE=1 tar --format=ustar -cf ../bootfs.tar *
	@echo "[OK] boot image: $(BOOTFS_TAR) ($(notdir $(BOOT_FILES)) $(BOOT_PROGRAMS))"
