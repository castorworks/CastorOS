/**
 * @file pci.h
 * @brief PCI 总线驱动
 * 
 * 实现 PCI 配置空间访问、设备枚举和资源管理
 */

#ifndef _DRIVERS_PCI_H_
#define _DRIVERS_PCI_H_

#include <types.h>

/* ============================================================================
 * PCI 配置空间端口
 * ============================================================================ */

#define PCI_CONFIG_ADDR     0xCF8   ///< 配置地址端口
#define PCI_CONFIG_DATA     0xCFC   ///< 配置数据端口

/* ============================================================================
 * PCI 配置空间寄存器偏移
 * ============================================================================ */

#define PCI_VENDOR_ID       0x00    ///< Vendor ID (16 bit)
#define PCI_DEVICE_ID       0x02    ///< Device ID (16 bit)
#define PCI_COMMAND         0x04    ///< Command (16 bit)
#define PCI_STATUS          0x06    ///< Status (16 bit)
#define PCI_REVISION_ID     0x08    ///< Revision ID (8 bit)
#define PCI_PROG_IF         0x09    ///< Programming Interface (8 bit)
#define PCI_SUBCLASS        0x0A    ///< Subclass (8 bit)
#define PCI_CLASS           0x0B    ///< Class Code (8 bit)
#define PCI_CACHE_LINE_SIZE 0x0C    ///< Cache Line Size (8 bit)
#define PCI_LATENCY_TIMER   0x0D    ///< Latency Timer (8 bit)
#define PCI_HEADER_TYPE     0x0E    ///< Header Type (8 bit)
#define PCI_BIST            0x0F    ///< Built-in Self Test (8 bit)
#define PCI_BAR0            0x10    ///< Base Address Register 0
#define PCI_BAR1            0x14    ///< Base Address Register 1
#define PCI_BAR2            0x18    ///< Base Address Register 2
#define PCI_BAR3            0x1C    ///< Base Address Register 3
#define PCI_BAR4            0x20    ///< Base Address Register 4
#define PCI_BAR5            0x24    ///< Base Address Register 5
#define PCI_CARDBUS_CIS     0x28    ///< CardBus CIS Pointer
#define PCI_SUBSYS_VENDOR_ID 0x2C   ///< Subsystem Vendor ID
#define PCI_SUBSYS_ID       0x2E    ///< Subsystem ID
#define PCI_ROM_ADDRESS     0x30    ///< Expansion ROM Base Address
#define PCI_CAPABILITIES    0x34    ///< Capabilities Pointer
#define PCI_INTERRUPT_LINE  0x3C    ///< Interrupt Line (8 bit)
#define PCI_INTERRUPT_PIN   0x3D    ///< Interrupt Pin (8 bit)
#define PCI_MIN_GRANT       0x3E    ///< Min Grant (8 bit)
#define PCI_MAX_LATENCY     0x3F    ///< Max Latency (8 bit)

/* ============================================================================
 * PCI 命令寄存器位
 * ============================================================================ */

#define PCI_CMD_IO_SPACE        (1 << 0)    ///< I/O 空间使能
#define PCI_CMD_MEMORY_SPACE    (1 << 1)    ///< 内存空间使能
#define PCI_CMD_BUS_MASTER      (1 << 2)    ///< 总线主控使能
#define PCI_CMD_SPECIAL_CYCLES  (1 << 3)    ///< 特殊周期使能
#define PCI_CMD_MWI_ENABLE      (1 << 4)    ///< 内存写入无效使能
#define PCI_CMD_VGA_PALETTE     (1 << 5)    ///< VGA 调色板侦测
#define PCI_CMD_PARITY_ERROR    (1 << 6)    ///< 奇偶校验错误响应
#define PCI_CMD_SERR_ENABLE     (1 << 8)    ///< SERR# 使能
#define PCI_CMD_FAST_B2B        (1 << 9)    ///< 快速背靠背使能
#define PCI_CMD_INTX_DISABLE    (1 << 10)   ///< INTx 中断禁用

/* ============================================================================
 * BAR 类型
 * ============================================================================ */

#define PCI_BAR_TYPE_MASK       0x01    ///< BAR 类型掩码
#define PCI_BAR_TYPE_MEMORY     0x00    ///< 内存 BAR
#define PCI_BAR_TYPE_IO         0x01    ///< I/O BAR
#define PCI_BAR_MEM_TYPE_MASK   0x06    ///< 内存类型掩码
#define PCI_BAR_MEM_TYPE_32     0x00    ///< 32 位内存空间
#define PCI_BAR_MEM_TYPE_64     0x04    ///< 64 位内存空间
#define PCI_BAR_MEM_PREFETCH    0x08    ///< 可预取

/* ============================================================================
 * 常用 PCI 类别代码
 * ============================================================================ */

#define PCI_CLASS_NETWORK       0x02    ///< 网络控制器
#define PCI_CLASS_DISPLAY       0x03    ///< 显示控制器
#define PCI_CLASS_MULTIMEDIA    0x04    ///< 多媒体控制器
#define PCI_CLASS_STORAGE       0x01    ///< 存储控制器
#define PCI_CLASS_BRIDGE        0x06    ///< 桥接设备

/* ============================================================================
 * 最大值定义
 * ============================================================================ */

