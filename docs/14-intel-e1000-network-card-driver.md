# 阶段 14: Intel E1000 网卡驱动

## 概述

本阶段将实现 Intel E1000 系列千兆以太网控制器驱动，这是 CastorOS 网络功能的物理层实现。E1000 系列网卡是虚拟化环境（QEMU、VirtualBox、VMware）中最常用的模拟网卡，也是学习网卡驱动开发的理想选择。

**📝 设计理念**：

本阶段实现以下核心功能：

✅ **PCI 设备检测**
   - 扫描 PCI 总线发现 E1000 网卡
   - 读取 BAR 寄存器获取 MMIO 地址
   - 配置 PCI 命令寄存器

✅ **硬件初始化**
   - 设备重置
   - MAC 地址读取
   - 中断配置

✅ **DMA 描述符环**
   - 发送描述符环（TX Ring）
   - 接收描述符环（RX Ring）
   - 环形缓冲区管理

✅ **数据包收发**
   - 中断驱动的数据包接收
   - 发送队列管理
   - 与网络栈集成（netdev 接口）

---

## 目标

- [ ] 实现 PCI 设备检测和 E1000 网卡识别
- [ ] 实现 MMIO 寄存器访问
- [ ] 实现设备初始化和重置
- [ ] 实现 MAC 地址读取
- [ ] 实现 TX 描述符环和数据包发送
- [ ] 实现 RX 描述符环和数据包接收
- [ ] 实现中断处理
- [ ] 实现 netdev 接口集成
- [ ] 在 QEMU 中测试网络功能

---

## 技术背景

### Intel E1000 系列概述

**Intel E1000** 是 Intel 千兆以太网控制器系列的统称，包括：

| 型号 | Device ID | 说明 |
|------|-----------|------|
| 82540EM | 0x100E | 桌面版，QEMU 默认模拟 |
| 82545EM | 0x100F | 服务器版 |
| 82541 | 0x1019 | 笔记本版 |
| 82543GC | 0x1004 | 早期型号 |
| 82574L | 0x10D3 | PCIe 版本 |

**QEMU 中的 E1000 模拟**：
```bash
# 使用 E1000 网卡启动 QEMU
qemu-system-i386 -kernel castor.bin \
    -netdev user,id=net0 \
    -device e1000,netdev=net0
```

**E1000 主要特性**：
- 千兆以太网（1000 Mbps）
- PCI/PCI-X 总线接口
- 支持全双工/半双工
- 硬件校验和卸载
- VLAN 标签支持
- 中断合并（Interrupt Coalescing）

### PCI 配置空间

E1000 是 PCI 设备，需要通过 PCI 配置空间访问。

**E1000 PCI 标识**：
```
Vendor ID: 0x8086 (Intel)
Device ID: 0x100E (82540EM) - QEMU 默认
Class Code: 0x020000 (Ethernet Controller)
```

**BAR（Base Address Register）布局**：
```
BAR0: Memory-mapped I/O (MMIO) 寄存器空间
      大小: 128KB (0x20000)
      用途: 访问所有设备寄存器

BAR1: I/O 端口空间（可选，较少使用）
BAR2: Flash 存储（如果存在）
```

### MMIO 寄存器

E1000 通过内存映射 I/O（MMIO）访问寄存器，所有寄存器都是 32 位对齐的。

**核心寄存器组**：

| 偏移 | 名称 | 说明 |
|------|------|------|
| 0x0000 | CTRL | 设备控制寄存器 |
| 0x0008 | STATUS | 设备状态寄存器 |
| 0x00C0 | ICR | 中断原因寄存器（读清除）|
| 0x00C4 | ITR | 中断节流寄存器 |
| 0x00C8 | ICS | 中断原因设置寄存器 |
| 0x00D0 | IMS | 中断掩码设置寄存器 |
| 0x00D8 | IMC | 中断掩码清除寄存器 |
| 0x0100 | RCTL | 接收控制寄存器 |
| 0x0400 | TCTL | 发送控制寄存器 |
| 0x2800 | RDBAL | 接收描述符基地址（低 32 位）|
| 0x2804 | RDBAH | 接收描述符基地址（高 32 位）|
| 0x2808 | RDLEN | 接收描述符长度 |
| 0x2810 | RDH | 接收描述符头指针 |
| 0x2818 | RDT | 接收描述符尾指针 |
| 0x3800 | TDBAL | 发送描述符基地址（低 32 位）|
| 0x3804 | TDBAH | 发送描述符基地址（高 32 位）|
| 0x3808 | TDLEN | 发送描述符长度 |
| 0x3810 | TDH | 发送描述符头指针 |
| 0x3818 | TDT | 发送描述符尾指针 |
| 0x5400 | RAL0 | 接收地址低（MAC 地址低 32 位）|
| 0x5404 | RAH0 | 接收地址高（MAC 地址高 16 位 + 标志）|

### 设备控制寄存器（CTRL）

```
 31                                                              0
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
|        Reserved       |VME|TFCE|RFCE|RST|     ...     |SLU|   |
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

Bit 26: RST   - 设备重置
Bit 6:  SLU   - Set Link Up（强制链路启用）
Bit 3:  LRST  - 链路重置
Bit 0:  FD    - 全双工模式
```

**控制寄存器位定义**：
```c
#define E1000_CTRL_FD       (1 << 0)    // 全双工
#define E1000_CTRL_LRST     (1 << 3)    // 链路重置
#define E1000_CTRL_ASDE     (1 << 5)    // 自动速度检测
#define E1000_CTRL_SLU      (1 << 6)    // Set Link Up
#define E1000_CTRL_ILOS     (1 << 7)    // 反转信号丢失
#define E1000_CTRL_RST      (1 << 26)   // 设备重置
#define E1000_CTRL_VME      (1 << 30)   // VLAN 模式启用
#define E1000_CTRL_PHY_RST  (1 << 31)   // PHY 重置
```

### 设备状态寄存器（STATUS）

```c
#define E1000_STATUS_FD     (1 << 0)    // 全双工模式激活
#define E1000_STATUS_LU     (1 << 1)    // 链路已建立
#define E1000_STATUS_TXOFF  (1 << 4)    // 传输暂停
#define E1000_STATUS_SPEED_MASK 0xC0    // 速度掩码
#define E1000_STATUS_SPEED_10   0x00    // 10 Mbps
#define E1000_STATUS_SPEED_100  0x40    // 100 Mbps
#define E1000_STATUS_SPEED_1000 0x80    // 1000 Mbps
```

### 中断系统

**中断原因寄存器（ICR）位定义**：
```c
#define E1000_ICR_TXDW      (1 << 0)    // 发送描述符写回
#define E1000_ICR_TXQE      (1 << 1)    // 发送队列空
#define E1000_ICR_LSC       (1 << 2)    // 链路状态变化
#define E1000_ICR_RXSEQ     (1 << 3)    // 接收序列错误
#define E1000_ICR_RXDMT0    (1 << 4)    // 接收描述符最小阈值
#define E1000_ICR_RXO       (1 << 6)    // 接收溢出
#define E1000_ICR_RXT0      (1 << 7)    // 接收定时器中断
```

**中断流程**：
```
数据包到达
    ↓
DMA 写入接收缓冲区
    ↓
更新接收描述符
    ↓
触发 RXT0 中断（如果启用）
    ↓
CPU 读取 ICR（清除中断）
    ↓
处理接收描述符
    ↓
更新 RDT 指针
```

### 描述符环（Descriptor Ring）

E1000 使用环形缓冲区管理数据包收发，每个缓冲区由描述符（Descriptor）描述。

