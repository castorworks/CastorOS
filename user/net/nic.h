#ifndef _NET_NIC_H_
#define _NET_NIC_H_

// 一块网卡。nic.cpp 通过这几个函数收发以太网帧，不关心它是哪种设备；每种设备一个源文件，
// 各自提供一个 <设备>_open：许可给本进程的设备是它那一种、而且能用，就填好 *nic 返回 true
// （MAC 地址填进 my_mac）。
//
// 许可给本进程的是哪一种，看许可表的样子：virtio-net 在 x86 上是一段端口，Intel 千兆网卡
// 是一段设备内存。

#include <types.h>

struct nic {
    const char *kind;       // 设备的种类，只用来打印
    int irq;
    /** 发一个以太网帧（不含帧尾的校验）。设备的发送队列满了就丢掉 */
    void (*send)(const uint8_t *frame, size_t len);
    /** 网卡的中断来了：让设备撤销它，把收到的每个帧交给 nic_receive */
    void (*interrupt)(void);
};

/** 驱动收到一个帧（nic.cpp）：交给协议栈 */
void nic_receive(uint8_t *frame, size_t len);

bool virtio_net_open(struct nic *nic);
#if !defined(ARCH_ARM64)
bool e1000_allowed(void);               // 许可表里是一块 Intel 千兆网卡
bool e1000_open(struct nic *nic);
#endif

#endif // _NET_NIC_H_
