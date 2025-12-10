/**
 * @file usb.h
 * @brief USB 核心定义
 * 
 * USB 1.1 驱动核心层，支持设备枚举、描述符解析和 URB 管理
 */

#ifndef _DRIVERS_X86_USB_USB_H_
#define _DRIVERS_X86_USB_USB_H_

#include <types.h>

/* ============================================================================
 * USB 规范常量
 * ============================================================================ */

/* USB 速度 */
#define USB_SPEED_LOW       0   // 1.5 Mbps
#define USB_SPEED_FULL      1   // 12 Mbps

/* 传输类型 */
#define USB_TRANSFER_CONTROL     0
#define USB_TRANSFER_ISOCHRONOUS 1
#define USB_TRANSFER_BULK        2
#define USB_TRANSFER_INTERRUPT   3

/* 端点方向 */
#define USB_DIR_OUT         0x00
#define USB_DIR_IN          0x80
#define USB_DIR_MASK        0x80

/* 描述符类型 */
#define USB_DESC_DEVICE         1
#define USB_DESC_CONFIGURATION  2
#define USB_DESC_STRING         3
#define USB_DESC_INTERFACE      4
#define USB_DESC_ENDPOINT       5
#define USB_DESC_HID            0x21
#define USB_DESC_HID_REPORT     0x22

/* 标准请求 */
#define USB_REQ_GET_STATUS      0x00
#define USB_REQ_CLEAR_FEATURE   0x01
#define USB_REQ_SET_FEATURE     0x03
#define USB_REQ_SET_ADDRESS     0x05
#define USB_REQ_GET_DESCRIPTOR  0x06
#define USB_REQ_SET_DESCRIPTOR  0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE   0x0A
#define USB_REQ_SET_INTERFACE   0x0B

/* 请求类型 */
#define USB_REQTYPE_DIR_MASK    0x80
#define USB_REQTYPE_TYPE_MASK   0x60
#define USB_REQTYPE_RECIP_MASK  0x1F

#define USB_REQTYPE_HOST_TO_DEV 0x00
#define USB_REQTYPE_DEV_TO_HOST 0x80

#define USB_REQTYPE_STANDARD    0x00
#define USB_REQTYPE_CLASS       0x20
#define USB_REQTYPE_VENDOR      0x40

#define USB_REQTYPE_DEVICE      0x00
#define USB_REQTYPE_INTERFACE   0x01
#define USB_REQTYPE_ENDPOINT    0x02
#define USB_REQTYPE_OTHER       0x03

/* 设备类 */
#define USB_CLASS_PER_INTERFACE 0x00    // 类信息在接口描述符中
#define USB_CLASS_AUDIO         0x01
#define USB_CLASS_CDC           0x02
#define USB_CLASS_HID           0x03
#define USB_CLASS_PHYSICAL      0x05
#define USB_CLASS_IMAGE         0x06
#define USB_CLASS_PRINTER       0x07
#define USB_CLASS_MASS_STORAGE  0x08
#define USB_CLASS_HUB           0x09
#define USB_CLASS_CDC_DATA      0x0A
#define USB_CLASS_VENDOR_SPEC   0xFF

/* Mass Storage 子类 */
#define USB_MSC_SUBCLASS_RBC        0x01    // Reduced Block Commands
#define USB_MSC_SUBCLASS_ATAPI      0x02    // ATAPI (CD/DVD)
#define USB_MSC_SUBCLASS_QIC157     0x03    // QIC-157 (tape)
#define USB_MSC_SUBCLASS_UFI        0x04    // UFI (floppy)
#define USB_MSC_SUBCLASS_SFF8070I   0x05    // SFF-8070i
#define USB_MSC_SUBCLASS_SCSI       0x06    // SCSI transparent command set

/* Mass Storage 协议 */
#define USB_MSC_PROTO_CBI_INT       0x00    // Control/Bulk/Interrupt with interrupt
#define USB_MSC_PROTO_CBI           0x01    // Control/Bulk/Interrupt without interrupt
#define USB_MSC_PROTO_BBB           0x50    // Bulk-Only Transport (BOT)

