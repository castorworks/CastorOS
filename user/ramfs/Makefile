# ramfs - 内存文件系统服务。启动映像（一个 ustar 归档）嵌在它的映像里。
#
# 这里也是整个系统的文件树做出来的地方（$(BOOTFS_DIR)）：user/bootfs 下的文件原样放入，
# BOOT_PROGRAMS 里的程序去掉调试信息后以不带 .elf 的名字放进 bin/，再加上空的 home/ 和 tmp/。
# 同一棵树有两个去处：打成归档嵌进 ramfs（没有磁盘时的根），和由顶层 Makefile 做成磁盘上的
# 根文件系统（tools/mkdiskfs）。
TARGET = ramfs
SOURCES = ramfs.cpp
ASM_SOURCES = bootfs.S

BOOT_PROGRAMS = ls cat cp rm mv mkdir echo write grep wc sleep clear hello disk ping ifconfig dns http echod poweroff reboot selftest

BOOTFS_DIR = build/$(ARCH)/bootfs
BOOTFS_TAR = build/$(ARCH)/bootfs.tar
BOOT_ELFS = $(foreach p,$(BOOT_PROGRAMS),../$(p)/build/$(ARCH)/$(p).elf)
# bootfs 下的子目录在启动映像里也是目录
BOOT_FILES = $(shell find ../bootfs -type f)

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
	@cp -R ../bootfs/. $(BOOTFS_DIR)/
	@mkdir -p $(BOOTFS_DIR)/bin $(BOOTFS_DIR)/home $(BOOTFS_DIR)/tmp
	@for p in $(BOOT_PROGRAMS); do cp ../$$p/build/$(ARCH)/$$p.stripped.elf $(BOOTFS_DIR)/bin/$$p; done
	@cd $(BOOTFS_DIR) && COPYFILE_DISABLE=1 tar --format=ustar -cf ../bootfs.tar *
	@echo "[OK] boot image: $(BOOTFS_TAR) ($(patsubst ../bootfs/%,%,$(BOOT_FILES)) $(BOOT_PROGRAMS))"
