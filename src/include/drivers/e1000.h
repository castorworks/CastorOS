/**
 * @file e1000.h
 * @brief Intel E1000 千兆以太网控制器驱动
 * 
 * 支持型号: 82540EM (QEMU), 82540EP-A, 82540EP Mobile (ThinkPad T41), 
 *          82545EM, 82541, 82541GI (ThinkPad X40), 82543GC, 82574L
 */

#ifndef _DRIVERS_E1000_H_
#define _DRIVERS_E1000_H_

#include <types.h>
#include <net/netdev.h>
#include <net/netbuf.h>

/* ============================================================================
 * PCI 标识
 * ============================================================================ */

#define E1000_VENDOR_ID         0x8086  ///< Intel

/* 支持的设备 ID 列表 */
#define E1000_DEV_ID_82540EM        0x100E  ///< QEMU 默认
#define E1000_DEV_ID_82540EP        0x1017  ///< 82540EP-A
#define E1000_DEV_ID_82540EP_M      0x101E  ///< 82540EP Mobile (ThinkPad T41)
#define E1000_DEV_ID_82545EM        0x100F
#define E1000_DEV_ID_82541          0x1019
#define E1000_DEV_ID_82541GI        0x1076  ///< ThinkPad X40 等
#define E1000_DEV_ID_82541GI_LF     0x107C  ///< 82541GI 低功耗版
#define E1000_DEV_ID_82543GC        0x1004
#define E1000_DEV_ID_82574L         0x10D3

/* ============================================================================
 * 寄存器偏移
 * ============================================================================ */

/* 通用寄存器 */
#define E1000_REG_CTRL      0x0000      ///< 设备控制
#define E1000_REG_STATUS    0x0008      ///< 设备状态
#define E1000_REG_EECD      0x0010      ///< EEPROM/Flash 控制
#define E1000_REG_EERD      0x0014      ///< EEPROM 读取
#define E1000_REG_CTRL_EXT  0x0018      ///< 扩展设备控制
#define E1000_REG_MDIC      0x0020      ///< MDI 控制

/* 中断寄存器 */
#define E1000_REG_ICR       0x00C0      ///< 中断原因读取（读清除）
#define E1000_REG_ITR       0x00C4      ///< 中断节流
#define E1000_REG_ICS       0x00C8      ///< 中断原因设置
#define E1000_REG_IMS       0x00D0      ///< 中断掩码设置
#define E1000_REG_IMC       0x00D8      ///< 中断掩码清除

/* 接收寄存器 */
#define E1000_REG_RCTL      0x0100      ///< 接收控制
#define E1000_REG_RDBAL     0x2800      ///< RX 描述符基地址低
#define E1000_REG_RDBAH     0x2804      ///< RX 描述符基地址高
#define E1000_REG_RDLEN     0x2808      ///< RX 描述符长度
#define E1000_REG_RDH       0x2810      ///< RX 描述符头
#define E1000_REG_RDT       0x2818      ///< RX 描述符尾
#define E1000_REG_RDTR      0x2820      ///< RX 延迟定时器

/* 发送寄存器 */
#define E1000_REG_TCTL      0x0400      ///< 发送控制
#define E1000_REG_TIPG      0x0410      ///< 发送间隔
#define E1000_REG_TDBAL     0x3800      ///< TX 描述符基地址低
#define E1000_REG_TDBAH     0x3804      ///< TX 描述符基地址高
#define E1000_REG_TDLEN     0x3808      ///< TX 描述符长度
#define E1000_REG_TDH       0x3810      ///< TX 描述符头
#define E1000_REG_TDT       0x3818      ///< TX 描述符尾

/* MAC 地址寄存器 */
#define E1000_REG_RAL0      0x5400      ///< 接收地址低（MAC 低 32 位）
#define E1000_REG_RAH0      0x5404      ///< 接收地址高（MAC 高 16 位）

/* 统计寄存器 */
#define E1000_REG_GPRC      0x4074      ///< 好的接收包数
#define E1000_REG_GPTC      0x4080      ///< 好的发送包数
#define E1000_REG_GORCL     0x4088      ///< 好的接收字节数（低）
#define E1000_REG_GORCH     0x408C      ///< 好的接收字节数（高）
#define E1000_REG_GOTCL     0x4090      ///< 好的发送字节数（低）
#define E1000_REG_GOTCH     0x4094      ///< 好的发送字节数（高）

/* MTA (Multicast Table Array) */
#define E1000_REG_MTA       0x5200      ///< 多播表数组（128 个 32 位寄存器）

/* ============================================================================
 * 控制寄存器位
 * ============================================================================ */

#define E1000_CTRL_FD       (1 << 0)    ///< 全双工
#define E1000_CTRL_LRST     (1 << 3)    ///< 链路重置
#define E1000_CTRL_ASDE     (1 << 5)    ///< 自动速度检测启用
#define E1000_CTRL_SLU      (1 << 6)    ///< Set Link Up
#define E1000_CTRL_ILOS     (1 << 7)    ///< 反转信号丢失
#define E1000_CTRL_RST      (1 << 26)   ///< 设备重置
#define E1000_CTRL_VME      (1 << 30)   ///< VLAN 模式启用
#define E1000_CTRL_PHY_RST  (1 << 31)   ///< PHY 重置