**接收描述符（Legacy RX Descriptor）**：
```
 63                                                              0
+---------------------------------------------------------------+
|                     Buffer Address (64-bit)                    |
+---------------------------------------------------------------+
|   Special   |  Errors  |  Status  |    Checksum    |  Length  |
+---------------------------------------------------------------+

字段说明：
- Buffer Address: 接收缓冲区物理地址
- Length: 接收到的数据包长度
- Checksum: 数据包校验和
- Status: 状态位（DD=描述符完成，EOP=包结束）
- Errors: 错误标志
- Special: VLAN 标签等
```

**接收描述符状态位**：
```c
#define E1000_RXD_STAT_DD   (1 << 0)    // 描述符已完成
#define E1000_RXD_STAT_EOP  (1 << 1)    // 数据包结束
#define E1000_RXD_STAT_IXSM (1 << 2)    // 忽略校验和
#define E1000_RXD_STAT_VP   (1 << 3)    // 数据包是 VLAN
#define E1000_RXD_STAT_TCPCS (1 << 5)   // TCP 校验和已计算
#define E1000_RXD_STAT_IPCS (1 << 6)    // IP 校验和已计算
```

**发送描述符（Legacy TX Descriptor）**：
```
 63                                                              0
+---------------------------------------------------------------+
|                     Buffer Address (64-bit)                    |
+---------------------------------------------------------------+
|   Special   | CSS |  Rsv  | Status | Command |      Length     |
+---------------------------------------------------------------+

字段说明：
- Buffer Address: 发送缓冲区物理地址
- Length: 要发送的数据长度
- Command: 命令位（EOP=包结束，IFCS=插入 FCS，RS=报告状态）
- Status: 状态位（DD=描述符完成）
- CSS: 校验和起始位置
- Special: VLAN 标签等
```

**发送描述符命令位**：
```c
#define E1000_TXD_CMD_EOP   (1 << 0)    // 数据包结束
#define E1000_TXD_CMD_IFCS  (1 << 1)    // 插入 FCS
#define E1000_TXD_CMD_IC    (1 << 2)    // 插入校验和
#define E1000_TXD_CMD_RS    (1 << 3)    // 报告状态
#define E1000_TXD_CMD_DEXT  (1 << 5)    // 描述符扩展（高级描述符）
#define E1000_TXD_CMD_VLE   (1 << 6)    // VLAN 包启用
#define E1000_TXD_CMD_IDE   (1 << 7)    // 中断延迟启用
```

**描述符环工作原理**：
```
发送描述符环（TX Ring）:

    TDH (Head) - 硬件下一个要处理的描述符
    TDT (Tail) - 软件下一个要写入的位置

    +---+---+---+---+---+---+---+---+
    | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |  描述符数组
    +---+---+---+---+---+---+---+---+
          ^           ^
          |           |
         TDH         TDT
          |           |
          +-----------+
          硬件正在发送的区域

工作流程：
1. 软件在 TDT 位置写入新描述符
2. 软件更新 TDT 指针（TDT++）
3. 硬件发现 TDH != TDT，开始发送
4. 发送完成后 TDH++ 并设置 DD 位
5. 软件可以回收 TDH 之前的描述符

接收描述符环（RX Ring）:

    RDH (Head) - 硬件下一个要写入的描述符
    RDT (Tail) - 软件已准备好的最后一个描述符

    +---+---+---+---+---+---+---+---+
    | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |  描述符数组
    +---+---+---+---+---+---+---+---+
          ^           ^
          |           |
         RDH         RDT
          |           |
          +-----------+
          可用于接收的缓冲区

工作流程：
1. 软件分配缓冲区，填充描述符
2. 软件更新 RDT 指针
3. 硬件收到数据包，写入缓冲区
4. 硬件更新描述符状态，设置 DD 位，RDH++
5. 触发中断（RXT0）
6. 软件处理接收到的数据包
7. 软件重新分配缓冲区，更新 RDT
```

### 接收控制寄存器（RCTL）

```c
#define E1000_RCTL_EN       (1 << 1)    // 接收启用
#define E1000_RCTL_SBP      (1 << 2)    // 存储坏包
#define E1000_RCTL_UPE      (1 << 3)    // 单播混杂模式
#define E1000_RCTL_MPE      (1 << 4)    // 多播混杂模式
#define E1000_RCTL_LPE      (1 << 5)    // 长包启用
#define E1000_RCTL_LBM_MASK 0xC0        // 环回模式
#define E1000_RCTL_RDMTS_HALF 0         // RX 描述符最小阈值
#define E1000_RCTL_MO_MASK  0x3000      // 多播偏移
#define E1000_RCTL_BAM      (1 << 15)   // 广播接受模式
#define E1000_RCTL_BSIZE_MASK 0x30000   // 缓冲区大小
#define E1000_RCTL_BSIZE_2048 0x00000   // 2048 字节缓冲区
#define E1000_RCTL_BSIZE_1024 0x10000   // 1024 字节缓冲区
#define E1000_RCTL_BSIZE_512  0x20000   // 512 字节缓冲区
#define E1000_RCTL_BSIZE_256  0x30000   // 256 字节缓冲区
#define E1000_RCTL_SECRC    (1 << 26)   // 剥离以太网 CRC
```

### 发送控制寄存器（TCTL）

```c
#define E1000_TCTL_EN       (1 << 1)    // 发送启用
#define E1000_TCTL_PSP      (1 << 3)    // 填充短包
#define E1000_TCTL_CT_SHIFT 4           // 冲突阈值位移
#define E1000_TCTL_COLD_SHIFT 12        // 冲突距离位移
#define E1000_TCTL_SWXOFF   (1 << 22)   // 软件 XOFF 传输
#define E1000_TCTL_RTLC     (1 << 24)   // 重传晚期冲突
```

---

## 实现设计

### 1. 文件结构

```
src/
├── include/
│   └── drivers/
│       ├── pci.h         # PCI 总线驱动（阶段 12 已规划）
│       └── e1000.h       # E1000 网卡驱动
└── drivers/
    ├── pci.c             # PCI 总线驱动实现
    └── e1000.c           # E1000 网卡驱动实现
```

### 2. E1000 驱动头文件

**文件**: `src/include/drivers/e1000.h`

