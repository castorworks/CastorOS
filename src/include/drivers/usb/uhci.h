/**
 * @file uhci.h
 * @brief UHCI (Universal Host Controller Interface) 驱动
 * 
 * Intel 设计的 USB 1.x 主机控制器接口
 */

#ifndef _DRIVERS_USB_UHCI_H_
#define _DRIVERS_USB_UHCI_H_

#include <types.h>
#include <drivers/usb/usb.h>

/* ============================================================================
 * UHCI PCI 标识
 * ============================================================================ */

#define UHCI_PCI_CLASS          0x0C    // Serial Bus Controller
#define UHCI_PCI_SUBCLASS       0x03    // USB Controller
#define UHCI_PCI_PROG_IF        0x00    // UHCI

/* ============================================================================
 * UHCI 寄存器偏移（I/O 端口）
 * ============================================================================ */

#define UHCI_REG_USBCMD         0x00    // USB 命令 (16 bit)
#define UHCI_REG_USBSTS         0x02    // USB 状态 (16 bit)
#define UHCI_REG_USBINTR        0x04    // USB 中断使能 (16 bit)
#define UHCI_REG_FRNUM          0x06    // 帧编号 (16 bit)
#define UHCI_REG_FRBASEADD      0x08    // 帧列表基地址 (32 bit)
#define UHCI_REG_SOFMOD         0x0C    // SOF 修改 (8 bit)
#define UHCI_REG_PORTSC1        0x10    // 端口 1 状态/控制 (16 bit)
#define UHCI_REG_PORTSC2        0x12    // 端口 2 状态/控制 (16 bit)

/* ============================================================================
 * USBCMD 寄存器位
 * ============================================================================ */

#define UHCI_CMD_RS             (1 << 0)    // Run/Stop
#define UHCI_CMD_HCRESET        (1 << 1)    // Host Controller Reset
#define UHCI_CMD_GRESET         (1 << 2)    // Global Reset
#define UHCI_CMD_EGSM           (1 << 3)    // Enter Global Suspend Mode
#define UHCI_CMD_FGR            (1 << 4)    // Force Global Resume
#define UHCI_CMD_SWDBG          (1 << 5)    // Software Debug
#define UHCI_CMD_CF             (1 << 6)    // Configure Flag
#define UHCI_CMD_MAXP           (1 << 7)    // Max Packet (0=32, 1=64)

/* ============================================================================
 * USBSTS 寄存器位
 * ============================================================================ */

#define UHCI_STS_USBINT         (1 << 0)    // USB 中断
#define UHCI_STS_ERROR          (1 << 1)    // USB 错误中断
#define UHCI_STS_RD             (1 << 2)    // Resume Detect
#define UHCI_STS_HSE            (1 << 3)    // Host System Error
#define UHCI_STS_HCPE           (1 << 4)    // Host Controller Process Error
#define UHCI_STS_HCH            (1 << 5)    // Host Controller Halted

/* ============================================================================
 * USBINTR 寄存器位
 * ============================================================================ */

#define UHCI_INTR_TIMEOUT       (1 << 0)    // Timeout/CRC 中断使能
#define UHCI_INTR_RESUME        (1 << 1)    // Resume 中断使能
#define UHCI_INTR_IOC           (1 << 2)    // Interrupt on Complete 使能
#define UHCI_INTR_SP            (1 << 3)    // Short Packet 中断使能

/* ============================================================================
 * PORTSC 寄存器位
 * ============================================================================ */

#define UHCI_PORT_CCS           (1 << 0)    // Current Connect Status
#define UHCI_PORT_CSC           (1 << 1)    // Connect Status Change (W1C)
#define UHCI_PORT_PE            (1 << 2)    // Port Enabled
#define UHCI_PORT_PEC           (1 << 3)    // Port Enable Change (W1C)
#define UHCI_PORT_LS_MASK       (3 << 4)    // Line Status
#define UHCI_PORT_LS_SE0        (0 << 4)    // SE0
#define UHCI_PORT_LS_J          (1 << 4)    // J-state (Full Speed)
#define UHCI_PORT_LS_K          (2 << 4)    // K-state (Low Speed)
#define UHCI_PORT_RD            (1 << 6)    // Resume Detect
#define UHCI_PORT_LSDA          (1 << 8)    // Low Speed Device Attached
#define UHCI_PORT_PR            (1 << 9)    // Port Reset
#define UHCI_PORT_SUSP          (1 << 12)   // Suspend