#define PCI_MAX_BUS         256     ///< 最大总线数
#define PCI_MAX_SLOT        32      ///< 每条总线最大设备数
#define PCI_MAX_FUNC        8       ///< 每个设备最大功能数
#define PCI_MAX_DEVICES     64      ///< 驱动支持的最大设备数

/* ============================================================================
 * PCI 设备结构
 * ============================================================================ */

typedef struct pci_device {
    uint8_t bus;            ///< 总线号
    uint8_t slot;           ///< 插槽号（设备号）
    uint8_t func;           ///< 功能号
    
    uint16_t vendor_id;     ///< 厂商 ID
    uint16_t device_id;     ///< 设备 ID
    
    uint8_t class_code;     ///< 类别代码
    uint8_t subclass;       ///< 子类别
    uint8_t prog_if;        ///< 编程接口
    uint8_t revision;       ///< 修订版本
    
    uint8_t header_type;    ///< 头类型
    uint8_t interrupt_line; ///< 中断线（IRQ）
    uint8_t interrupt_pin;  ///< 中断引脚
    
    uint32_t bar[6];        ///< 基地址寄存器
    uint32_t bar_size[6];   ///< BAR 大小
    uint8_t bar_type[6];    ///< BAR 类型（内存/IO）
} pci_device_t;

/* ============================================================================
 * 函数声明
 * ============================================================================ */

/**
 * @brief 初始化 PCI 总线驱动
 */
void pci_init(void);

/**
 * @brief 扫描所有 PCI 设备
 * @return 发现的设备数量
 */
int pci_scan_devices(void);

/**
 * @brief 根据厂商 ID 和设备 ID 查找设备
 * @param vendor_id 厂商 ID
 * @param device_id 设备 ID
 * @return 设备指针，未找到返回 NULL
 */
pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id);

/**
 * @brief 根据类别查找设备
 * @param class_code 类别代码
 * @param subclass 子类别（0xFF 表示任意）
 * @return 设备指针，未找到返回 NULL
 */
pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass);

/**
 * @brief 获取设备数量
 * @return 已发现的设备数量
 */
int pci_get_device_count(void);

/**
 * @brief 获取设备（按索引）
 * @param index 设备索引
 * @return 设备指针，索引无效返回 NULL
 */
pci_device_t *pci_get_device(int index);

/**
 * @brief 读取 PCI 配置空间（8 位）
 * @param bus 总线号
 * @param slot 插槽号
 * @param func 功能号
 * @param offset 寄存器偏移
 * @return 读取的值
 */
uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

/**
 * @brief 读取 PCI 配置空间（16 位）
 * @param bus 总线号
 * @param slot 插槽号
 * @param func 功能号
 * @param offset 寄存器偏移
 * @return 读取的值
 */
uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

/**
 * @brief 读取 PCI 配置空间（32 位）
 * @param bus 总线号
 * @param slot 插槽号
 * @param func 功能号
 * @param offset 寄存器偏移
 * @return 读取的值
 */
uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

/**
 * @brief 写入 PCI 配置空间（8 位）
 * @param bus 总线号
 * @param slot 插槽号
 * @param func 功能号
 * @param offset 寄存器偏移
 * @param value 要写入的值
 */
void pci_write_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value);

/**
 * @brief 写入 PCI 配置空间（16 位）
 * @param bus 总线号
 * @param slot 插槽号
 * @param func 功能号
 * @param offset 寄存器偏移
 * @param value 要写入的值
 */
void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);

/**
 * @brief 写入 PCI 配置空间（32 位）
 * @param bus 总线号
 * @param slot 插槽号
 * @param func 功能号
 * @param offset 寄存器偏移
 * @param value 要写入的值
 */
void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

/**
 * @brief 启用设备的总线主控功能
 * @param dev 设备指针
 */
void pci_enable_bus_master(pci_device_t *dev);

/**
 * @brief 启用设备的内存空间访问
 * @param dev 设备指针
 */
void pci_enable_memory_space(pci_device_t *dev);

/**
 * @brief 启用设备的 I/O 空间访问
 * @param dev 设备指针
 */
void pci_enable_io_space(pci_device_t *dev);

/**
 * @brief 获取 BAR 地址
 * @param dev 设备指针
 * @param bar_index BAR 索引（0-5）
 * @return BAR 基地址，失败返回 0
 */
uint32_t pci_get_bar_address(pci_device_t *dev, int bar_index);

/**
 * @brief 获取 BAR 大小
 * @param dev 设备指针
 * @param bar_index BAR 索引（0-5）
 * @return BAR 大小，失败返回 0
 */
uint32_t pci_get_bar_size(pci_device_t *dev, int bar_index);

/**
 * @brief 检查 BAR 是否为 I/O 类型
 * @param dev 设备指针
 * @param bar_index BAR 索引（0-5）
 * @return true 如果是 I/O BAR
 */
bool pci_bar_is_io(pci_device_t *dev, int bar_index);

/**
 * @brief 打印设备信息
 * @param dev 设备指针
 */
void pci_print_device(pci_device_t *dev);

/**
 * @brief 打印所有已发现的设备
 */
void pci_print_all_devices(void);

#endif // _DRIVERS_PCI_H_

