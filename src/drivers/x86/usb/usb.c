/**
 * @file usb.c
 * @brief USB 核心层实现
 * 
 * 实现 USB 设备枚举、描述符解析、URB 管理等核心功能
 */

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

/** 主机控制器链表 */
static usb_host_controller_t *usb_hc_list = NULL;

/** USB 驱动链表 */
static usb_driver_t *usb_driver_list = NULL;

/** 控制传输超时（毫秒） */
#define USB_CTRL_TIMEOUT_MS     5000

/** 批量传输超时（毫秒） */
#define USB_BULK_TIMEOUT_MS     10000

/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

/**
 * @brief 等待指定毫秒数
 */
static void usb_delay_ms(uint32_t ms) {
    timer_wait(ms);
}

/**
 * @brief 匹配驱动和设备
 */
static bool usb_driver_match(usb_driver_t *drv, usb_interface_t *iface) {
    if (drv->id.class_code != 0xFF && drv->id.class_code != iface->class_code) {
        return false;
    }
    if (drv->id.subclass_code != 0xFF && drv->id.subclass_code != iface->subclass_code) {
        return false;
    }
    if (drv->id.protocol != 0xFF && drv->id.protocol != iface->protocol) {
        return false;
    }
    return true;
}

/**
 * @brief 解析配置描述符
 */
static int usb_parse_configuration(usb_device_t *dev, uint8_t *config_data, uint16_t total_len) {
    if (!dev || !config_data || total_len < 9) {
        return -1;
    }
    
    uint8_t *ptr = config_data;
    uint8_t *end = config_data + total_len;
    usb_interface_t *current_iface = NULL;
    
    while (ptr < end) {
        uint8_t len = ptr[0];
        uint8_t type = ptr[1];
        
        if (len == 0 || ptr + len > end) {
            break;
        }
        
        switch (type) {
            case USB_DESC_CONFIGURATION: {
                usb_configuration_descriptor_t *cfg = (usb_configuration_descriptor_t *)ptr;
                dev->config_value = cfg->bConfigurationValue;
                LOG_DEBUG_MSG("usb: Configuration %d, %d interfaces\n", 
                             cfg->bConfigurationValue, cfg->bNumInterfaces);
                break;
            }
            
            case USB_DESC_INTERFACE: {
                usb_interface_descriptor_t *iface_desc = (usb_interface_descriptor_t *)ptr;
                
                if (dev->num_interfaces < USB_MAX_INTERFACES) {
                    current_iface = &dev->interfaces[dev->num_interfaces];
                    current_iface->interface_number = iface_desc->bInterfaceNumber;
                    current_iface->alternate_setting = iface_desc->bAlternateSetting;
                    current_iface->class_code = iface_desc->bInterfaceClass;
                    current_iface->subclass_code = iface_desc->bInterfaceSubClass;
                    current_iface->protocol = iface_desc->bInterfaceProtocol;
                    current_iface->num_endpoints = 0;
                    current_iface->driver_data = NULL;
                    dev->num_interfaces++;
                    
                    LOG_DEBUG_MSG("usb:   Interface %d: class=%02x subclass=%02x proto=%02x\n",
                                 iface_desc->bInterfaceNumber, iface_desc->bInterfaceClass,
                                 iface_desc->bInterfaceSubClass, iface_desc->bInterfaceProtocol);
                }
                break;
            }
            
            case USB_DESC_ENDPOINT: {
                usb_endpoint_descriptor_t *ep_desc = (usb_endpoint_descriptor_t *)ptr;
                
                if (current_iface && current_iface->num_endpoints < USB_MAX_ENDPOINTS) {
                    usb_endpoint_t *ep = &current_iface->endpoints[current_iface->num_endpoints];
                    ep->address = ep_desc->bEndpointAddress;
                    ep->type = ep_desc->bmAttributes & 0x03;
                    ep->max_packet_size = ep_desc->wMaxPacketSize;
                    ep->interval = ep_desc->bInterval;
                    ep->toggle = 0;
                    current_iface->num_endpoints++;
                    
                    LOG_DEBUG_MSG("usb:     Endpoint 0x%02x: type=%d maxpkt=%d\n",
                                 ep->address, ep->type, ep->max_packet_size);
                }
                break;
            }
            
            default:
                break;
        }
        
        ptr += len;
    }
    
    return 0;
}