/* W1C 位掩码 */
#define UHCI_PORT_W1C_MASK      (UHCI_PORT_CSC | UHCI_PORT_PEC)

/* ============================================================================
 * 链接指针位
 * ============================================================================ */

#define UHCI_LP_TERM            (1 << 0)    // Terminate
#define UHCI_LP_QH              (1 << 1)    // QH (vs TD)
#define UHCI_LP_DEPTH           (1 << 2)    // Depth/Breadth (仅 QH)

/* ============================================================================
 * TD 控制/状态位
 * ============================================================================ */

#define UHCI_TD_ACTLEN_MASK     0x7FF       // Actual Length 掩码
#define UHCI_TD_BITSTUFF        (1 << 17)   // Bitstuff Error
#define UHCI_TD_TIMEOUT         (1 << 18)   // CRC/Timeout Error
#define UHCI_TD_NAK             (1 << 19)   // NAK Received
#define UHCI_TD_BABBLE          (1 << 20)   // Babble Detected
#define UHCI_TD_DATA_BUFFER_ERR (1 << 21)   // Data Buffer Error
#define UHCI_TD_STALLED         (1 << 22)   // Stalled
#define UHCI_TD_ACTIVE          (1 << 23)   // Active
#define UHCI_TD_IOC             (1 << 24)   // Interrupt on Complete
#define UHCI_TD_IOS             (1 << 25)   // Isochronous Select
#define UHCI_TD_LS              (1 << 26)   // Low Speed Device
#define UHCI_TD_CERR_SHIFT      27          // Error Counter shift
#define UHCI_TD_CERR_MASK       (3 << 27)   // Error Counter mask
#define UHCI_TD_SPD             (1 << 29)   // Short Packet Detect

/* TD Token 位 */
#define UHCI_TD_PID_OUT         0xE1
#define UHCI_TD_PID_IN          0x69
#define UHCI_TD_PID_SETUP       0x2D

/* ============================================================================
 * UHCI 数据结构
 * ============================================================================ */

#define UHCI_FRAME_LIST_SIZE    1024
#define UHCI_TD_POOL_SIZE       256
#define UHCI_QH_POOL_SIZE       32
#define UHCI_NUM_PORTS          2

/** 传输描述符（16 字节对齐） */
typedef struct uhci_td {
    volatile uint32_t link;         // 链接指针
    volatile uint32_t ctrl_status;  // 控制和状态
    volatile uint32_t token;        // 令牌
    volatile uint32_t buffer;       // 数据缓冲区物理地址
    
    /* 软件使用（硬件不访问） */
    uint32_t phys_addr;             // 此 TD 的物理地址
    usb_urb_t *urb;                 // 关联的 URB
    struct uhci_td *next;           // 链表下一个
    uint32_t pad;                   // 填充到 32 字节
} __attribute__((packed, aligned(16))) uhci_td_t;

/** 队列头（16 字节对齐） */
typedef struct uhci_qh {
    volatile uint32_t head;         // 水平链接指针
    volatile uint32_t element;      // 垂直链接指针（TD）
    
    /* 软件使用 */
    uint32_t phys_addr;             // 此 QH 的物理地址
    uhci_td_t *first_td;            // 第一个 TD
    uhci_td_t *last_td;             // 最后一个 TD
    struct uhci_qh *next;           // 链表下一个
    uint32_t pad[2];                // 填充到 32 字节
} __attribute__((packed, aligned(16))) uhci_qh_t;

