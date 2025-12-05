/**
 * @file usb_mass_storage.c
 * @brief USB Mass Storage 驱动实现
 * 
 * 实现 USB Mass Storage Bulk-Only Transport (BBB) 协议
 * 支持 SCSI 透明命令集，用于 U 盘等存储设备
 */

#include <drivers/usb/usb_mass_storage.h>
#include <drivers/usb/usb.h>
#include <drivers/timer.h>
#include <mm/heap.h>
#include <mm/vmm.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>

/* ============================================================================
 * 全局变量
 * ============================================================================ */

/** MSC 设备链表 */
static usb_msc_device_t *msc_devices = NULL;

/** MSC 设备计数 */
static int msc_device_count = 0;

/** USB MSC 驱动 */
static usb_driver_t usb_msc_driver;

/* 传输超时 */
#define MSC_COMMAND_TIMEOUT_MS  5000
#define MSC_DATA_TIMEOUT_MS     30000

/* ============================================================================
 * 大小端转换
 * ============================================================================ */

static inline uint32_t be32_to_cpu(uint32_t val) {
    return ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) |
           ((val & 0xFF0000) >> 8) | ((val & 0xFF000000) >> 24);
}

static inline uint32_t cpu_to_be32(uint32_t val) {
    return be32_to_cpu(val);
}

static inline uint16_t be16_to_cpu(uint16_t val) {
    return ((val & 0xFF) << 8) | ((val & 0xFF00) >> 8);
}

static inline uint16_t cpu_to_be16(uint16_t val) {
    return be16_to_cpu(val);
}

/* ============================================================================
 * BBB 协议实现
 * ============================================================================ */

/**
 * @brief 发送 CBW（Command Block Wrapper）
 */
static int msc_send_cbw(usb_msc_device_t *msc, usb_msc_cbw_t *cbw) {
    uint32_t actual;
    int ret = usb_bulk_transfer(msc->usb_dev, msc->ep_out->address,
                                cbw, sizeof(usb_msc_cbw_t), &actual,
                                MSC_COMMAND_TIMEOUT_MS);
    
    if (ret < 0) {
        LOG_ERROR_MSG("msc: Failed to send CBW, ret=%d\n", ret);
        return ret;
    }
    
    if (actual != sizeof(usb_msc_cbw_t)) {
        LOG_ERROR_MSG("msc: CBW incomplete, sent %u of %u\n", actual, (uint32_t)sizeof(usb_msc_cbw_t));
        return -1;
    }
    
    return 0;
}

/**
 * @brief 接收 CSW（Command Status Wrapper）
 */
static int msc_recv_csw(usb_msc_device_t *msc, usb_msc_csw_t *csw, uint32_t expected_tag) {
    uint32_t actual;
    int ret = usb_bulk_transfer(msc->usb_dev, msc->ep_in->address,
                                csw, sizeof(usb_msc_csw_t), &actual,
                                MSC_COMMAND_TIMEOUT_MS);
    
    if (ret < 0) {
        LOG_ERROR_MSG("msc: Failed to receive CSW, ret=%d\n", ret);
        
        /* 尝试清除 HALT 并重试 */
        usb_clear_halt(msc->usb_dev, msc->ep_in->address);
        ret = usb_bulk_transfer(msc->usb_dev, msc->ep_in->address,
                                csw, sizeof(usb_msc_csw_t), &actual,
                                MSC_COMMAND_TIMEOUT_MS);
        if (ret < 0) {
            return ret;
        }
    }
    
    if (actual != sizeof(usb_msc_csw_t)) {
        LOG_ERROR_MSG("msc: CSW incomplete, received %u of %u\n", actual, (uint32_t)sizeof(usb_msc_csw_t));
        return -1;
    }
    
    /* 验证 CSW */
    if (csw->dCSWSignature != USB_MSC_CSW_SIGNATURE) {
        LOG_ERROR_MSG("msc: Invalid CSW signature 0x%08x\n", csw->dCSWSignature);
        return -1;
    }
    
    if (csw->dCSWTag != expected_tag) {
        LOG_ERROR_MSG("msc: CSW tag mismatch: expected %u, got %u\n", expected_tag, csw->dCSWTag);
        return -1;
    }
    
    return 0;
}

/**
 * @brief 执行 SCSI 命令
 */