/**
 * @brief 为设备加载驱动
 */
static void usb_probe_device_drivers(usb_device_t *dev) {
    for (uint8_t i = 0; i < dev->num_interfaces; i++) {
        usb_interface_t *iface = &dev->interfaces[i];
        
        for (usb_driver_t *drv = usb_driver_list; drv; drv = drv->next) {
            if (usb_driver_match(drv, iface)) {
                LOG_INFO_MSG("usb: Probing driver '%s' for interface %d\n",
                            drv->name, iface->interface_number);
                
                if (drv->probe(dev, iface) == 0) {
                    LOG_INFO_MSG("usb: Driver '%s' attached to interface %d\n",
                                drv->name, iface->interface_number);
                    break;  // 找到驱动就停止
                }
            }
        }
    }
}

/* ============================================================================
 * USB 核心 API 实现
 * ============================================================================ */

int usb_init(void) {
    usb_hc_list = NULL;
    usb_driver_list = NULL;
    
    LOG_INFO_MSG("usb: USB subsystem initialized\n");
    return 0;
}

int usb_register_hc(usb_host_controller_t *hc) {
    if (!hc) {
        return -1;
    }
    
    hc->next_address = 1;
    hc->devices = NULL;
    hc->next = usb_hc_list;
    usb_hc_list = hc;
    
    LOG_INFO_MSG("usb: Registered host controller '%s'\n", hc->name);
    return 0;
}

usb_device_t *usb_alloc_device(void) {
    usb_device_t *dev = (usb_device_t *)kmalloc(sizeof(usb_device_t));
    if (!dev) {
        return NULL;
    }
    
    memset(dev, 0, sizeof(usb_device_t));
    
    /* 初始化端点 0 */
    dev->ep0.address = 0;
    dev->ep0.type = USB_TRANSFER_CONTROL;
    dev->ep0.max_packet_size = 8;  // 初始假设为 8，枚举后更新
    dev->ep0.toggle = 0;
    
    return dev;
}

void usb_free_device(usb_device_t *dev) {
    if (!dev) {
        return;
    }
    
    if (dev->config_desc_buf) {
        kfree(dev->config_desc_buf);
    }
    
    kfree(dev);
}

usb_urb_t *usb_alloc_urb(void) {
    usb_urb_t *urb = (usb_urb_t *)kmalloc(sizeof(usb_urb_t));
    if (!urb) {
        return NULL;
    }
    
    memset(urb, 0, sizeof(usb_urb_t));
    urb->status = URB_STATUS_PENDING;
    
    return urb;
}

void usb_free_urb(usb_urb_t *urb) {
    if (urb) {
        kfree(urb);
    }
}

int usb_submit_urb(usb_urb_t *urb) {
    if (!urb || !urb->device || !urb->device->hc) {
        return -1;
    }
    
    usb_host_controller_t *hc = (usb_host_controller_t *)urb->device->hc;
    if (!hc->ops || !hc->ops->submit_urb) {
        return -1;
    }
    
    return hc->ops->submit_urb(hc->private_data, urb);
}