/* PID 类型 */
#define USB_PID_OUT             0xE1
#define USB_PID_IN              0x69
#define USB_PID_SETUP           0x2D

/* 最大值 */
#define USB_MAX_DEVICES         127
#define USB_MAX_ENDPOINTS       16
#define USB_MAX_INTERFACES      8
#define USB_MAX_PACKET_SIZE_LS  8       // 低速最大包大小
#define USB_MAX_PACKET_SIZE_FS  64      // 全速最大包大小

/* Feature 选择器 */
#define USB_FEATURE_ENDPOINT_HALT   0

/* ============================================================================
 * USB 描述符结构
 * ============================================================================ */

/** 设备描述符 */
typedef struct usb_device_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed)) usb_device_descriptor_t;

/** 配置描述符 */
typedef struct usb_configuration_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed)) usb_configuration_descriptor_t;

/** 接口描述符 */
typedef struct usb_interface_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} __attribute__((packed)) usb_interface_descriptor_t;

/** 端点描述符 */
typedef struct usb_endpoint_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed)) usb_endpoint_descriptor_t;

/** SETUP 包 */
typedef struct usb_setup_packet {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_packet_t;

/* ============================================================================
 * USB 设备和端点结构
 * ============================================================================ */

struct usb_device;  // 前向声明

/** USB 端点 */
typedef struct usb_endpoint {
    uint8_t address;            // 端点地址
    uint8_t type;               // 传输类型
    uint16_t max_packet_size;   // 最大包大小
    uint8_t interval;           // 轮询间隔
    uint8_t toggle;             // 数据切换位
} usb_endpoint_t;

/** USB 接口 */
typedef struct usb_interface {
    uint8_t interface_number;
    uint8_t alternate_setting;
    uint8_t class_code;
    uint8_t subclass_code;
    uint8_t protocol;
    uint8_t num_endpoints;
    usb_endpoint_t endpoints[USB_MAX_ENDPOINTS];
    void *driver_data;          // 驱动私有数据
} usb_interface_t;

/** USB 设备 */
typedef struct usb_device {
    uint8_t address;            // 设备地址 (1-127)
    uint8_t speed;              // 设备速度
    uint8_t port;               // 连接的端口号
    struct usb_device *parent;  // 父设备（Hub）
    
    /* 设备描述符 */
    usb_device_descriptor_t device_desc;
    
    /* 端点 0（控制端点） */
    usb_endpoint_t ep0;
    
    /* 接口 */
    uint8_t num_interfaces;
    uint8_t config_value;       // 当前配置值
    usb_interface_t interfaces[USB_MAX_INTERFACES];
    
    /* 完整配置描述符缓存 */
    uint8_t *config_desc_buf;
    uint16_t config_desc_len;
    
    /* 主机控制器 */
    void *hc;                   // 主机控制器指针
    void *hc_data;              // 主机控制器私有数据
    
    /* 设备驱动 */
    void *driver;               // 设备驱动
    void *driver_data;          // 驱动私有数据
    
    /* 链表 */
    struct usb_device *next;
} usb_device_t;

/* ============================================================================
 * URB（USB Request Block）
 * ============================================================================ */

/** URB 状态 */
#define URB_STATUS_PENDING      0
#define URB_STATUS_COMPLETE     1
#define URB_STATUS_ERROR        -1
#define URB_STATUS_STALL        -2
#define URB_STATUS_TIMEOUT      -3
#define URB_STATUS_NAK          -4
#define URB_STATUS_BABBLE       -5

/** URB 标志 */
#define URB_SHORT_NOT_OK        (1 << 0)    // 短包为错误

typedef struct usb_urb usb_urb_t;

/** URB 完成回调 */
typedef void (*urb_complete_t)(usb_urb_t *urb);

/** USB Request Block */
struct usb_urb {
    usb_device_t *device;       // 目标设备
    usb_endpoint_t *endpoint;   // 目标端点
    
    /* 传输数据 */
    usb_setup_packet_t setup;   // SETUP 包（控制传输）
    void *buffer;               // 数据缓冲区
    uint32_t buffer_length;     // 缓冲区长度
    uint32_t actual_length;     // 实际传输长度
    
    /* 状态和标志 */
    int status;                 // URB 状态
    uint32_t flags;             // URB 标志
    
    /* 回调 */
    urb_complete_t complete;    // 完成回调
    void *context;              // 回调上下文
    
    /* 主机控制器使用 */
    void *hc_data;              // 控制器私有数据
    struct usb_urb *next;       // 链表下一个
};

/* ============================================================================
 * USB 驱动结构
 * ============================================================================ */

/** USB 驱动匹配信息 */
typedef struct usb_driver_id {
    uint8_t class_code;         // 设备/接口类（0xFF 匹配任意）
    uint8_t subclass_code;      // 子类（0xFF 匹配任意）
    uint8_t protocol;           // 协议（0xFF 匹配任意）
    uint16_t vendor_id;         // 厂商 ID（0xFFFF 匹配任意）
    uint16_t product_id;        // 产品 ID（0xFFFF 匹配任意）
} usb_driver_id_t;

/** USB 驱动结构 */
typedef struct usb_driver {
    const char *name;
    usb_driver_id_t id;
    int (*probe)(usb_device_t *dev, usb_interface_t *iface);
    void (*disconnect)(usb_device_t *dev, usb_interface_t *iface);
    struct usb_driver *next;
} usb_driver_t;

/* ============================================================================
 * 主机控制器接口
 * ============================================================================ */

/** 主机控制器操作 */
typedef struct usb_hc_ops {
    int (*submit_urb)(void *hc, usb_urb_t *urb);
    int (*cancel_urb)(void *hc, usb_urb_t *urb);
    int (*reset_port)(void *hc, int port);
    int (*enable_port)(void *hc, int port);
    uint16_t (*get_port_status)(void *hc, int port);
    bool (*port_connected)(void *hc, int port);
    bool (*port_low_speed)(void *hc, int port);
    int (*get_port_count)(void *hc);
} usb_hc_ops_t;

/** 主机控制器 */
typedef struct usb_host_controller {
    const char *name;
    void *private_data;
    usb_hc_ops_t *ops;
    uint8_t next_address;       // 下一个可用设备地址
    usb_device_t *devices;      // 设备链表
    struct usb_host_controller *next;
} usb_host_controller_t;

/* ============================================================================
 * 函数声明
 * ============================================================================ */

/**
 * @brief 初始化 USB 子系统
 * @return 0 成功，-1 失败
 */
int usb_init(void);

/**
 * @brief 注册主机控制器
 * @param hc 主机控制器
 * @return 0 成功，-1 失败
 */
int usb_register_hc(usb_host_controller_t *hc);

/**
 * @brief 分配 USB 设备结构
 * @return 设备指针，失败返回 NULL
 */
usb_device_t *usb_alloc_device(void);

/**
 * @brief 释放 USB 设备结构
 * @param dev 设备指针
 */
void usb_free_device(usb_device_t *dev);

/**
 * @brief 分配 URB
 * @return URB 指针，失败返回 NULL
 */
usb_urb_t *usb_alloc_urb(void);

/**
 * @brief 释放 URB
 * @param urb URB 指针
 */
void usb_free_urb(usb_urb_t *urb);

/**
 * @brief 提交 URB（异步）
 * @param urb URB 指针
 * @return 0 成功，-1 失败
 */
int usb_submit_urb(usb_urb_t *urb);

/**
 * @brief 同步控制传输
 * @param dev 设备
 * @param request_type 请求类型
 * @param request 请求代码
 * @param value 值
 * @param index 索引
 * @param data 数据缓冲区
 * @param length 数据长度
 * @param timeout_ms 超时（毫秒）
 * @return 实际传输长度，<0 错误
 */
int usb_control_msg(usb_device_t *dev, uint8_t request_type, uint8_t request,
                    uint16_t value, uint16_t index, void *data, uint16_t length,
                    uint32_t timeout_ms);

/**
 * @brief 同步批量传输
 * @param dev 设备
 * @param endpoint 端点地址
 * @param data 数据缓冲区
 * @param length 数据长度
 * @param actual_length 实际传输长度（输出）
 * @param timeout_ms 超时（毫秒）
 * @return 0 成功，<0 错误
 */
int usb_bulk_transfer(usb_device_t *dev, uint8_t endpoint, void *data,
                      uint32_t length, uint32_t *actual_length, uint32_t timeout_ms);

/**
 * @brief 获取描述符
 * @param dev 设备
 * @param type 描述符类型
 * @param index 描述符索引
 * @param buffer 缓冲区
 * @param length 缓冲区长度
 * @return 实际长度，<0 错误
 */
int usb_get_descriptor(usb_device_t *dev, uint8_t type, uint8_t index,
                       void *buffer, uint16_t length);

/**
 * @brief 设置设备地址
 * @param dev 设备
 * @param address 新地址
 * @return 0 成功，-1 失败
 */
int usb_set_address(usb_device_t *dev, uint8_t address);

/**
 * @brief 设置配置
 * @param dev 设备
 * @param configuration 配置值
 * @return 0 成功，-1 失败
 */
int usb_set_configuration(usb_device_t *dev, uint8_t configuration);

/**
 * @brief 清除端点 HALT
 * @param dev 设备
 * @param endpoint 端点地址
 * @return 0 成功，-1 失败
 */
int usb_clear_halt(usb_device_t *dev, uint8_t endpoint);

/**
 * @brief 设备枚举
 * @param hc 主机控制器
 * @param port 端口号
 * @return 新设备指针，失败返回 NULL
 */
usb_device_t *usb_enumerate_device(usb_host_controller_t *hc, int port);

/**
 * @brief 注册 USB 驱动
 * @param driver 驱动结构
 * @return 0 成功，-1 失败
 */
int usb_register_driver(usb_driver_t *driver);

/**
 * @brief 注销 USB 驱动
 * @param driver 驱动结构
 */
void usb_unregister_driver(usb_driver_t *driver);

/**
 * @brief 查找设备的端点
 * @param dev 设备
 * @param iface_num 接口号
 * @param type 传输类型
 * @param dir 方向
 * @return 端点指针，失败返回 NULL
 */
usb_endpoint_t *usb_find_endpoint(usb_device_t *dev, uint8_t iface_num,
                                   uint8_t type, uint8_t dir);

/**
 * @brief 打印 USB 设备信息
 * @param dev 设备
 */
void usb_print_device_info(usb_device_t *dev);

/**
 * @brief 扫描所有主机控制器的端口
 */
void usb_scan_devices(void);

/**
 * @brief 获取主机控制器链表头
 * @return 主机控制器链表头指针
 */
usb_host_controller_t *usb_get_hc_list(void);

/**
 * @brief 获取 USB 设备数量
 * @return 设备数量
 */
int usb_get_device_count(void);

/**
 * @brief 根据索引获取 USB 设备
 * @param index 设备索引
 * @return 设备指针，失败返回 NULL
 */
usb_device_t *usb_get_device(int index);

/**
 * @brief 断开 USB 设备（热插拔移除）
 * @param hc 主机控制器
 * @param dev 设备指针
 */
void usb_disconnect_device(usb_host_controller_t *hc, usb_device_t *dev);

/**
 * @brief 在指定端口上查找设备
 * @param hc 主机控制器
 * @param port 端口号
 * @return 设备指针，未找到返回 NULL
 */
usb_device_t *usb_find_device_by_port(usb_host_controller_t *hc, int port);

/**
 * @brief 处理端口连接事件
 * @param hc 主机控制器
 * @param port 端口号
 * @return 新设备指针，失败返回 NULL
 */
usb_device_t *usb_handle_port_connect(usb_host_controller_t *hc, int port);

/**
 * @brief 处理端口断开事件
 * @param hc 主机控制器
 * @param port 端口号
 */
void usb_handle_port_disconnect(usb_host_controller_t *hc, int port);

#endif // _DRIVERS_X86_USB_USB_H_