static int msc_scsi_command(usb_msc_device_t *msc, uint8_t *cmd, uint8_t cmd_len,
                            uint8_t direction, void *data, uint32_t data_len,
                            uint32_t *actual_len) {
    usb_msc_cbw_t cbw;
    usb_msc_csw_t csw;
    
    /* 构建 CBW */
    memset(&cbw, 0, sizeof(cbw));
    cbw.dCBWSignature = USB_MSC_CBW_SIGNATURE;
    cbw.dCBWTag = ++msc->tag;
    cbw.dCBWDataTransferLength = data_len;
    cbw.bmCBWFlags = direction;
    cbw.bCBWLUN = 0;
    cbw.bCBWCBLength = cmd_len;
    memcpy(cbw.CBWCB, cmd, cmd_len);
    
    /* 发送 CBW */
    int ret = msc_send_cbw(msc, &cbw);
    if (ret < 0) {
        return ret;
    }
    
    /* 数据阶段 */
    uint32_t transferred = 0;
    if (data_len > 0 && data) {
        uint8_t ep_addr = (direction == USB_MSC_CBW_DIR_IN) ? 
                           msc->ep_in->address : msc->ep_out->address;
        
        ret = usb_bulk_transfer(msc->usb_dev, ep_addr, data, data_len,
                                &transferred, MSC_DATA_TIMEOUT_MS);
        
        if (ret < 0 && ret != URB_STATUS_STALL) {
            LOG_ERROR_MSG("msc: Data transfer failed, ret=%d\n", ret);
            /* 尝试恢复 */
            usb_clear_halt(msc->usb_dev, ep_addr);
        }
    }
    
    if (actual_len) {
        *actual_len = transferred;
    }
    
    /* 接收 CSW */
    ret = msc_recv_csw(msc, &csw, cbw.dCBWTag);
    if (ret < 0) {
        return ret;
    }
    
    /* 检查状态 */
    if (csw.bCSWStatus == USB_MSC_CSW_STATUS_PASS) {
        return 0;
    } else if (csw.bCSWStatus == USB_MSC_CSW_STATUS_FAIL) {
        LOG_DEBUG_MSG("msc: Command failed, residue=%u\n", csw.dCSWDataResidue);
        return -1;
    } else {
        LOG_ERROR_MSG("msc: Phase error\n");
        return -1;
    }
}

/* ============================================================================
 * SCSI 命令实现
 * ============================================================================ */

/**
 * @brief SCSI Test Unit Ready
 */
static int msc_test_unit_ready(usb_msc_device_t *msc) {
    uint8_t cmd[6] = { SCSI_CMD_TEST_UNIT_READY, 0, 0, 0, 0, 0 };
    return msc_scsi_command(msc, cmd, 6, USB_MSC_CBW_DIR_IN, NULL, 0, NULL);
}

/**
 * @brief SCSI Request Sense
 */
static int msc_request_sense(usb_msc_device_t *msc, scsi_request_sense_response_t *sense) {
    uint8_t cmd[6] = { SCSI_CMD_REQUEST_SENSE, 0, 0, 0, 18, 0 };
    return msc_scsi_command(msc, cmd, 6, USB_MSC_CBW_DIR_IN, sense, 18, NULL);
}

/**
 * @brief SCSI Inquiry
 */
static int msc_inquiry(usb_msc_device_t *msc, scsi_inquiry_response_t *inquiry) {
    uint8_t cmd[6] = { SCSI_CMD_INQUIRY, 0, 0, 0, 36, 0 };
    return msc_scsi_command(msc, cmd, 6, USB_MSC_CBW_DIR_IN, inquiry, 36, NULL);
}

/**
 * @brief SCSI Read Capacity (10)
 */