int usb_control_msg(usb_device_t *dev, uint8_t request_type, uint8_t request,
                    uint16_t value, uint16_t index, void *data, uint16_t length,
                    uint32_t timeout_ms) {
    if (!dev || !dev->hc) {
        return -1;
    }
    
    usb_urb_t *urb = usb_alloc_urb();
    if (!urb) {
        return -1;
    }
    
    /* 设置 URB */
    urb->device = dev;
    urb->endpoint = &dev->ep0;
    
    /* 设置 SETUP 包 */
    urb->setup.bmRequestType = request_type;
    urb->setup.bRequest = request;
    urb->setup.wValue = value;
    urb->setup.wIndex = index;
    urb->setup.wLength = length;
    
    urb->buffer = data;
    urb->buffer_length = length;
    urb->actual_length = 0;
    urb->status = URB_STATUS_PENDING;
    urb->complete = NULL;  // 同步传输
    
    /* 提交 URB */
    int ret = usb_submit_urb(urb);
    if (ret < 0) {
        usb_free_urb(urb);
        return ret;
    }
    
    /* 等待完成（轮询方式） */
    uint64_t start_time = timer_get_uptime_ms();
    while (urb->status == URB_STATUS_PENDING) {
        if (timer_get_uptime_ms() - start_time > timeout_ms) {
            urb->status = URB_STATUS_TIMEOUT;
            break;
        }
        /* 短延迟 */
        for (volatile int i = 0; i < 1000; i++);
    }
    
    int result;
    if (urb->status == URB_STATUS_COMPLETE) {
        result = (int)urb->actual_length;
    } else {
        LOG_DEBUG_MSG("usb: Control transfer failed, status=%d\n", urb->status);
        result = urb->status;
    }
    
    usb_free_urb(urb);
    return result;
}

int usb_bulk_transfer(usb_device_t *dev, uint8_t endpoint, void *data,
                      uint32_t length, uint32_t *actual_length, uint32_t timeout_ms) {
    if (!dev || !dev->hc || !data) {
        return -1;
    }
    
    /* 查找端点 */
    usb_endpoint_t *ep = NULL;
    for (uint8_t i = 0; i < dev->num_interfaces; i++) {
        usb_interface_t *iface = &dev->interfaces[i];
        for (uint8_t j = 0; j < iface->num_endpoints; j++) {
            if (iface->endpoints[j].address == endpoint) {
                ep = &iface->endpoints[j];
                break;
            }
        }
        if (ep) break;
    }
    
    if (!ep || ep->type != USB_TRANSFER_BULK) {
        LOG_ERROR_MSG("usb: Endpoint 0x%02x not found or not bulk\n", endpoint);
        return -1;
    }
    
    usb_urb_t *urb = usb_alloc_urb();
    if (!urb) {
        return -1;
    }
    
    urb->device = dev;
    urb->endpoint = ep;
    urb->buffer = data;
    urb->buffer_length = length;
    urb->actual_length = 0;
    urb->status = URB_STATUS_PENDING;
    urb->complete = NULL;
    
    int ret = usb_submit_urb(urb);
    if (ret < 0) {
        usb_free_urb(urb);
        return ret;
    }
    
    /* 等待完成 */
    uint64_t start_time = timer_get_uptime_ms();
    while (urb->status == URB_STATUS_PENDING) {
        if (timer_get_uptime_ms() - start_time > timeout_ms) {
            urb->status = URB_STATUS_TIMEOUT;
            break;
        }
        for (volatile int i = 0; i < 1000; i++);
    }
    
    if (actual_length) {
        *actual_length = urb->actual_length;
    }
    
    int result = (urb->status == URB_STATUS_COMPLETE) ? 0 : urb->status;
    usb_free_urb(urb);
    
    return result;
}

int usb_get_descriptor(usb_device_t *dev, uint8_t type, uint8_t index,
                       void *buffer, uint16_t length) {
    return usb_control_msg(dev,
                          USB_REQTYPE_DEV_TO_HOST | USB_REQTYPE_STANDARD | USB_REQTYPE_DEVICE,
                          USB_REQ_GET_DESCRIPTOR,
                          (type << 8) | index,
                          0,
                          buffer,
                          length,
                          USB_CTRL_TIMEOUT_MS);
}

