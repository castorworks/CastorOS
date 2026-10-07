# blk - 块设备驱动：virtio-blk，PC 上还有 IDE 硬盘
TARGET = blk
SOURCES = blk.cpp virtio_blk.cpp
ifneq ($(ARCH),arm64)
SOURCES += ata.cpp
endif
include ../program.mk
