#ifndef _USERLAND_LIB_PCI_H_
#define _USERLAND_LIB_PCI_H_

// PCI 配置空间（只有 x86）。
//
// PC 上的设备大多挂在 PCI 总线上，每个设备有一块 256 字节的配置空间：它是什么设备、
// 寄存器在哪里（BAR）、用哪条中断线。配置空间通过两个端口访问（0xCF8 选地址，0xCFC 读写），
// 所以只有有特权的进程碰得到：init 用它找到驱动的设备、打开设备的访问开关，再把设备的
// 寄存器和中断线许可给驱动。驱动自己不扫总线。
//
// 只看 0 号总线：主板上的设备和 QEMU 的设备都在这里。

#include <types.h>

/** 一个设备功能在总线上的位置：(设备号 << 3) | 功能号 */
typedef uint32_t pci_dev_t;
#define PCI_DEV(device, function)   (((uint32_t)(device) << 3) | (uint32_t)(function))

// 配置空间里的偏移
#define PCI_ID              0x00    // 低 16 位厂商号，高 16 位设备号；没有设备时读出来全是 1
#define PCI_COMMAND         0x04
#define PCI_CLASS           0x08    // 高 24 位：类别、子类别、编程接口
#define PCI_HEADER          0x0C    // 第 23 位：这个设备有不止一个功能
#define PCI_BAR0            0x10
#define PCI_SUBSYSTEM       0x2C
#define PCI_INTERRUPT       0x3C    // 低 8 位：固件给它分配的中断线

// PCI_COMMAND 里的开关
#define PCI_COMMAND_IO          0x1     // 响应 I/O 端口
#define PCI_COMMAND_MEMORY      0x2     // 响应设备内存
#define PCI_COMMAND_MASTER      0x4     // 可以自己读写内存（DMA）

/** 读/写配置空间里 off 处的 32 位（off 向下取到 4 的倍数） */
uint32_t pci_read(pci_dev_t dev, uint32_t off);
void pci_write(pci_dev_t dev, uint32_t off, uint32_t value);

/**
 * 找类别是 class_code（类别 << 16 | 子类别 << 8 | 编程接口）的第 index 个设备功能（从 0 开始）。
 * @return 没有这么多个返回 false
 */
bool pci_find_class(uint32_t class_code, uint32_t index, pci_dev_t *dev);

/**
 * 量出 dev 的第 bar 个 BAR 占多大（端口数或字节数）：全写 1 再读回来，设备不译码的低位
 * 读出来是 0。量的时候先关掉设备的译码，免得它在这一瞬间响应别处的地址。
 */
uint32_t pci_bar_size(pci_dev_t dev, uint32_t bar);

#endif // _USERLAND_LIB_PCI_H_