int usb_set_address(usb_device_t *dev, uint8_t address) {
    int ret = usb_control_msg(dev,
                             USB_REQTYPE_HOST_TO_DEV | USB_REQTYPE_STANDARD | USB_REQTYPE_DEVICE,
                             USB_REQ_SET_ADDRESS,
                             address,
                             0,
                             NULL,
                             0,
                             USB_CTRL_TIMEOUT_MS);
    
    if (ret >= 0) {
        /* 等待设备处理地址变更 */
        usb_delay_ms(10);
        dev->address = address;
    }
    
    return (ret >= 0) ? 0 : ret;
}

int usb_set_configuration(usb_device_t *dev, uint8_t configuration) {
    int ret = usb_control_msg(dev,
                             USB_REQTYPE_HOST_TO_DEV | USB_REQTYPE_STANDARD | USB_REQTYPE_DEVICE,
                             USB_REQ_SET_CONFIGURATION,
                             configuration,
                             0,
                             NULL,
                             0,
                             USB_CTRL_TIMEOUT_MS);
    
    if (ret >= 0) {
        dev->config_value = configuration;
    }
    
    return (ret >= 0) ? 0 : ret;
}

int usb_clear_halt(usb_device_t *dev, uint8_t endpoint) {
    int ret = usb_control_msg(dev,
                             USB_REQTYPE_HOST_TO_DEV | USB_REQTYPE_STANDARD | USB_REQTYPE_ENDPOINT,
                             USB_REQ_CLEAR_FEATURE,
                             USB_FEATURE_ENDPOINT_HALT,
                             endpoint,
                             NULL,
                             0,
                             USB_CTRL_TIMEOUT_MS);
    
    /* 重置端点的数据切换位 */
    if (ret >= 0) {
        for (uint8_t i = 0; i < dev->num_interfaces; i++) {
            usb_interface_t *iface = &dev->interfaces[i];
            for (uint8_t j = 0; j < iface->num_endpoints; j++) {
                if (iface->endpoints[j].address == endpoint) {
                    iface->endpoints[j].toggle = 0;
                    break;
                }
            }
        }
    }
    
    return (ret >= 0) ? 0 : ret;
}