```c
#ifndef _DRIVERS_E1000_H_
#define _DRIVERS_E1000_H_

#include <types.h>
#include <net/netdev.h>
#include <net/netbuf.h>

/* ============================================================================
 * PCI 标识
 * ============================================================================ */

#define E1000_VENDOR_ID         0x8086  // Intel

/* 支持的设备 ID 列表 */
#define E1000_DEV_ID_82540EM    0x100E  // QEMU 默认
#define E1000_DEV_ID_82545EM    0x100F
#define E1000_DEV_ID_82541      0x1019
#define E1000_DEV_ID_82543GC    0x1004
#define E1000_DEV_ID_82574L     0x10D3

/* ============================================================================
 * 寄存器偏移
 * ============================================================================ */

/* 通用寄存器 */
#define E1000_REG_CTRL      0x0000      // 设备控制
#define E1000_REG_STATUS    0x0008      // 设备状态
#define E1000_REG_EECD      0x0010      // EEPROM/Flash 控制
#define E1000_REG_EERD      0x0014      // EEPROM 读取
#define E1000_REG_CTRL_EXT  0x0018      // 扩展设备控制
#define E1000_REG_MDIC      0x0020      // MDI 控制

/* 中断寄存器 */
#define E1000_REG_ICR       0x00C0      // 中断原因读取（读清除）
#define E1000_REG_ITR       0x00C4      // 中断节流
#define E1000_REG_ICS       0x00C8      // 中断原因设置
#define E1000_REG_IMS       0x00D0      // 中断掩码设置
#define E1000_REG_IMC       0x00D8      // 中断掩码清除

/* 接收寄存器 */
#define E1000_REG_RCTL      0x0100      // 接收控制
#define E1000_REG_RDBAL     0x2800      // RX 描述符基地址低
#define E1000_REG_RDBAH     0x2804      // RX 描述符基地址高
#define E1000_REG_RDLEN     0x2808      // RX 描述符长度
#define E1000_REG_RDH       0x2810      // RX 描述符头
#define E1000_REG_RDT       0x2818      // RX 描述符尾
#define E1000_REG_RDTR      0x2820      // RX 延迟定时器

/* 发送寄存器 */
#define E1000_REG_TCTL      0x0400      // 发送控制
#define E1000_REG_TIPG      0x0410      // 发送间隔
#define E1000_REG_TDBAL     0x3800      // TX 描述符基地址低
#define E1000_REG_TDBAH     0x3804      // TX 描述符基地址高
#define E1000_REG_TDLEN     0x3808      // TX 描述符长度
#define E1000_REG_TDH       0x3810      // TX 描述符头
#define E1000_REG_TDT       0x3818      // TX 描述符尾

/* MAC 地址寄存器 */
#define E1000_REG_RAL0      0x5400      // 接收地址低（MAC 低 32 位）
#define E1000_REG_RAH0      0x5404      // 接收地址高（MAC 高 16 位）

/* 统计寄存器 */
#define E1000_REG_GPRC      0x4074      // 好的接收包数
#define E1000_REG_GPTC      0x4080      // 好的发送包数
#define E1000_REG_GORCL     0x4088      // 好的接收字节数（低）
#define E1000_REG_GORCH     0x408C      // 好的接收字节数（高）
#define E1000_REG_GOTCL     0x4090      // 好的发送字节数（低）
#define E1000_REG_GOTCH     0x4094      // 好的发送字节数（高）

/* MTA (Multicast Table Array) */
#define E1000_REG_MTA       0x5200      // 多播表数组（128 个 32 位寄存器）

/* ============================================================================
 * 控制寄存器位
 * ============================================================================ */

#define E1000_CTRL_FD       (1 << 0)    // 全双工
#define E1000_CTRL_LRST     (1 << 3)    // 链路重置
#define E1000_CTRL_ASDE     (1 << 5)    // 自动速度检测启用
#define E1000_CTRL_SLU      (1 << 6)    // Set Link Up
#define E1000_CTRL_ILOS     (1 << 7)    // 反转信号丢失
#define E1000_CTRL_RST      (1 << 26)   // 设备重置
#define E1000_CTRL_VME      (1 << 30)   // VLAN 模式启用
#define E1000_CTRL_PHY_RST  (1 << 31)   // PHY 重置

/* ============================================================================
 * 状态寄存器位
 * ============================================================================ */

#define E1000_STATUS_FD     (1 << 0)    // 全双工
#define E1000_STATUS_LU     (1 << 1)    // 链路已建立
#define E1000_STATUS_TXOFF  (1 << 4)    // 传输暂停
#define E1000_STATUS_SPEED_MASK  0xC0
#define E1000_STATUS_SPEED_10    0x00
#define E1000_STATUS_SPEED_100   0x40
#define E1000_STATUS_SPEED_1000  0x80

/* ============================================================================
 * 中断位
 * ============================================================================ */

#define E1000_ICR_TXDW      (1 << 0)    // TX 描述符写回
#define E1000_ICR_TXQE      (1 << 1)    // TX 队列空
#define E1000_ICR_LSC       (1 << 2)    // 链路状态变化
#define E1000_ICR_RXSEQ     (1 << 3)    // RX 序列错误
#define E1000_ICR_RXDMT0    (1 << 4)    // RX 描述符最小阈值
#define E1000_ICR_RXO       (1 << 6)    // RX 溢出
#define E1000_ICR_RXT0      (1 << 7)    // RX 定时器中断

/* ============================================================================
 * 接收控制寄存器位
 * ============================================================================ */

#define E1000_RCTL_EN       (1 << 1)    // 接收启用
#define E1000_RCTL_SBP      (1 << 2)    // 存储坏包
#define E1000_RCTL_UPE      (1 << 3)    // 单播混杂模式
#define E1000_RCTL_MPE      (1 << 4)    // 多播混杂模式
#define E1000_RCTL_LPE      (1 << 5)    // 长包启用
#define E1000_RCTL_LBM_MASK 0xC0        // 环回模式掩码
#define E1000_RCTL_LBM_NO   0x00        // 无环回
#define E1000_RCTL_RDMTS_HALF   0x000   // RX 描述符最小阈值 = 1/2
#define E1000_RCTL_RDMTS_QUARTER 0x100  // RX 描述符最小阈值 = 1/4
#define E1000_RCTL_RDMTS_EIGHTH  0x200  // RX 描述符最小阈值 = 1/8
#define E1000_RCTL_MO_36    0x0000      // 多播偏移 36
#define E1000_RCTL_MO_35    0x1000      // 多播偏移 35
#define E1000_RCTL_MO_34    0x2000      // 多播偏移 34
#define E1000_RCTL_MO_32    0x3000      // 多播偏移 32
#define E1000_RCTL_BAM      (1 << 15)   // 广播接受模式
#define E1000_RCTL_BSIZE_2048   0x00000 // 缓冲区大小 2048
#define E1000_RCTL_BSIZE_1024   0x10000 // 缓冲区大小 1024
#define E1000_RCTL_BSIZE_512    0x20000 // 缓冲区大小 512
#define E1000_RCTL_BSIZE_256    0x30000 // 缓冲区大小 256
#define E1000_RCTL_BSEX     (1 << 25)   // 缓冲区大小扩展
#define E1000_RCTL_SECRC    (1 << 26)   // 剥离以太网 CRC

/* ============================================================================
 * 发送控制寄存器位
 * ============================================================================ */

#define E1000_TCTL_EN       (1 << 1)    // 发送启用
#define E1000_TCTL_PSP      (1 << 3)    // 填充短包
#define E1000_TCTL_CT_SHIFT 4           // 冲突阈值位移
#define E1000_TCTL_COLD_SHIFT 12        // 冲突距离位移
#define E1000_TCTL_SWXOFF   (1 << 22)   // 软件 XOFF
#define E1000_TCTL_RTLC     (1 << 24)   // 重传晚期冲突

/* TIPG 默认值 */
#define E1000_TIPG_IPGT     10          // IPG 传输时间
#define E1000_TIPG_IPGR1    8           // IPG 接收时间 1
#define E1000_TIPG_IPGR2    6           // IPG 接收时间 2

/* ============================================================================
 * 描述符定义
 * ============================================================================ */

/* 接收描述符（Legacy 格式） */
typedef struct e1000_rx_desc {
    uint64_t buffer_addr;       // 缓冲区物理地址
    uint16_t length;            // 接收到的数据长度
    uint16_t checksum;          // 数据包校验和
    uint8_t  status;            // 状态
    uint8_t  errors;            // 错误
    uint16_t special;           // 特殊字段（VLAN 标签）
} __attribute__((packed)) e1000_rx_desc_t;

/* 接收描述符状态位 */
#define E1000_RXD_STAT_DD   (1 << 0)    // 描述符完成
#define E1000_RXD_STAT_EOP  (1 << 1)    // 数据包结束
#define E1000_RXD_STAT_IXSM (1 << 2)    // 忽略校验和
#define E1000_RXD_STAT_VP   (1 << 3)    // VLAN 数据包
#define E1000_RXD_STAT_TCPCS (1 << 5)   // TCP 校验和已计算
#define E1000_RXD_STAT_IPCS (1 << 6)    // IP 校验和已计算
#define E1000_RXD_STAT_PIF  (1 << 7)    // 传递完整帧

/* 发送描述符（Legacy 格式） */
typedef struct e1000_tx_desc {
    uint64_t buffer_addr;       // 缓冲区物理地址
    uint16_t length;            // 数据长度
    uint8_t  cso;               // 校验和偏移
    uint8_t  cmd;               // 命令
    uint8_t  status;            // 状态
    uint8_t  css;               // 校验和起始
    uint16_t special;           // 特殊字段
} __attribute__((packed)) e1000_tx_desc_t;

/* 发送描述符命令位 */
#define E1000_TXD_CMD_EOP   (1 << 0)    // 数据包结束
#define E1000_TXD_CMD_IFCS  (1 << 1)    // 插入 FCS
#define E1000_TXD_CMD_IC    (1 << 2)    // 插入校验和
#define E1000_TXD_CMD_RS    (1 << 3)    // 报告状态
#define E1000_TXD_CMD_DEXT  (1 << 5)    // 描述符扩展
#define E1000_TXD_CMD_VLE   (1 << 6)    // VLAN 包启用
#define E1000_TXD_CMD_IDE   (1 << 7)    // 中断延迟启用

/* 发送描述符状态位 */
#define E1000_TXD_STAT_DD   (1 << 0)    // 描述符完成
#define E1000_TXD_STAT_EC   (1 << 1)    // 过多冲突
#define E1000_TXD_STAT_LC   (1 << 2)    // 晚期冲突
#define E1000_TXD_STAT_TU   (1 << 3)    // 传输下溢

/* ============================================================================
 * 驱动配置
 * ============================================================================ */

#define E1000_NUM_RX_DESC   32          // 接收描述符数量（必须是 8 的倍数）
#define E1000_NUM_TX_DESC   32          // 发送描述符数量（必须是 8 的倍数）
#define E1000_RX_BUFFER_SIZE 2048       // 接收缓冲区大小

/* ============================================================================
 * 设备结构
 * ============================================================================ */

typedef struct e1000_device {
    /* PCI 信息 */
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t device_id;
    uint8_t irq;
    
    /* MMIO 基地址 */
    volatile uint32_t *mmio_base;
    uint32_t mmio_size;
    
    /* MAC 地址 */
    uint8_t mac_addr[6];
    
    /* 接收描述符环 */
    e1000_rx_desc_t *rx_descs;          // 描述符数组（物理地址对齐）
    uint32_t rx_descs_phys;              // 描述符数组物理地址
    uint8_t *rx_buffers[E1000_NUM_RX_DESC]; // 接收缓冲区数组
    uint32_t rx_cur;                     // 当前接收描述符索引
    
    /* 发送描述符环 */
    e1000_tx_desc_t *tx_descs;          // 描述符数组（物理地址对齐）
    uint32_t tx_descs_phys;              // 描述符数组物理地址
    uint8_t *tx_buffers[E1000_NUM_TX_DESC]; // 发送缓冲区数组
    uint32_t tx_cur;                     // 当前发送描述符索引
    
    /* 网络设备接口 */
    netdev_t netdev;
    
    /* 统计信息 */
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_errors;
    uint64_t tx_errors;
    
    /* 链路状态 */
    bool link_up;
    uint32_t speed;                      // Mbps
    bool full_duplex;
} e1000_device_t;

/* ============================================================================
 * 函数声明
 * ============================================================================ */

/**
 * 初始化 E1000 驱动
 * 扫描 PCI 总线，检测并初始化所有 E1000 网卡
 * @return 检测到的网卡数量，-1 表示错误
 */
int e1000_init(void);

/**
 * 获取 E1000 设备
 * @param index 设备索引
 * @return 设备指针，不存在返回 NULL
 */
e1000_device_t *e1000_get_device(int index);

/**
 * 发送数据包
 * @param dev 设备指针
 * @param data 数据指针
 * @param len 数据长度
 * @return 0 成功，-1 失败
 */
int e1000_send(e1000_device_t *dev, void *data, uint32_t len);

/**
 * 接收数据包（由中断处理程序调用）
 * @param dev 设备指针
 */
void e1000_receive(e1000_device_t *dev);

/**
 * 获取 MAC 地址
 * @param dev 设备指针
 * @param mac 输出 MAC 地址（6 字节）
 */
void e1000_get_mac(e1000_device_t *dev, uint8_t *mac);

/**
 * 启用/禁用设备
 * @param dev 设备指针
 * @param enable true 启用，false 禁用
 * @return 0 成功，-1 失败
 */
int e1000_set_enable(e1000_device_t *dev, bool enable);

/**
 * 获取链路状态
 * @param dev 设备指针
 * @return true 链路已建立，false 链路断开
 */
bool e1000_link_up(e1000_device_t *dev);

/**
 * 打印设备信息（调试用）
 * @param dev 设备指针
 */
void e1000_print_info(e1000_device_t *dev);

#endif // _DRIVERS_E1000_H_
```