/* ============================================================================
 * 状态寄存器位
 * ============================================================================ */

#define E1000_STATUS_FD     (1 << 0)    ///< 全双工
#define E1000_STATUS_LU     (1 << 1)    ///< 链路已建立
#define E1000_STATUS_TXOFF  (1 << 4)    ///< 传输暂停
#define E1000_STATUS_SPEED_MASK  0xC0
#define E1000_STATUS_SPEED_10    0x00
#define E1000_STATUS_SPEED_100   0x40
#define E1000_STATUS_SPEED_1000  0x80

/* ============================================================================
 * 中断位
 * ============================================================================ */

#define E1000_ICR_TXDW      (1 << 0)    ///< TX 描述符写回
#define E1000_ICR_TXQE      (1 << 1)    ///< TX 队列空
#define E1000_ICR_LSC       (1 << 2)    ///< 链路状态变化
#define E1000_ICR_RXSEQ     (1 << 3)    ///< RX 序列错误
#define E1000_ICR_RXDMT0    (1 << 4)    ///< RX 描述符最小阈值
#define E1000_ICR_RXO       (1 << 6)    ///< RX 溢出
#define E1000_ICR_RXT0      (1 << 7)    ///< RX 定时器中断

/* ============================================================================
 * 接收控制寄存器位
 * ============================================================================ */

#define E1000_RCTL_EN       (1 << 1)    ///< 接收启用
#define E1000_RCTL_SBP      (1 << 2)    ///< 存储坏包
#define E1000_RCTL_UPE      (1 << 3)    ///< 单播混杂模式
#define E1000_RCTL_MPE      (1 << 4)    ///< 多播混杂模式
#define E1000_RCTL_LPE      (1 << 5)    ///< 长包启用
#define E1000_RCTL_LBM_MASK 0xC0        ///< 环回模式掩码
#define E1000_RCTL_LBM_NO   0x00        ///< 无环回
#define E1000_RCTL_RDMTS_HALF   0x000   ///< RX 描述符最小阈值 = 1/2
#define E1000_RCTL_RDMTS_QUARTER 0x100  ///< RX 描述符最小阈值 = 1/4
#define E1000_RCTL_RDMTS_EIGHTH  0x200  ///< RX 描述符最小阈值 = 1/8
#define E1000_RCTL_MO_36    0x0000      ///< 多播偏移 36
#define E1000_RCTL_MO_35    0x1000      ///< 多播偏移 35
#define E1000_RCTL_MO_34    0x2000      ///< 多播偏移 34
#define E1000_RCTL_MO_32    0x3000      ///< 多播偏移 32
#define E1000_RCTL_BAM      (1 << 15)   ///< 广播接受模式
#define E1000_RCTL_BSIZE_2048   0x00000 ///< 缓冲区大小 2048
#define E1000_RCTL_BSIZE_1024   0x10000 ///< 缓冲区大小 1024
#define E1000_RCTL_BSIZE_512    0x20000 ///< 缓冲区大小 512
#define E1000_RCTL_BSIZE_256    0x30000 ///< 缓冲区大小 256
#define E1000_RCTL_BSEX     (1 << 25)   ///< 缓冲区大小扩展
#define E1000_RCTL_SECRC    (1 << 26)   ///< 剥离以太网 CRC

/* ============================================================================
 * 发送控制寄存器位
 * ============================================================================ */

#define E1000_TCTL_EN       (1 << 1)    ///< 发送启用
#define E1000_TCTL_PSP      (1 << 3)    ///< 填充短包
#define E1000_TCTL_CT_SHIFT 4           ///< 冲突阈值位移
#define E1000_TCTL_COLD_SHIFT 12        ///< 冲突距离位移
#define E1000_TCTL_SWXOFF   (1 << 22)   ///< 软件 XOFF
#define E1000_TCTL_RTLC     (1 << 24)   ///< 重传晚期冲突

/* TIPG 默认值 */
#define E1000_TIPG_IPGT     10          ///< IPG 传输时间
#define E1000_TIPG_IPGR1    8           ///< IPG 接收时间 1
#define E1000_TIPG_IPGR2    6           ///< IPG 接收时间 2

/* ============================================================================
 * 描述符定义
 * ============================================================================ */

/**
 * @brief 接收描述符（Legacy 格式）
 */
typedef struct e1000_rx_desc {
    uint64_t buffer_addr;       ///< 缓冲区物理地址
    uint16_t length;            ///< 接收到的数据长度
    uint16_t checksum;          ///< 数据包校验和
    uint8_t  status;            ///< 状态
    uint8_t  errors;            ///< 错误
    uint16_t special;           ///< 特殊字段（VLAN 标签）
} __attribute__((packed)) e1000_rx_desc_t;