usb_device_t *usb_enumerate_device(usb_host_controller_t *hc, int port) {
    if (!hc || !hc->ops) {
        return NULL;
    }
    
    LOG_INFO_MSG("usb: Enumerating device on port %d\n", port);
    
    /* 检查端口连接状态 */
    if (!hc->ops->port_connected(hc->private_data, port)) {
        LOG_DEBUG_MSG("usb: No device on port %d\n", port);
        return NULL;
    }
    
    /* 复位端口 */
    if (hc->ops->reset_port(hc->private_data, port) < 0) {
        LOG_ERROR_MSG("usb: Port %d reset failed\n", port);
        return NULL;
    }
    
    /* 等待复位稳定 */
    usb_delay_ms(50);
    
    /* 启用端口 */
    if (hc->ops->enable_port && hc->ops->enable_port(hc->private_data, port) < 0) {
        LOG_ERROR_MSG("usb: Port %d enable failed\n", port);
        return NULL;
    }
    
    /* 分配设备结构 */
    usb_device_t *dev = usb_alloc_device();
    if (!dev) {
        LOG_ERROR_MSG("usb: Failed to allocate device\n");
        return NULL;
    }
    
    dev->port = port;
    dev->speed = hc->ops->port_low_speed(hc->private_data, port) ? USB_SPEED_LOW : USB_SPEED_FULL;
    dev->address = 0;  // 初始地址为 0
    dev->hc = hc;
    
    /* 设置端点 0 最大包大小 */
    dev->ep0.max_packet_size = (dev->speed == USB_SPEED_LOW) ? 8 : 64;
    
    LOG_INFO_MSG("usb: Device speed: %s\n", dev->speed == USB_SPEED_LOW ? "Low" : "Full");
    
    /* 读取设备描述符（仅前 8 字节，获取 bMaxPacketSize0） */
    usb_device_descriptor_t partial_desc;
    int ret = usb_get_descriptor(dev, USB_DESC_DEVICE, 0, &partial_desc, 8);
    if (ret < 8) {
        LOG_ERROR_MSG("usb: Failed to get device descriptor (ret=%d)\n", ret);
        usb_free_device(dev);
        return NULL;
    }
    
    /* 更新端点 0 最大包大小 */
    dev->ep0.max_packet_size = partial_desc.bMaxPacketSize0;
    LOG_DEBUG_MSG("usb: EP0 max packet size: %d\n", dev->ep0.max_packet_size);
    
    /* 再次复位端口（某些设备需要） */
    hc->ops->reset_port(hc->private_data, port);
    usb_delay_ms(50);
    
    /* 分配设备地址 */
    uint8_t new_address = hc->next_address++;
    if (new_address > 127) {
        LOG_ERROR_MSG("usb: No more device addresses available\n");
        usb_free_device(dev);
        return NULL;
    }
    
    ret = usb_set_address(dev, new_address);
    if (ret < 0) {
        LOG_ERROR_MSG("usb: Failed to set address %d\n", new_address);
        usb_free_device(dev);
        return NULL;
    }
    
    LOG_INFO_MSG("usb: Device assigned address %d\n", dev->address);
    
    /* 读取完整设备描述符 */
    ret = usb_get_descriptor(dev, USB_DESC_DEVICE, 0, &dev->device_desc, sizeof(usb_device_descriptor_t));
    if (ret < (int)sizeof(usb_device_descriptor_t)) {
        LOG_ERROR_MSG("usb: Failed to get full device descriptor\n");
        usb_free_device(dev);
        return NULL;
    }
    
    LOG_INFO_MSG("usb: Device: VID=%04x PID=%04x Class=%02x\n",
                dev->device_desc.idVendor, dev->device_desc.idProduct,
                dev->device_desc.bDeviceClass);
    
    /* 读取配置描述符 */
    if (dev->device_desc.bNumConfigurations > 0) {
        usb_configuration_descriptor_t cfg_header;
        ret = usb_get_descriptor(dev, USB_DESC_CONFIGURATION, 0, &cfg_header, sizeof(cfg_header));
        if (ret < (int)sizeof(cfg_header)) {
            LOG_ERROR_MSG("usb: Failed to get configuration descriptor header\n");
            usb_free_device(dev);
            return NULL;
        }
        
        /* 分配并读取完整配置描述符 */
        dev->config_desc_len = cfg_header.wTotalLength;
        dev->config_desc_buf = (uint8_t *)kmalloc(dev->config_desc_len);
        if (!dev->config_desc_buf) {
            LOG_ERROR_MSG("usb: Failed to allocate config descriptor buffer\n");
            usb_free_device(dev);
            return NULL;
        }
        
        ret = usb_get_descriptor(dev, USB_DESC_CONFIGURATION, 0, 
                                dev->config_desc_buf, dev->config_desc_len);
        if (ret < (int)dev->config_desc_len) {
            LOG_ERROR_MSG("usb: Failed to get full configuration descriptor\n");
            usb_free_device(dev);
            return NULL;
        }
        
        /* 解析配置描述符 */
        usb_parse_configuration(dev, dev->config_desc_buf, dev->config_desc_len);
        
        /* 设置配置 */
        ret = usb_set_configuration(dev, cfg_header.bConfigurationValue);
        if (ret < 0) {
            LOG_ERROR_MSG("usb: Failed to set configuration\n");
            usb_free_device(dev);
            return NULL;
        }
    }
    
    /* 添加到设备列表 */
    dev->next = hc->devices;
    hc->devices = dev;
    
    /* 探测并加载驱动 */
    usb_probe_device_drivers(dev);
    
    LOG_INFO_MSG("usb: Device enumeration complete\n");
    
    return dev;
}