static int msc_read_capacity(usb_msc_device_t *msc, uint32_t *block_count, uint32_t *block_size) {
    scsi_read_capacity_response_t resp;
    uint8_t cmd[10] = { SCSI_CMD_READ_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    
    int ret = msc_scsi_command(msc, cmd, 10, USB_MSC_CBW_DIR_IN, &resp, sizeof(resp), NULL);
    if (ret < 0) {
        return ret;
    }
    
    *block_count = be32_to_cpu(resp.last_lba) + 1;
    *block_size = be32_to_cpu(resp.block_size);
    
    return 0;
}

/**
 * @brief SCSI Read (10)
 */
static int msc_read_10(usb_msc_device_t *msc, uint32_t lba, uint16_t count, uint8_t *buffer) {
    uint8_t cmd[10];
    cmd[0] = SCSI_CMD_READ_10;
    cmd[1] = 0;
    cmd[2] = (lba >> 24) & 0xFF;
    cmd[3] = (lba >> 16) & 0xFF;
    cmd[4] = (lba >> 8) & 0xFF;
    cmd[5] = lba & 0xFF;
    cmd[6] = 0;
    cmd[7] = (count >> 8) & 0xFF;
    cmd[8] = count & 0xFF;
    cmd[9] = 0;
    
    uint32_t data_len = (uint32_t)count * msc->block_size;
    return msc_scsi_command(msc, cmd, 10, USB_MSC_CBW_DIR_IN, buffer, data_len, NULL);
}

/**
 * @brief SCSI Write (10)
 */
static int msc_write_10(usb_msc_device_t *msc, uint32_t lba, uint16_t count, const uint8_t *buffer) {
    uint8_t cmd[10];
    cmd[0] = SCSI_CMD_WRITE_10;
    cmd[1] = 0;
    cmd[2] = (lba >> 24) & 0xFF;
    cmd[3] = (lba >> 16) & 0xFF;
    cmd[4] = (lba >> 8) & 0xFF;
    cmd[5] = lba & 0xFF;
    cmd[6] = 0;
    cmd[7] = (count >> 8) & 0xFF;
    cmd[8] = count & 0xFF;
    cmd[9] = 0;
    
    uint32_t data_len = (uint32_t)count * msc->block_size;
    return msc_scsi_command(msc, cmd, 10, USB_MSC_CBW_DIR_OUT, (void *)buffer, data_len, NULL);
}

/* ============================================================================
 * 块设备接口
 * ============================================================================ */

/**
 * @brief 块设备读取回调
 */
static int msc_blockdev_read(void *dev, uint32_t sector, uint32_t count, uint8_t *buffer) {
    usb_msc_device_t *msc = (usb_msc_device_t *)dev;
    if (!msc || !msc->ready || !buffer) {
        return -1;
    }
    
    /* 分块读取（每次最多 128 扇区，64KB） */
    uint32_t max_sectors = 128;
    uint32_t offset = 0;
    
    while (count > 0) {
        uint16_t chunk = (count > max_sectors) ? max_sectors : count;
        
        int ret = msc_read_10(msc, sector + offset, chunk, buffer + offset * msc->block_size);
        if (ret < 0) {
            LOG_ERROR_MSG("msc: Read failed at sector %u\n", sector + offset);
            return ret;
        }
        
        offset += chunk;
        count -= chunk;
    }
    
    return 0;
}

/**
 * @brief 块设备写入回调
 */
static int msc_blockdev_write(void *dev, uint32_t sector, uint32_t count, const uint8_t *buffer) {
    usb_msc_device_t *msc = (usb_msc_device_t *)dev;
    if (!msc || !msc->ready || !buffer) {
        return -1;
    }
    
    /* 分块写入 */
    uint32_t max_sectors = 128;
    uint32_t offset = 0;
    
    while (count > 0) {
        uint16_t chunk = (count > max_sectors) ? max_sectors : count;
        
        int ret = msc_write_10(msc, sector + offset, chunk, buffer + offset * msc->block_size);
        if (ret < 0) {
            LOG_ERROR_MSG("msc: Write failed at sector %u\n", sector + offset);
            return ret;
        }
        
        offset += chunk;
        count -= chunk;
    }
    
    return 0;
}

/**
 * @brief 获取块设备大小
 */
static uint32_t msc_blockdev_get_size(void *dev) {
    usb_msc_device_t *msc = (usb_msc_device_t *)dev;
    return msc ? msc->block_count : 0;
}

/**
 * @brief 获取块设备块大小
 */
static uint32_t msc_blockdev_get_block_size(void *dev) {
    usb_msc_device_t *msc = (usb_msc_device_t *)dev;
    return msc ? msc->block_size : 0;
}

/* ============================================================================
 * 设备探测和断开
 * ============================================================================ */

int usb_msc_probe(usb_device_t *dev, usb_interface_t *iface) {
    if (!dev || !iface) {
        return -1;
    }
    
    /* 检查是否是 Mass Storage Bulk-Only 设备 */
    if (iface->class_code != USB_CLASS_MASS_STORAGE) {
        return -1;
    }
    if (iface->subclass_code != USB_MSC_SUBCLASS_SCSI && 
        iface->subclass_code != USB_MSC_SUBCLASS_RBC) {
        LOG_DEBUG_MSG("msc: Unsupported subclass 0x%02x\n", iface->subclass_code);
        return -1;
    }
    if (iface->protocol != USB_MSC_PROTO_BBB) {
        LOG_DEBUG_MSG("msc: Unsupported protocol 0x%02x\n", iface->protocol);
        return -1;
    }
    
    LOG_INFO_MSG("msc: Found USB Mass Storage device\n");
    
    /* 分配 MSC 设备结构 */
    usb_msc_device_t *msc = (usb_msc_device_t *)kmalloc(sizeof(usb_msc_device_t));
    if (!msc) {
        return -1;
    }
    memset(msc, 0, sizeof(usb_msc_device_t));
    
    msc->usb_dev = dev;
    msc->iface = iface;
    
    /* 查找 Bulk IN 和 OUT 端点 */
    for (uint8_t i = 0; i < iface->num_endpoints; i++) {
        usb_endpoint_t *ep = &iface->endpoints[i];
        if (ep->type != USB_TRANSFER_BULK) {
            continue;
        }
        
        if ((ep->address & USB_DIR_MASK) == USB_DIR_IN) {
            msc->ep_in = ep;
        } else {
            msc->ep_out = ep;
        }
    }
    
    if (!msc->ep_in || !msc->ep_out) {
        LOG_ERROR_MSG("msc: Missing bulk endpoints\n");
        kfree(msc);
        return -1;
    }
    
    LOG_DEBUG_MSG("msc: EP IN=0x%02x OUT=0x%02x\n", msc->ep_in->address, msc->ep_out->address);
    
    /* 执行 SCSI Inquiry */
    scsi_inquiry_response_t inquiry;
    if (msc_inquiry(msc, &inquiry) < 0) {
        LOG_ERROR_MSG("msc: Inquiry failed\n");
        kfree(msc);
        return -1;
    }
    
    /* 保存设备信息 */
    memcpy(msc->vendor, inquiry.vendor, 8);
    msc->vendor[8] = '\0';
    memcpy(msc->product, inquiry.product, 16);
    msc->product[16] = '\0';
    memcpy(msc->revision, inquiry.revision, 4);
    msc->revision[4] = '\0';
    
    /* 去除尾部空格 */
    for (int i = 7; i >= 0 && msc->vendor[i] == ' '; i--) msc->vendor[i] = '\0';
    for (int i = 15; i >= 0 && msc->product[i] == ' '; i--) msc->product[i] = '\0';
    
    LOG_INFO_MSG("msc: Vendor='%s' Product='%s'\n", msc->vendor, msc->product);
    
    /* 等待设备准备就绪 */
    int retries = 10;
    while (retries > 0) {
        if (msc_test_unit_ready(msc) == 0) {
            break;
        }
        
        /* 获取 sense 数据 */
        scsi_request_sense_response_t sense;
        msc_request_sense(msc, &sense);
        
        timer_wait(500);
        retries--;
    }
    
    if (retries == 0) {
        LOG_WARN_MSG("msc: Device not ready\n");
        /* 继续尝试，某些设备需要更长时间 */
    }
    
    /* 获取容量 */
    if (msc_read_capacity(msc, &msc->block_count, &msc->block_size) < 0) {
        LOG_ERROR_MSG("msc: Read capacity failed\n");
        kfree(msc);
        return -1;
    }
    
    LOG_INFO_MSG("msc: Capacity: %u blocks x %u bytes = %u MB\n",
                msc->block_count, msc->block_size,
                (msc->block_count * msc->block_size) / (1024 * 1024));
    
    msc->ready = true;
    
    /* 设置块设备名称 */
    msc->blockdev.name[0] = 'u';
    msc->blockdev.name[1] = 's';
    msc->blockdev.name[2] = 'b';
    msc->blockdev.name[3] = '0' + msc_device_count;
    msc->blockdev.name[4] = '\0';
    msc->blockdev.private_data = msc;
    msc->blockdev.block_size = msc->block_size;
    msc->blockdev.total_sectors = msc->block_count;
    msc->blockdev.read = msc_blockdev_read;
    msc->blockdev.write = msc_blockdev_write;
    msc->blockdev.get_size = msc_blockdev_get_size;
    msc->blockdev.get_block_size = msc_blockdev_get_block_size;
    
    /* 注册块设备 */
    if (blockdev_register(&msc->blockdev) < 0) {
        LOG_ERROR_MSG("msc: Failed to register block device\n");
        kfree(msc);
        return -1;
    }
    
    /* 添加到链表 */
    msc->next = msc_devices;
    msc_devices = msc;
    msc_device_count++;
    
    /* 保存到接口 */
    iface->driver_data = msc;
    
    LOG_INFO_MSG("msc: Registered as '%s'\n", msc->blockdev.name);
    
    return 0;
}

void usb_msc_disconnect(usb_device_t *dev, usb_interface_t *iface) {
    if (!iface || !iface->driver_data) {
        return;
    }
    
    usb_msc_device_t *msc = (usb_msc_device_t *)iface->driver_data;
    
    /* 从链表移除 */
    usb_msc_device_t **pp = &msc_devices;
    while (*pp) {
        if (*pp == msc) {
            *pp = msc->next;
            break;
        }
        pp = &(*pp)->next;
    }
    
    /* 注销块设备 */
    blockdev_unregister(&msc->blockdev);
    
    /* 释放 */
    kfree(msc);
    iface->driver_data = NULL;
    msc_device_count--;
    
    LOG_INFO_MSG("msc: Device disconnected\n");
    (void)dev;
}

/* ============================================================================
 * 公共 API
 * ============================================================================ */

int usb_msc_read(usb_msc_device_t *msc, uint32_t lba, uint32_t count, uint8_t *buffer) {
    return msc_blockdev_read(msc, lba, count, buffer);
}

int usb_msc_write(usb_msc_device_t *msc, uint32_t lba, uint32_t count, const uint8_t *buffer) {
    return msc_blockdev_write(msc, lba, count, buffer);
}

int usb_msc_get_capacity(usb_msc_device_t *msc, uint32_t *block_count, uint32_t *block_size) {
    if (!msc || !msc->ready) {
        return -1;
    }
    
    if (block_count) *block_count = msc->block_count;
    if (block_size) *block_size = msc->block_size;
    
    return 0;
}

usb_msc_device_t *usb_msc_get_devices(void) {
    return msc_devices;
}

blockdev_t *usb_msc_get_blockdev(const char *name) {
    for (usb_msc_device_t *msc = msc_devices; msc; msc = msc->next) {
        if (strcmp(msc->blockdev.name, name) == 0) {
            return &msc->blockdev;
        }
    }
    return NULL;
}

void usb_msc_print_info(usb_msc_device_t *msc) {
    if (!msc) return;
    
    kprintf("USB Mass Storage Device:\n");
    kprintf("  Vendor: %s\n", msc->vendor);
    kprintf("  Product: %s\n", msc->product);
    kprintf("  Revision: %s\n", msc->revision);
    kprintf("  Block Size: %u bytes\n", msc->block_size);
    kprintf("  Blocks: %u\n", msc->block_count);
    kprintf("  Capacity: %u MB\n", (msc->block_count * msc->block_size) / (1024 * 1024));
    kprintf("  Block Device: %s\n", msc->blockdev.name);
    kprintf("  Ready: %s\n", msc->ready ? "Yes" : "No");
}

int usb_msc_init(void) {
    msc_devices = NULL;
    msc_device_count = 0;
    
    /* 注册 USB 驱动 */
    usb_msc_driver.name = "usb-storage";
    usb_msc_driver.id.class_code = USB_CLASS_MASS_STORAGE;
    usb_msc_driver.id.subclass_code = 0xFF;  // 匹配任意子类
    usb_msc_driver.id.protocol = USB_MSC_PROTO_BBB;
    usb_msc_driver.id.vendor_id = 0xFFFF;
    usb_msc_driver.id.product_id = 0xFFFF;
    usb_msc_driver.probe = usb_msc_probe;
    usb_msc_driver.disconnect = usb_msc_disconnect;
    
    usb_register_driver(&usb_msc_driver);
    
    LOG_INFO_MSG("msc: USB Mass Storage driver initialized\n");
    return 0;
}