### 3. E1000 驱动实现

**文件**: `src/drivers/e1000.c`

```c
/**
 * Intel E1000 千兆以太网控制器驱动
 * 
 * 支持型号: 82540EM (QEMU), 82545EM, 82541, 82543GC, 82574L
 * 
 * 功能:
 * - PCI 设备检测和初始化
 * - MMIO 寄存器访问
 * - DMA 描述符环管理
 * - 中断驱动的数据包收发
 * - netdev 接口集成
 */

#include <drivers/e1000.h>
#include <drivers/pci.h>
#include <kernel/io.h>
#include <kernel/irq.h>
#include <kernel/sync/mutex.h>
#include <mm/heap.h>
#include <mm/vmm.h>
#include <lib/klog.h>
#include <lib/string.h>
#include <net/ethernet.h>

/* 最大支持的 E1000 设备数量 */
#define E1000_MAX_DEVICES   4

/* 全局设备数组 */
static e1000_device_t e1000_devices[E1000_MAX_DEVICES];
static int e1000_device_count = 0;

/* 设备访问锁 */
static mutex_t e1000_mutex;

/* ============================================================================
 * 寄存器访问函数
 * ============================================================================ */

/**
 * 读取 MMIO 寄存器
 */
static inline uint32_t e1000_read_reg(e1000_device_t *dev, uint32_t reg) {
    return dev->mmio_base[reg / 4];
}

/**
 * 写入 MMIO 寄存器
 */
static inline void e1000_write_reg(e1000_device_t *dev, uint32_t reg, uint32_t value) {
    dev->mmio_base[reg / 4] = value;
}

/* ============================================================================
 * EEPROM 访问（读取 MAC 地址）
 * ============================================================================ */

/**
 * 从 EEPROM 读取一个字
 */
static uint16_t e1000_eeprom_read(e1000_device_t *dev, uint8_t addr) {
    uint32_t val;
    
    /* 写入读取命令 */
    e1000_write_reg(dev, E1000_REG_EERD, (uint32_t)addr << 8 | 1);
    
    /* 等待读取完成 */
    while (!((val = e1000_read_reg(dev, E1000_REG_EERD)) & (1 << 4))) {
        /* 简单的忙等待，实际应该有超时处理 */
    }
    
    return (uint16_t)(val >> 16);
}

/**
 * 读取 MAC 地址
 */
static void e1000_read_mac_address(e1000_device_t *dev) {
    uint32_t ral, rah;
    
    /* 首先尝试从 RAL/RAH 寄存器读取 */
    ral = e1000_read_reg(dev, E1000_REG_RAL0);
    rah = e1000_read_reg(dev, E1000_REG_RAH0);
    
    if (ral != 0 || (rah & 0xFFFF) != 0) {
        /* 从寄存器读取 */
        dev->mac_addr[0] = ral & 0xFF;
        dev->mac_addr[1] = (ral >> 8) & 0xFF;
        dev->mac_addr[2] = (ral >> 16) & 0xFF;
        dev->mac_addr[3] = (ral >> 24) & 0xFF;
        dev->mac_addr[4] = rah & 0xFF;
        dev->mac_addr[5] = (rah >> 8) & 0xFF;
    } else {
        /* 从 EEPROM 读取 */
        uint16_t word;
        
        word = e1000_eeprom_read(dev, 0);
        dev->mac_addr[0] = word & 0xFF;
        dev->mac_addr[1] = (word >> 8) & 0xFF;
        
        word = e1000_eeprom_read(dev, 1);
        dev->mac_addr[2] = word & 0xFF;
        dev->mac_addr[3] = (word >> 8) & 0xFF;
        
        word = e1000_eeprom_read(dev, 2);
        dev->mac_addr[4] = word & 0xFF;
        dev->mac_addr[5] = (word >> 8) & 0xFF;
    }
}

/* ============================================================================
 * 描述符环初始化
 * ============================================================================ */

/**
 * 初始化接收描述符环
 */
static int e1000_init_rx_ring(e1000_device_t *dev) {
    /* 分配描述符数组（16 字节对齐） */
    uint32_t desc_size = sizeof(e1000_rx_desc_t) * E1000_NUM_RX_DESC;
    dev->rx_descs = (e1000_rx_desc_t *)kmalloc_aligned(desc_size, 16);
    if (!dev->rx_descs) {
        LOG_ERROR_MSG("e1000: Failed to allocate RX descriptors\n");
        return -1;
    }
    memset(dev->rx_descs, 0, desc_size);
    
    /* 获取物理地址 */
    dev->rx_descs_phys = VIRT_TO_PHYS((uint32_t)dev->rx_descs);
    
    /* 为每个描述符分配接收缓冲区 */
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        dev->rx_buffers[i] = (uint8_t *)kmalloc_aligned(E1000_RX_BUFFER_SIZE, 16);
        if (!dev->rx_buffers[i]) {
            LOG_ERROR_MSG("e1000: Failed to allocate RX buffer %d\n", i);
            return -1;
        }
        
        /* 设置描述符 */
        dev->rx_descs[i].buffer_addr = VIRT_TO_PHYS((uint32_t)dev->rx_buffers[i]);
        dev->rx_descs[i].status = 0;
    }
    
    dev->rx_cur = 0;
    
    /* 配置接收描述符寄存器 */
    e1000_write_reg(dev, E1000_REG_RDBAL, dev->rx_descs_phys);
    e1000_write_reg(dev, E1000_REG_RDBAH, 0);  // 32 位系统
    e1000_write_reg(dev, E1000_REG_RDLEN, desc_size);
    e1000_write_reg(dev, E1000_REG_RDH, 0);
    e1000_write_reg(dev, E1000_REG_RDT, E1000_NUM_RX_DESC - 1);
    
    return 0;
}

/**
 * 初始化发送描述符环
 */
static int e1000_init_tx_ring(e1000_device_t *dev) {
    /* 分配描述符数组（16 字节对齐） */
    uint32_t desc_size = sizeof(e1000_tx_desc_t) * E1000_NUM_TX_DESC;
    dev->tx_descs = (e1000_tx_desc_t *)kmalloc_aligned(desc_size, 16);
    if (!dev->tx_descs) {
        LOG_ERROR_MSG("e1000: Failed to allocate TX descriptors\n");
        return -1;
    }
    memset(dev->tx_descs, 0, desc_size);
    
    /* 获取物理地址 */
    dev->tx_descs_phys = VIRT_TO_PHYS((uint32_t)dev->tx_descs);
    
    /* 为每个描述符分配发送缓冲区 */
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        dev->tx_buffers[i] = (uint8_t *)kmalloc_aligned(E1000_RX_BUFFER_SIZE, 16);
        if (!dev->tx_buffers[i]) {
            LOG_ERROR_MSG("e1000: Failed to allocate TX buffer %d\n", i);
            return -1;
        }
        
        /* 设置描述符 */
        dev->tx_descs[i].buffer_addr = VIRT_TO_PHYS((uint32_t)dev->tx_buffers[i]);
        dev->tx_descs[i].status = E1000_TXD_STAT_DD;  // 标记为完成（可用）
        dev->tx_descs[i].cmd = 0;
    }
    
    dev->tx_cur = 0;
    
    /* 配置发送描述符寄存器 */
    e1000_write_reg(dev, E1000_REG_TDBAL, dev->tx_descs_phys);
    e1000_write_reg(dev, E1000_REG_TDBAH, 0);  // 32 位系统
    e1000_write_reg(dev, E1000_REG_TDLEN, desc_size);
    e1000_write_reg(dev, E1000_REG_TDH, 0);
    e1000_write_reg(dev, E1000_REG_TDT, 0);
    
    return 0;
}

/* ============================================================================
 * 设备初始化
 * ============================================================================ */

/**
 * 重置设备
 */
static void e1000_reset(e1000_device_t *dev) {
    uint32_t ctrl;
    
    /* 禁用中断 */
    e1000_write_reg(dev, E1000_REG_IMC, 0xFFFFFFFF);
    
    /* 设备重置 */
    ctrl = e1000_read_reg(dev, E1000_REG_CTRL);
    e1000_write_reg(dev, E1000_REG_CTRL, ctrl | E1000_CTRL_RST);
    
    /* 等待重置完成（约 1ms） */
    for (int i = 0; i < 10000; i++) {
        if (!(e1000_read_reg(dev, E1000_REG_CTRL) & E1000_CTRL_RST)) {
            break;
        }
    }
    
    /* 再次禁用中断 */
    e1000_write_reg(dev, E1000_REG_IMC, 0xFFFFFFFF);
}

/**
 * 初始化接收功能
 */
static void e1000_init_rx(e1000_device_t *dev) {
    uint32_t rctl;
    
    /* 清除多播表 */
    for (int i = 0; i < 128; i++) {
        e1000_write_reg(dev, E1000_REG_MTA + i * 4, 0);
    }
    
    /* 设置接收控制寄存器 */
    rctl = E1000_RCTL_EN |          // 启用接收
           E1000_RCTL_BAM |         // 接受广播
           E1000_RCTL_BSIZE_2048 |  // 2048 字节缓冲区
           E1000_RCTL_SECRC;        // 剥离 CRC
    
    e1000_write_reg(dev, E1000_REG_RCTL, rctl);
}

/**
 * 初始化发送功能
 */
static void e1000_init_tx(e1000_device_t *dev) {
    uint32_t tctl, tipg;
    
    /* 设置发送控制寄存器 */
    tctl = E1000_TCTL_EN |                          // 启用发送
           E1000_TCTL_PSP |                         // 填充短包
           (15 << E1000_TCTL_CT_SHIFT) |            // 冲突阈值
           (64 << E1000_TCTL_COLD_SHIFT);           // 冲突距离
    
    e1000_write_reg(dev, E1000_REG_TCTL, tctl);
    
    /* 设置发送间隔 */
    tipg = E1000_TIPG_IPGT |
           (E1000_TIPG_IPGR1 << 10) |
           (E1000_TIPG_IPGR2 << 20);
    
    e1000_write_reg(dev, E1000_REG_TIPG, tipg);
}

/**
 * 启用中断
 */
static void e1000_enable_interrupts(e1000_device_t *dev) {
    /* 清除所有待处理的中断 */
    e1000_read_reg(dev, E1000_REG_ICR);
    
    /* 启用我们关心的中断 */
    uint32_t ims = E1000_ICR_LSC |       // 链路状态变化
                   E1000_ICR_RXT0 |      // 接收定时器
                   E1000_ICR_RXO |       // 接收溢出
                   E1000_ICR_RXDMT0 |    // 接收描述符最小阈值
                   E1000_ICR_TXDW;       // 发送完成
    
    e1000_write_reg(dev, E1000_REG_IMS, ims);
}

/**
 * 更新链路状态
 */
static void e1000_update_link_status(e1000_device_t *dev) {
    uint32_t status = e1000_read_reg(dev, E1000_REG_STATUS);
    
    dev->link_up = (status & E1000_STATUS_LU) != 0;
    dev->full_duplex = (status & E1000_STATUS_FD) != 0;
    
    uint32_t speed = status & E1000_STATUS_SPEED_MASK;
    switch (speed) {
        case E1000_STATUS_SPEED_10:
            dev->speed = 10;
            break;
        case E1000_STATUS_SPEED_100:
            dev->speed = 100;
            break;
        case E1000_STATUS_SPEED_1000:
            dev->speed = 1000;
            break;
        default:
            dev->speed = 0;
    }
}

/* ============================================================================
 * netdev 接口实现
 * ============================================================================ */

/**
 * 打开设备
 */
static int e1000_netdev_open(netdev_t *netdev) {
    e1000_device_t *dev = (e1000_device_t *)netdev->priv;
    
    /* 设置链路启用 */
    uint32_t ctrl = e1000_read_reg(dev, E1000_REG_CTRL);
    ctrl |= E1000_CTRL_SLU;  // Set Link Up
    e1000_write_reg(dev, E1000_REG_CTRL, ctrl);
    
    /* 启用中断 */
    e1000_enable_interrupts(dev);
    
    /* 更新链路状态 */
    e1000_update_link_status(dev);
    
    return 0;
}

/**
 * 关闭设备
 */
static int e1000_netdev_close(netdev_t *netdev) {
    e1000_device_t *dev = (e1000_device_t *)netdev->priv;
    
    /* 禁用中断 */
    e1000_write_reg(dev, E1000_REG_IMC, 0xFFFFFFFF);
    
    /* 禁用接收和发送 */
    e1000_write_reg(dev, E1000_REG_RCTL, 0);
    e1000_write_reg(dev, E1000_REG_TCTL, 0);
    
    return 0;
}

/**
 * 发送数据包
 */
static int e1000_netdev_transmit(netdev_t *netdev, netbuf_t *buf) {
    e1000_device_t *dev = (e1000_device_t *)netdev->priv;
    
    if (!buf || buf->len == 0 || buf->len > 1518) {
        return -1;
    }
    
    mutex_lock(&e1000_mutex);
    
    uint32_t cur = dev->tx_cur;
    e1000_tx_desc_t *desc = &dev->tx_descs[cur];
    
    /* 等待描述符可用 */
    while (!(desc->status & E1000_TXD_STAT_DD)) {
        /* 忙等待，实际应该有超时处理 */
    }
    
    /* 复制数据到发送缓冲区 */
    memcpy(dev->tx_buffers[cur], buf->data, buf->len);
    
    /* 设置描述符 */
    desc->length = buf->len;
    desc->status = 0;
    desc->cmd = E1000_TXD_CMD_EOP |   // 数据包结束
                E1000_TXD_CMD_IFCS |  // 插入 FCS
                E1000_TXD_CMD_RS;     // 报告状态
    
    /* 更新尾指针，触发发送 */
    dev->tx_cur = (cur + 1) % E1000_NUM_TX_DESC;
    e1000_write_reg(dev, E1000_REG_TDT, dev->tx_cur);
    
    /* 更新统计 */
    dev->tx_packets++;
    dev->tx_bytes += buf->len;
    netdev->tx_packets++;
    netdev->tx_bytes += buf->len;
    
    mutex_unlock(&e1000_mutex);
    
    return 0;
}

/**
 * 设置 MAC 地址
 */
static int e1000_netdev_set_mac(netdev_t *netdev, uint8_t *mac) {
    e1000_device_t *dev = (e1000_device_t *)netdev->priv;
    
    /* 复制 MAC 地址 */
    memcpy(dev->mac_addr, mac, 6);
    memcpy(netdev->mac, mac, 6);
    
    /* 写入 RAL/RAH 寄存器 */
    uint32_t ral = mac[0] | (mac[1] << 8) | (mac[2] << 16) | (mac[3] << 24);
    uint32_t rah = mac[4] | (mac[5] << 8) | (1 << 31);  // AV = 1 (地址有效)
    
    e1000_write_reg(dev, E1000_REG_RAL0, ral);
    e1000_write_reg(dev, E1000_REG_RAH0, rah);
    
    return 0;
}

/* netdev 操作函数表 */
static netdev_ops_t e1000_netdev_ops = {
    .open = e1000_netdev_open,
    .close = e1000_netdev_close,
    .transmit = e1000_netdev_transmit,
    .set_mac = e1000_netdev_set_mac,
};

/* ============================================================================
 * 中断处理
 * ============================================================================ */

/**
 * 处理接收到的数据包
 */
void e1000_receive(e1000_device_t *dev) {
    while (1) {
        uint32_t cur = dev->rx_cur;
        e1000_rx_desc_t *desc = &dev->rx_descs[cur];
        
        /* 检查描述符是否完成 */
        if (!(desc->status & E1000_RXD_STAT_DD)) {
            break;
        }
        
        /* 检查是否是完整的数据包 */
        if (desc->status & E1000_RXD_STAT_EOP) {
            uint16_t len = desc->length;
            
            if (len > 0 && len <= E1000_RX_BUFFER_SIZE) {
                /* 分配网络缓冲区 */
                netbuf_t *buf = netbuf_alloc(len);
                if (buf) {
                    /* 复制数据 */
                    memcpy(netbuf_put(buf, len), dev->rx_buffers[cur], len);
                    buf->dev = &dev->netdev;
                    
                    /* 更新统计 */
                    dev->rx_packets++;
                    dev->rx_bytes += len;
                    dev->netdev.rx_packets++;
                    dev->netdev.rx_bytes += len;
                    
                    /* 传递给网络栈 */
                    netdev_receive(&dev->netdev, buf);
                }
            }
        }
        
        /* 重置描述符 */
        desc->status = 0;
        
        /* 更新 RDT */
        uint32_t old_cur = dev->rx_cur;
        dev->rx_cur = (cur + 1) % E1000_NUM_RX_DESC;
        e1000_write_reg(dev, E1000_REG_RDT, old_cur);
    }
}

/**
 * 中断处理程序
 */
static void e1000_irq_handler(registers_t *regs) {
    (void)regs;
    
    for (int i = 0; i < e1000_device_count; i++) {
        e1000_device_t *dev = &e1000_devices[i];
        
        /* 读取中断原因（读清除） */
        uint32_t icr = e1000_read_reg(dev, E1000_REG_ICR);
        if (icr == 0) {
            continue;
        }
        
        /* 处理接收中断 */
        if (icr & (E1000_ICR_RXT0 | E1000_ICR_RXDMT0 | E1000_ICR_RXO)) {
            e1000_receive(dev);
        }
        
        /* 处理链路状态变化 */
        if (icr & E1000_ICR_LSC) {
            e1000_update_link_status(dev);
            LOG_INFO_MSG("e1000: %s link %s, speed %u Mbps, %s duplex\n",
                        dev->netdev.name,
                        dev->link_up ? "up" : "down",
                        dev->speed,
                        dev->full_duplex ? "full" : "half");
        }
        
        /* 处理发送完成 */
        if (icr & E1000_ICR_TXDW) {
            /* 发送完成，可以回收描述符 */
        }
    }
}

/* ============================================================================
 * 设备检测和初始化
 * ============================================================================ */

/**
 * 检测并初始化单个 E1000 设备
 */
static int e1000_init_device(pci_device_t *pci_dev) {
    if (e1000_device_count >= E1000_MAX_DEVICES) {
        LOG_WARN_MSG("e1000: Maximum devices reached\n");
        return -1;
    }
    
    e1000_device_t *dev = &e1000_devices[e1000_device_count];
    memset(dev, 0, sizeof(e1000_device_t));
    
    /* 保存 PCI 信息 */
    dev->bus = pci_dev->bus;
    dev->slot = pci_dev->slot;
    dev->func = pci_dev->func;
    dev->device_id = pci_dev->device_id;
    dev->irq = pci_dev->interrupt_line;
    
    /* 启用 PCI 总线主控和内存空间 */
    pci_enable_bus_master(pci_dev);
    pci_enable_memory_space(pci_dev);
    
    /* 获取 MMIO 基地址 */
    uint32_t bar0 = pci_get_bar_address(pci_dev, 0);
    if (bar0 == 0) {
        LOG_ERROR_MSG("e1000: Invalid BAR0 address\n");
        return -1;
    }
    
    /* 映射 MMIO 空间 */
    dev->mmio_size = 0x20000;  // 128KB
    uint32_t mmio_virt = vmm_map_mmio(bar0, dev->mmio_size);
    if (!mmio_virt) {
        LOG_ERROR_MSG("e1000: Failed to map MMIO\n");
        return -1;
    }
    dev->mmio_base = (volatile uint32_t *)mmio_virt;
    
    /* 重置设备 */
    e1000_reset(dev);
    
    /* 读取 MAC 地址 */
    e1000_read_mac_address(dev);
    
    /* 设置 MAC 地址寄存器 */
    uint32_t ral = dev->mac_addr[0] | (dev->mac_addr[1] << 8) |
                   (dev->mac_addr[2] << 16) | (dev->mac_addr[3] << 24);
    uint32_t rah = dev->mac_addr[4] | (dev->mac_addr[5] << 8) | (1 << 31);
    e1000_write_reg(dev, E1000_REG_RAL0, ral);
    e1000_write_reg(dev, E1000_REG_RAH0, rah);
    
    /* 初始化描述符环 */
    if (e1000_init_rx_ring(dev) < 0) {
        return -1;
    }
    if (e1000_init_tx_ring(dev) < 0) {
        return -1;
    }
    
    /* 初始化接收和发送 */
    e1000_init_rx(dev);
    e1000_init_tx(dev);
    
    /* 设置链路启用 */
    uint32_t ctrl = e1000_read_reg(dev, E1000_REG_CTRL);
    ctrl |= E1000_CTRL_SLU | E1000_CTRL_ASDE;
    ctrl &= ~E1000_CTRL_LRST;
    ctrl &= ~E1000_CTRL_PHY_RST;
    ctrl &= ~E1000_CTRL_ILOS;
    e1000_write_reg(dev, E1000_REG_CTRL, ctrl);
    
    /* 注册中断处理程序 */
    if (dev->irq != 0 && dev->irq != 0xFF) {
        irq_register_handler(dev->irq, e1000_irq_handler);
        irq_enable_line(dev->irq);
    }
    
    /* 启用中断 */
    e1000_enable_interrupts(dev);
    
    /* 更新链路状态 */
    e1000_update_link_status(dev);
    
    /* 配置 netdev 接口 */
    char name[16];
    snprintf(name, sizeof(name), "eth%d", e1000_device_count);
    strcpy(dev->netdev.name, name);
    memcpy(dev->netdev.mac, dev->mac_addr, 6);
    dev->netdev.mtu = 1500;
    dev->netdev.state = NETDEV_DOWN;
    dev->netdev.ops = &e1000_netdev_ops;
    dev->netdev.priv = dev;
    
    /* 注册网络设备 */
    if (netdev_register(&dev->netdev) < 0) {
        LOG_ERROR_MSG("e1000: Failed to register netdev\n");
        return -1;
    }
    
    e1000_device_count++;
    
    LOG_INFO_MSG("e1000: %s initialized (Device ID: 0x%04x, IRQ: %d)\n",
                name, dev->device_id, dev->irq);
    LOG_INFO_MSG("e1000: MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                dev->mac_addr[0], dev->mac_addr[1], dev->mac_addr[2],
                dev->mac_addr[3], dev->mac_addr[4], dev->mac_addr[5]);
    LOG_INFO_MSG("e1000: Link %s, %u Mbps, %s duplex\n",
                dev->link_up ? "up" : "down",
                dev->speed,
                dev->full_duplex ? "full" : "half");
    
    return 0;
}

/* 支持的设备 ID 列表 */
static const uint16_t e1000_device_ids[] = {
    E1000_DEV_ID_82540EM,
    E1000_DEV_ID_82545EM,
    E1000_DEV_ID_82541,
    E1000_DEV_ID_82543GC,
    E1000_DEV_ID_82574L,
    0  // 结束标记
};

/**
 * 初始化 E1000 驱动
 */
int e1000_init(void) {
    mutex_init(&e1000_mutex);
    e1000_device_count = 0;
    
    /* 扫描 PCI 总线查找 E1000 设备 */
    for (int i = 0; e1000_device_ids[i] != 0; i++) {
        pci_device_t *pci_dev = pci_find_device(E1000_VENDOR_ID, e1000_device_ids[i]);
        if (pci_dev) {
            e1000_init_device(pci_dev);
        }
    }
    
    if (e1000_device_count == 0) {
        LOG_DEBUG_MSG("e1000: No devices found\n");
        return 0;
    }
    
    LOG_INFO_MSG("e1000: Initialized %d device(s)\n", e1000_device_count);
    return e1000_device_count;
}

/* ============================================================================
 * 公共 API
 * ============================================================================ */

e1000_device_t *e1000_get_device(int index) {
    if (index < 0 || index >= e1000_device_count) {
        return NULL;
    }
    return &e1000_devices[index];
}

int e1000_send(e1000_device_t *dev, void *data, uint32_t len) {
    netbuf_t buf;
    buf.data = (uint8_t *)data;
    buf.len = len;
    return e1000_netdev_transmit(&dev->netdev, &buf);
}

void e1000_get_mac(e1000_device_t *dev, uint8_t *mac) {
    memcpy(mac, dev->mac_addr, 6);
}

int e1000_set_enable(e1000_device_t *dev, bool enable) {
    if (enable) {
        return e1000_netdev_open(&dev->netdev);
    } else {
        return e1000_netdev_close(&dev->netdev);
    }
}

bool e1000_link_up(e1000_device_t *dev) {
    e1000_update_link_status(dev);
    return dev->link_up;
}

void e1000_print_info(e1000_device_t *dev) {
    kprintf("E1000 Device Info:\n");
    kprintf("  Name: %s\n", dev->netdev.name);
    kprintf("  PCI: %02x:%02x.%x\n", dev->bus, dev->slot, dev->func);
    kprintf("  Device ID: 0x%04x\n", dev->device_id);
    kprintf("  IRQ: %d\n", dev->irq);
    kprintf("  MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
            dev->mac_addr[0], dev->mac_addr[1], dev->mac_addr[2],
            dev->mac_addr[3], dev->mac_addr[4], dev->mac_addr[5]);
    kprintf("  Link: %s\n", dev->link_up ? "up" : "down");
    kprintf("  Speed: %u Mbps\n", dev->speed);
    kprintf("  Duplex: %s\n", dev->full_duplex ? "full" : "half");
    kprintf("  RX: %llu packets, %llu bytes\n", dev->rx_packets, dev->rx_bytes);
    kprintf("  TX: %llu packets, %llu bytes\n", dev->tx_packets, dev->tx_bytes);
}
```