/* 接收描述符状态位 */
#define E1000_RXD_STAT_DD   (1 << 0)    ///< 描述符完成
#define E1000_RXD_STAT_EOP  (1 << 1)    ///< 数据包结束
#define E1000_RXD_STAT_IXSM (1 << 2)    ///< 忽略校验和
#define E1000_RXD_STAT_VP   (1 << 3)    ///< VLAN 数据包
#define E1000_RXD_STAT_TCPCS (1 << 5)   ///< TCP 校验和已计算
#define E1000_RXD_STAT_IPCS (1 << 6)    ///< IP 校验和已计算
#define E1000_RXD_STAT_PIF  (1 << 7)    ///< 传递完整帧

/**
 * @brief 发送描述符（Legacy 格式）
 */
typedef struct e1000_tx_desc {
    uint64_t buffer_addr;       ///< 缓冲区物理地址
    uint16_t length;            ///< 数据长度
    uint8_t  cso;               ///< 校验和偏移
    uint8_t  cmd;               ///< 命令
    uint8_t  status;            ///< 状态
    uint8_t  css;               ///< 校验和起始
    uint16_t special;           ///< 特殊字段
} __attribute__((packed)) e1000_tx_desc_t;

/* 发送描述符命令位 */
#define E1000_TXD_CMD_EOP   (1 << 0)    ///< 数据包结束
#define E1000_TXD_CMD_IFCS  (1 << 1)    ///< 插入 FCS
#define E1000_TXD_CMD_IC    (1 << 2)    ///< 插入校验和
#define E1000_TXD_CMD_RS    (1 << 3)    ///< 报告状态
#define E1000_TXD_CMD_DEXT  (1 << 5)    ///< 描述符扩展
#define E1000_TXD_CMD_VLE   (1 << 6)    ///< VLAN 包启用
#define E1000_TXD_CMD_IDE   (1 << 7)    ///< 中断延迟启用

/* 发送描述符状态位 */
#define E1000_TXD_STAT_DD   (1 << 0)    ///< 描述符完成
#define E1000_TXD_STAT_EC   (1 << 1)    ///< 过多冲突
#define E1000_TXD_STAT_LC   (1 << 2)    ///< 晚期冲突
#define E1000_TXD_STAT_TU   (1 << 3)    ///< 传输下溢

/* ============================================================================
 * 驱动配置
 * ============================================================================ */

#define E1000_NUM_RX_DESC   32          ///< 接收描述符数量（必须是 8 的倍数）
#define E1000_NUM_TX_DESC   32          ///< 发送描述符数量（必须是 8 的倍数）
#define E1000_RX_BUFFER_SIZE 2048       ///< 接收缓冲区大小

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
    e1000_rx_desc_t *rx_descs;          ///< 描述符数组（物理地址对齐）
    uint32_t rx_descs_phys;              ///< 描述符数组物理地址
    uint8_t *rx_buffers[E1000_NUM_RX_DESC]; ///< 接收缓冲区数组
    uint32_t rx_cur;                     ///< 当前接收描述符索引
    
    /* 发送描述符环 */
    e1000_tx_desc_t *tx_descs;          ///< 描述符数组（物理地址对齐）
    uint32_t tx_descs_phys;              ///< 描述符数组物理地址
    uint8_t *tx_buffers[E1000_NUM_TX_DESC]; ///< 发送缓冲区数组
    uint32_t tx_cur;                     ///< 当前发送描述符索引
    
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
    uint32_t speed;                      ///< Mbps
    bool full_duplex;
} e1000_device_t;

/* ============================================================================
 * 函数声明
 * ============================================================================ */

/**
 * @brief 初始化 E1000 驱动
 * 扫描 PCI 总线，检测并初始化所有 E1000 网卡
 * @return 检测到的网卡数量，-1 表示错误
 */
int e1000_init(void);

/**
 * @brief 获取 E1000 设备
 * @param index 设备索引
 * @return 设备指针，不存在返回 NULL
 */
e1000_device_t *e1000_get_device(int index);

/**
 * @brief 发送数据包
 * @param dev 设备指针
 * @param data 数据指针
 * @param len 数据长度
 * @return 0 成功，-1 失败
 */
int e1000_send(e1000_device_t *dev, void *data, uint32_t len);

/**
 * @brief 接收数据包（由中断处理程序调用）
 * @param dev 设备指针
 */
void e1000_receive(e1000_device_t *dev);

/**
 * @brief 获取 MAC 地址
 * @param dev 设备指针
 * @param mac 输出 MAC 地址（6 字节）
 */
void e1000_get_mac(e1000_device_t *dev, uint8_t *mac);

/**
 * @brief 启用/禁用设备
 * @param dev 设备指针
 * @param enable true 启用，false 禁用
 * @return 0 成功，-1 失败
 */
int e1000_set_enable(e1000_device_t *dev, bool enable);

/**
 * @brief 获取链路状态
 * @param dev 设备指针
 * @return true 链路已建立，false 链路断开
 */
bool e1000_link_up(e1000_device_t *dev);

/**
 * @brief 打印设备信息（调试用）
 * @param dev 设备指针
 */
void e1000_print_info(e1000_device_t *dev);

#endif // _DRIVERS_E1000_H_