int usb_register_driver(usb_driver_t *driver) {
    if (!driver) {
        return -1;
    }
    
    driver->next = usb_driver_list;
    usb_driver_list = driver;
    
    LOG_INFO_MSG("usb: Registered driver '%s'\n", driver->name);
    
    /* 尝试匹配现有设备 */
    for (usb_host_controller_t *hc = usb_hc_list; hc; hc = hc->next) {
        for (usb_device_t *dev = hc->devices; dev; dev = dev->next) {
            for (uint8_t i = 0; i < dev->num_interfaces; i++) {
                usb_interface_t *iface = &dev->interfaces[i];
                if (!iface->driver_data && usb_driver_match(driver, iface)) {
                    if (driver->probe(dev, iface) == 0) {
                        LOG_INFO_MSG("usb: Late-bound driver '%s' to device\n", driver->name);
                    }
                }
            }
        }
    }
    
    return 0;
}

void usb_unregister_driver(usb_driver_t *driver) {
    if (!driver) {
        return;
    }
    
    usb_driver_t **pp = &usb_driver_list;
    while (*pp) {
        if (*pp == driver) {
            *pp = driver->next;
            break;
        }
        pp = &(*pp)->next;
    }
}

usb_endpoint_t *usb_find_endpoint(usb_device_t *dev, uint8_t iface_num,
                                   uint8_t type, uint8_t dir) {
    if (!dev) {
        return NULL;
    }
    
    for (uint8_t i = 0; i < dev->num_interfaces; i++) {
        usb_interface_t *iface = &dev->interfaces[i];
        if (iface->interface_number != iface_num) {
            continue;
        }
        
        for (uint8_t j = 0; j < iface->num_endpoints; j++) {
            usb_endpoint_t *ep = &iface->endpoints[j];
            if (ep->type == type && (ep->address & USB_DIR_MASK) == dir) {
                return ep;
            }
        }
    }
    
    return NULL;
}

void usb_print_device_info(usb_device_t *dev) {
    if (!dev) {
        return;
    }
    
    kprintf("USB Device:\n");
    kprintf("  Address: %d\n", dev->address);
    kprintf("  Speed: %s\n", dev->speed == USB_SPEED_LOW ? "Low (1.5 Mbps)" : "Full (12 Mbps)");
    kprintf("  Vendor ID: 0x%04x\n", dev->device_desc.idVendor);
    kprintf("  Product ID: 0x%04x\n", dev->device_desc.idProduct);
    kprintf("  Device Class: 0x%02x\n", dev->device_desc.bDeviceClass);
    kprintf("  Subclass: 0x%02x\n", dev->device_desc.bDeviceSubClass);
    kprintf("  Protocol: 0x%02x\n", dev->device_desc.bDeviceProtocol);
    kprintf("  Interfaces: %d\n", dev->num_interfaces);
    
    for (uint8_t i = 0; i < dev->num_interfaces; i++) {
        usb_interface_t *iface = &dev->interfaces[i];
        kprintf("    Interface %d: class=0x%02x sub=0x%02x proto=0x%02x endpoints=%d\n",
               iface->interface_number, iface->class_code, iface->subclass_code,
               iface->protocol, iface->num_endpoints);
        
        for (uint8_t j = 0; j < iface->num_endpoints; j++) {
            usb_endpoint_t *ep = &iface->endpoints[j];
            const char *type_str = "Unknown";
            switch (ep->type) {
                case USB_TRANSFER_CONTROL: type_str = "Control"; break;
                case USB_TRANSFER_ISOCHRONOUS: type_str = "Isochronous"; break;
                case USB_TRANSFER_BULK: type_str = "Bulk"; break;
                case USB_TRANSFER_INTERRUPT: type_str = "Interrupt"; break;
            }
            kprintf("      EP 0x%02x: %s %s maxpkt=%d\n",
                   ep->address, type_str,
                   (ep->address & USB_DIR_IN) ? "IN" : "OUT",
                   ep->max_packet_size);
        }
    }
}