---

## 内核集成

### 在 kernel.c 中初始化

```c
// kernel.c

#include <drivers/pci.h>
#include <drivers/e1000.h>
#include <net/netdev.h>

void kernel_main(multiboot_info_t *mbi) {
    // ... 其他初始化 ...
    
    // ========================================================================
    // 阶段 5: 网络初始化
    // ========================================================================
    LOG_INFO_MSG("[Stage 5] Initializing network...\n");
    
    // 5.1 初始化 PCI 总线
    pci_init();
    pci_scan_devices();
    LOG_DEBUG_MSG("  [5.1] PCI bus initialized\n");
    
    // 5.2 初始化网络设备子系统
    netdev_init();
    LOG_DEBUG_MSG("  [5.2] Network device subsystem initialized\n");
    
    // 5.3 初始化 E1000 网卡驱动
    int e1000_count = e1000_init();
    LOG_DEBUG_MSG("  [5.3] E1000 driver initialized (%d devices)\n", e1000_count);
    
    // 5.4 配置默认网络设备（如果存在）
    netdev_t *eth0 = netdev_get_by_name("eth0");
    if (eth0) {
        // QEMU 用户模式网络默认配置
        uint32_t ip, netmask, gateway;
        str_to_ip("10.0.2.15", &ip);
        str_to_ip("255.255.255.0", &netmask);
        str_to_ip("10.0.2.2", &gateway);
        netdev_set_ip(eth0, ip, netmask, gateway);
        netdev_up(eth0);
        LOG_INFO_MSG("  Network: eth0 configured (10.0.2.15)\n");
    }
    
    // ... 继续其他初始化 ...
}
```

