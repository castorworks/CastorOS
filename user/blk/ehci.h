#ifndef _BLK_EHCI_H_
#define _BLK_EHCI_H_

// EHCI：USB 2.0 的主控制器。这里是它对上面（usb_storage.cpp）提供的接口：
// 复位端口、在一个设备的端点上收发数据。实现和原理在 ehci.cpp。
//
// 只照顾一个设备：一个控制端点（每个 USB 设备都有的 0 号端点），加上一对批量端点
// （U 盘用它们传数据）。

#include <types.h>

/** 高速设备 0 号端点的最大包长（规范定死的） */
#define USB_EP0_MAX_PACKET  64

/** 找到许可给本进程的控制器，复位并启动。@return 没有控制器（或者用不了）返回 false */
bool ehci_open(void);

/** 不用了：让控制器停下，不再发中断 */
void ehci_close(void);

/** 控制器有几个端口 */
int ehci_ports(void);

/**
 * 复位第 port 个端口上的设备。
 * @return 上面有一个高速设备：它现在在地址 0 上，可以开始和它说话了。
 *         没有设备，或者是低速/全速设备（归别的控制器管），返回 false
 */
bool ehci_port_reset(int port);

/** 控制端点接下来和哪个地址的设备说话（刚复位的设备在地址 0） */
void ehci_set_address(uint8_t address);

/**
 * 一次控制传输：8 字节的请求，可选的数据阶段，状态阶段。
 * request_type 的最高位是数据的方向（1 = 设备到主机）。
 * @return 数据阶段实际传了多少字节；失败返回 -1
 */
long ehci_control(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                  void *data, uint16_t length);

/** 设好那对批量端点：设备地址、两个端点号、最大包长 */
void ehci_bulk_setup(uint8_t address, uint8_t endpoint_in, uint8_t endpoint_out, uint16_t max_packet);

/**
 * 在批量端点上收（in）或发 length 字节，最多 4096。
 * @return 实际传了多少字节；失败返回 -1（端点可能被设备挂起了，见 ehci_bulk_reset）
 */
long ehci_bulk(bool in, void *data, uint32_t length);

/** 把一个批量端点在主机这边的状态恢复成初始的样子（设备那边要另外用控制请求解除挂起） */
void ehci_bulk_reset(bool in);

/** 控制器的中断线（没有是 -1） */
int ehci_irq(void);

#endif // _BLK_EHCI_H_
