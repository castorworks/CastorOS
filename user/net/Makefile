# net - 网络服务：网卡驱动（virtio-net，PC 上还有 Intel 千兆网卡）+ 协议栈（ARP、IPv4、ICMP、UDP、TCP、DHCP）
TARGET = net
SOURCES = net.cpp nic.cpp virtio_net.cpp ip.cpp udp.cpp tcp.cpp dhcp.cpp
ifneq ($(ARCH),arm64)
SOURCES += e1000.cpp
endif
include ../program.mk