---

## QEMU 测试配置

### 启动命令

```bash
# 基本配置（用户模式网络）
qemu-system-i386 -kernel castor.bin \
    -netdev user,id=net0 \
    -device e1000,netdev=net0

# 启用调试输出
qemu-system-i386 -kernel castor.bin \
    -netdev user,id=net0 \
    -device e1000,netdev=net0 \
    -serial stdio \
    -d int,cpu_reset

# 端口转发（允许主机访问虚拟机服务）
qemu-system-i386 -kernel castor.bin \
    -netdev user,id=net0,hostfwd=tcp::8080-:80 \
    -device e1000,netdev=net0

# TAP 模式（完全网络访问）
sudo qemu-system-i386 -kernel castor.bin \
    -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
    -device e1000,netdev=net0
```

### QEMU 用户模式网络配置

| 参数 | 默认值 | 说明 |
|------|--------|------|
| 虚拟机 IP | 10.0.2.15 | 分配给虚拟机的 IP |
| 网关 IP | 10.0.2.2 | 虚拟网关（也是 DNS 代理）|
| DNS 服务器 | 10.0.2.3 | 虚拟 DNS 服务器 |
| DHCP 范围 | 10.0.2.15-31 | DHCP 分配范围 |
| 子网掩码 | 255.255.255.0 | 子网掩码 |