/** UHCI 控制器结构 */
typedef struct uhci_controller {
    /* PCI 信息 */
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t irq;
    
    /* I/O 端口基地址 */
    uint16_t io_base;
    
    /* 帧列表（4KB 对齐） */
    uint32_t *frame_list;           // 虚拟地址
    uint32_t frame_list_phys;       // 物理地址
    
    /* QH 链表 */
    uhci_qh_t *qh_int;              // 中断传输 QH
    uhci_qh_t *qh_ctrl;             // 控制传输 QH
    uhci_qh_t *qh_bulk;             // 批量传输 QH
    
    /* TD/QH 内存池 */
    uhci_td_t *td_pool;             // TD 池
    uhci_qh_t *qh_pool;             // QH 池
    uint32_t td_pool_phys;          // TD 池物理地址
    uint32_t qh_pool_phys;          // QH 池物理地址
    
    /* 空闲链表 */
    uhci_td_t *free_tds;            // 空闲 TD 链表
    uhci_qh_t *free_qhs;            // 空闲 QH 链表
    
    /* 活动 QH */
    uhci_qh_t *active_ctrl_qh;      // 活动的控制传输 QH
    uhci_qh_t *active_bulk_qh;      // 活动的批量传输 QH
    
    /* 挂起的 URB */
    usb_urb_t *pending_urbs;        // 挂起的 URB 链表
    
    /* USB 核心主机控制器 */
    usb_host_controller_t usb_hc;
    
    /* 热插拔支持 */
    uint16_t port_status[UHCI_NUM_PORTS];   // 上次记录的端口状态
    usb_device_t *port_device[UHCI_NUM_PORTS]; // 端口上的设备
} uhci_controller_t;

/* ============================================================================
 * 函数声明
 * ============================================================================ */

/**
 * @brief 初始化 UHCI 驱动
 * @return 检测到的控制器数量
 */
int uhci_init(void);

/**
 * @brief 获取 UHCI 控制器
 * @param index 控制器索引
 * @return 控制器指针，失败返回 NULL
 */
uhci_controller_t *uhci_get_controller(int index);

/**
 * @brief 提交 URB 到 UHCI 控制器
 * @param hc 控制器指针
 * @param urb URB 指针
 * @return 0 成功，-1 失败
 */
int uhci_submit_urb(uhci_controller_t *hc, usb_urb_t *urb);

/**
 * @brief 复位端口
 * @param hc 控制器
 * @param port 端口号 (0 或 1)
 * @return 0 成功，-1 失败
 */
int uhci_reset_port(uhci_controller_t *hc, int port);

/**
 * @brief 启用端口
 * @param hc 控制器
 * @param port 端口号
 * @return 0 成功，-1 失败
 */
int uhci_enable_port(uhci_controller_t *hc, int port);

/**
 * @brief 获取端口状态
 * @param hc 控制器
 * @param port 端口号
 * @return 端口状态
 */
uint16_t uhci_get_port_status(uhci_controller_t *hc, int port);

/**
 * @brief 检查端口是否有设备连接
 * @param hc 控制器
 * @param port 端口号
 * @return true 有设备连接
 */
bool uhci_port_connected(uhci_controller_t *hc, int port);

/**
 * @brief 检查端口是否为低速设备
 * @param hc 控制器
 * @param port 端口号
 * @return true 低速设备
 */
bool uhci_port_low_speed(uhci_controller_t *hc, int port);

/**
 * @brief 打印 UHCI 控制器信息
 * @param hc 控制器指针
 */
void uhci_print_info(uhci_controller_t *hc);

/**
 * @brief 检查端口状态变化（热插拔检测）
 * @param hc 控制器指针
 */
void uhci_check_port_changes(uhci_controller_t *hc);

/**
 * @brief 处理所有控制器的端口状态变化
 */
void uhci_poll_port_changes(void);

/**
 * @brief 获取控制器数量
 * @return 控制器数量
 */
int uhci_get_controller_count(void);

/**
 * @brief 同步端口设备映射
 * 在初始设备扫描后调用，建立端口到设备的映射
 */
void uhci_sync_port_devices(void);

/**
 * @brief 启动热插拔监控
 * 注册周期性定时器检查端口状态变化
 */
void uhci_start_hotplug_monitor(void);

/**
 * @brief 停止热插拔监控
 */
void uhci_stop_hotplug_monitor(void);

#endif // _DRIVERS_USB_UHCI_H_