void usb_scan_devices(void) {
    LOG_INFO_MSG("usb: Scanning for devices...\n");
    
    for (usb_host_controller_t *hc = usb_hc_list; hc; hc = hc->next) {
        if (!hc->ops) {
            continue;
        }
        
        int port_count = 2;  // UHCI 默认 2 个端口
        if (hc->ops->get_port_count) {
            port_count = hc->ops->get_port_count(hc->private_data);
        }
        
        for (int port = 0; port < port_count; port++) {
            if (hc->ops->port_connected(hc->private_data, port)) {
                usb_enumerate_device(hc, port);
            }
        }
    }
}

usb_host_controller_t *usb_get_hc_list(void) {
    return usb_hc_list;
}

int usb_get_device_count(void) {
    int count = 0;
    for (usb_host_controller_t *hc = usb_hc_list; hc; hc = hc->next) {
        for (usb_device_t *dev = hc->devices; dev; dev = dev->next) {
            count++;
        }
    }
    return count;
}

usb_device_t *usb_get_device(int index) {
    int count = 0;
    for (usb_host_controller_t *hc = usb_hc_list; hc; hc = hc->next) {
        for (usb_device_t *dev = hc->devices; dev; dev = dev->next) {
            if (count == index) {
                return dev;
            }
            count++;
        }
    }
    return NULL;
}

usb_device_t *usb_find_device_by_port(usb_host_controller_t *hc, int port) {
    if (!hc) {
        return NULL;
    }
    
    for (usb_device_t *dev = hc->devices; dev; dev = dev->next) {
        if (dev->port == port) {
            return dev;
        }
    }
    return NULL;
}

void usb_disconnect_device(usb_host_controller_t *hc, usb_device_t *dev) {
    if (!hc || !dev) {
        return;
    }
    
    LOG_INFO_MSG("usb: Disconnecting device at address %d on port %d\n", 
                 dev->address, dev->port);
    
    /* 通知所有接口的驱动 */
    for (uint8_t i = 0; i < dev->num_interfaces; i++) {
        usb_interface_t *iface = &dev->interfaces[i];
        
        /* 查找并调用驱动的 disconnect 回调 */
        for (usb_driver_t *drv = usb_driver_list; drv; drv = drv->next) {
            if (usb_driver_match(drv, iface) && drv->disconnect) {
                LOG_INFO_MSG("usb: Calling disconnect for driver '%s'\n", drv->name);
                drv->disconnect(dev, iface);
            }
        }
        
        iface->driver_data = NULL;
    }
    
    /* 从设备链表中移除 */
    usb_device_t **pp = &hc->devices;
    while (*pp) {
        if (*pp == dev) {
            *pp = dev->next;
            break;
        }
        pp = &(*pp)->next;
    }
    
    /* 释放设备资源 */
    usb_free_device(dev);
    
    LOG_INFO_MSG("usb: Device disconnected and freed\n");
}

usb_device_t *usb_handle_port_connect(usb_host_controller_t *hc, int port) {
    if (!hc) {
        return NULL;
    }
    
    LOG_INFO_MSG("usb: Device connected on port %d\n", port);
    
    /* 枚举新设备 */
    usb_device_t *dev = usb_enumerate_device(hc, port);
    if (dev) {
        LOG_INFO_MSG("usb: New device enumerated: VID=%04x PID=%04x\n",
                    dev->device_desc.idVendor, dev->device_desc.idProduct);
    }
    
    return dev;
}

void usb_handle_port_disconnect(usb_host_controller_t *hc, int port) {
    if (!hc) {
        return;
    }
    
    LOG_INFO_MSG("usb: Device disconnected from port %d\n", port);
    
    /* 查找并断开该端口上的设备 */
    usb_device_t *dev = usb_find_device_by_port(hc, port);
    if (dev) {
        usb_disconnect_device(hc, dev);
    }
}

