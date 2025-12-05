/**
 * @file usb_mass_storage.h
 * @brief USB Mass Storage 驱动
 * 
 * 实现 USB Mass Storage Bulk-Only Transport (BBB) 协议
 * 支持 SCSI 透明命令集，用于 U 盘等存储设备
 */

#ifndef _DRIVERS_USB_USB_MASS_STORAGE_H_
#define _DRIVERS_USB_USB_MASS_STORAGE_H_

#include <types.h>
#include <drivers/usb/usb.h>
#include <fs/blockdev.h>

/* ============================================================================
 * USB Mass Storage 常量
 * ============================================================================ */

/* BBB (Bulk-Only) 协议常量 */
#define USB_MSC_BBB_RESET           0xFF    // Mass Storage Reset
#define USB_MSC_BBB_GET_MAX_LUN     0xFE    // Get Max LUN

/* CBW (Command Block Wrapper) */
#define USB_MSC_CBW_SIGNATURE       0x43425355  // "USBC"
#define USB_MSC_CBW_LENGTH          31

/* CSW (Command Status Wrapper) */
#define USB_MSC_CSW_SIGNATURE       0x53425355  // "USBS"
#define USB_MSC_CSW_LENGTH          13

/* CSW 状态 */
#define USB_MSC_CSW_STATUS_PASS     0x00
#define USB_MSC_CSW_STATUS_FAIL     0x01
#define USB_MSC_CSW_STATUS_PHASE    0x02

/* CBW 方向 */
#define USB_MSC_CBW_DIR_OUT         0x00
#define USB_MSC_CBW_DIR_IN          0x80

/* SCSI 命令 */
#define SCSI_CMD_TEST_UNIT_READY    0x00
#define SCSI_CMD_REQUEST_SENSE      0x03
#define SCSI_CMD_INQUIRY            0x12
#define SCSI_CMD_READ_CAPACITY      0x25
#define SCSI_CMD_READ_10            0x28
#define SCSI_CMD_WRITE_10           0x2A
#define SCSI_CMD_MODE_SENSE_6       0x1A

/* SCSI 状态 */
#define SCSI_STATUS_GOOD            0x00
#define SCSI_STATUS_CHECK_CONDITION 0x02
#define SCSI_STATUS_BUSY            0x08

/* 块大小 */
#define USB_MSC_BLOCK_SIZE          512

/* 最大支持的 MSC 设备数 */
#define USB_MSC_MAX_DEVICES         8

/* ============================================================================
 * USB Mass Storage 数据结构
 * ============================================================================ */

/** Command Block Wrapper (CBW) */
typedef struct usb_msc_cbw {
    uint32_t dCBWSignature;         // CBW 签名
    uint32_t dCBWTag;               // 命令标签
    uint32_t dCBWDataTransferLength;// 数据传输长度
    uint8_t  bmCBWFlags;            // 方向标志
    uint8_t  bCBWLUN;               // LUN
    uint8_t  bCBWCBLength;          // 命令块长度 (1-16)
    uint8_t  CBWCB[16];             // 命令块
} __attribute__((packed)) usb_msc_cbw_t;

/** Command Status Wrapper (CSW) */
typedef struct usb_msc_csw {
    uint32_t dCSWSignature;         // CSW 签名
    uint32_t dCSWTag;               // 命令标签（与 CBW 匹配）
    uint32_t dCSWDataResidue;       // 数据残余
    uint8_t  bCSWStatus;            // 状态
} __attribute__((packed)) usb_msc_csw_t;

/** SCSI Inquiry 响应 */
typedef struct scsi_inquiry_response {
    uint8_t peripheral;             // 外设类型
    uint8_t removable;              // 可移除标志
    uint8_t version;                // SCSI 版本
    uint8_t response_format;        // 响应格式
    uint8_t additional_length;      // 额外数据长度
    uint8_t reserved[3];
    char vendor[8];                 // 厂商 ID
    char product[16];               // 产品 ID
    char revision[4];               // 修订版本
} __attribute__((packed)) scsi_inquiry_response_t;

