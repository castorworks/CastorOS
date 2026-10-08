# blk - 块设备驱动：virtio-blk，PC 上还有 IDE 硬盘和 U 盘（USB 2.0）
TARGET = blk
SOURCES = blk.cpp virtio_blk.cpp
ifneq ($(ARCH),arm64)
SOURCES += ata.cpp ehci.cpp usb_storage.cpp
endif
include ../program.mk