---

## 测试用例

### 1. 设备检测测试

```c
void test_e1000_detection(void) {
    e1000_device_t *dev = e1000_get_device(0);
    assert(dev != NULL);
    assert(dev->device_id == E1000_DEV_ID_82540EM);
    
    // 验证 MAC 地址不为空
    bool mac_valid = false;
    for (int i = 0; i < 6; i++) {
        if (dev->mac_addr[i] != 0) {
            mac_valid = true;
            break;
        }
    }
    assert(mac_valid);
    
    kprintf("E1000 detection test passed\n");
}
```

### 2. 数据包发送测试

```c
void test_e1000_transmit(void) {
    netdev_t *eth0 = netdev_get_by_name("eth0");
    assert(eth0 != NULL);
    
    // 构造 ARP 请求
    uint8_t packet[64];
    memset(packet, 0xFF, 6);  // 目的 MAC（广播）
    memcpy(packet + 6, eth0->mac, 6);  // 源 MAC
    packet[12] = 0x08;  // EtherType: ARP
    packet[13] = 0x06;
    // ... 填充 ARP 头部 ...
    
    netbuf_t *buf = netbuf_alloc(64);
    memcpy(netbuf_put(buf, 64), packet, 64);
    
    int ret = netdev_transmit(eth0, buf);
    assert(ret == 0);
    
    netbuf_free(buf);
    kprintf("E1000 transmit test passed\n");
}
```