/** SCSI Read Capacity (10) 响应 */
typedef struct scsi_read_capacity_response {
    uint32_t last_lba;              // 最后一个 LBA（大端序）
    uint32_t block_size;            // 块大小（大端序）
} __attribute__((packed)) scsi_read_capacity_response_t;

/** SCSI Request Sense 响应 */
typedef struct scsi_request_sense_response {
    uint8_t error_code;
    uint8_t segment_number;
    uint8_t sense_key;
    uint8_t information[4];
    uint8_t additional_length;
    uint8_t reserved[4];
    uint8_t asc;                    // Additional Sense Code
    uint8_t ascq;                   // Additional Sense Code Qualifier
    uint8_t reserved2[4];
} __attribute__((packed)) scsi_request_sense_response_t;

/** USB Mass Storage 设备 */
typedef struct usb_msc_device {
    usb_device_t *usb_dev;          // USB 设备
    usb_interface_t *iface;         // 接口
    
    /* 端点 */
    usb_endpoint_t *ep_in;          // Bulk IN 端点
    usb_endpoint_t *ep_out;         // Bulk OUT 端点
    
    /* 设备信息 */
    uint8_t max_lun;                // 最大 LUN
    uint32_t block_size;            // 块大小
    uint32_t block_count;           // 块数量
    bool ready;                     // 设备就绪
    
    /* CBW 标签 */
    uint32_t tag;                   // 命令标签计数
    
    /* 块设备 */
    blockdev_t blockdev;            // 块设备接口
    
    /* Inquiry 数据 */
    char vendor[9];                 // 厂商 ID
    char product[17];               // 产品 ID
    char revision[5];               // 修订版本
    
    /* 链表 */
    struct usb_msc_device *next;
} usb_msc_device_t;

/* ============================================================================
 * 函数声明
 * ============================================================================ */

/**
 * @brief 初始化 USB Mass Storage 驱动
 * @return 0 成功，-1 失败
 */
int usb_msc_init(void);

/**
 * @brief 探测 USB Mass Storage 设备
 * @param dev USB 设备
 * @param iface USB 接口
 * @return 0 成功，-1 失败
 */
int usb_msc_probe(usb_device_t *dev, usb_interface_t *iface);

/**
 * @brief 断开 USB Mass Storage 设备
 * @param dev USB 设备
 * @param iface USB 接口
 */
void usb_msc_disconnect(usb_device_t *dev, usb_interface_t *iface);

/**
 * @brief 读取扇区
 * @param msc MSC 设备
 * @param lba 逻辑块地址
 * @param count 块数量
 * @param buffer 缓冲区
 * @return 0 成功，-1 失败
 */
int usb_msc_read(usb_msc_device_t *msc, uint32_t lba, uint32_t count, uint8_t *buffer);

/**
 * @brief 写入扇区
 * @param msc MSC 设备
 * @param lba 逻辑块地址
 * @param count 块数量
 * @param buffer 缓冲区
 * @return 0 成功，-1 失败
 */
int usb_msc_write(usb_msc_device_t *msc, uint32_t lba, uint32_t count, const uint8_t *buffer);

/**
 * @brief 获取设备容量
 * @param msc MSC 设备
 * @param block_count 块数量（输出）
 * @param block_size 块大小（输出）
 * @return 0 成功，-1 失败
 */
int usb_msc_get_capacity(usb_msc_device_t *msc, uint32_t *block_count, uint32_t *block_size);

/**
 * @brief 获取 MSC 设备列表
 * @return 设备链表头
 */
usb_msc_device_t *usb_msc_get_devices(void);

/**
 * @brief 按名称获取 MSC 块设备
 * @param name 设备名称（如 "usb0"）
 * @return 块设备指针，未找到返回 NULL
 */
blockdev_t *usb_msc_get_blockdev(const char *name);

/**
 * @brief 打印 MSC 设备信息
 * @param msc MSC 设备
 */
void usb_msc_print_info(usb_msc_device_t *msc);

#endif // _DRIVERS_USB_USB_MASS_STORAGE_H_