### 3. Ping 测试（集成网络栈后）

```
// 在 shell 中测试
> ping 10.0.2.2
PING 10.0.2.2: 56 data bytes
64 bytes from 10.0.2.2: icmp_seq=1 ttl=64 time=0.5 ms
64 bytes from 10.0.2.2: icmp_seq=2 ttl=64 time=0.3 ms
64 bytes from 10.0.2.2: icmp_seq=3 ttl=64 time=0.4 ms

--- 10.0.2.2 ping statistics ---
3 packets transmitted, 3 packets received, 0% packet loss
```

---

## 实现顺序

### 阶段 1: 基础设施
1. [ ] 确保 PCI 总线驱动已实现（阶段 12）
2. [ ] 实现 MMIO 映射函数 `vmm_map_mmio()`
3. [ ] 实现对齐内存分配 `kmalloc_aligned()`

### 阶段 2: 设备检测
4. [ ] 实现 E1000 PCI 设备检测
5. [ ] 实现 MMIO 寄存器访问
6. [ ] 实现设备重置
7. [ ] 实现 MAC 地址读取

### 阶段 3: 描述符环
8. [ ] 实现 RX 描述符环初始化
9. [ ] 实现 TX 描述符环初始化
10. [ ] 实现接收控制初始化
11. [ ] 实现发送控制初始化

### 阶段 4: 数据包收发
12. [ ] 实现数据包发送
13. [ ] 实现数据包接收
14. [ ] 实现中断处理

### 阶段 5: netdev 集成
15. [ ] 实现 netdev 操作函数
16. [ ] 注册网络设备
17. [ ] 与网络栈集成（阶段 13）
18. [ ] QEMU 测试

---

## 依赖关系

### 前置依赖

| 依赖模块 | 用途 |
|----------|------|
| PCI 总线驱动（阶段 12） | 设备检测和配置 |
| 虚拟内存管理（VMM） | MMIO 映射 |
| 堆内存管理（heap） | 描述符和缓冲区分配 |
| IRQ 中断处理 | 中断驱动 |
| 网络栈（阶段 13） | netdev 接口、netbuf |

### 提供功能

| 功能 | 使用者 |
|------|--------|
| netdev 接口 | 网络栈以太网层 |
| 数据包收发 | IP、ARP 协议 |

---

## 参考资料

1. **Intel 官方文档**
   - Intel 82540EM Gigabit Ethernet Controller Software Developer's Manual
   - PCI/PCI-X Family of Gigabit Ethernet Controllers Software Developer's Manual

2. **开源实现参考**
   - Linux kernel: `drivers/net/ethernet/intel/e1000/`
   - OSDev Wiki: [Intel Ethernet i217](https://wiki.osdev.org/Intel_Ethernet_i217)
   - xv6: `e1000.c`

3. **QEMU 文档**
   - [QEMU Networking](https://wiki.qemu.org/Documentation/Networking)
   - [QEMU E1000 Implementation](https://github.com/qemu/qemu/blob/master/hw/net/e1000.c)

