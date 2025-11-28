/**
 * @file pci.c
 * @brief PCI 总线驱动实现
 * 
 * 实现 PCI 配置空间访问、设备枚举和资源管理
 */

#include <drivers/pci.h>
#include <kernel/io.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>

/* ============================================================================
 * 全局变量
 * ============================================================================ */

/** 已发现的 PCI 设备列表 */
static pci_device_t pci_devices[PCI_MAX_DEVICES];
static int pci_device_count = 0;

/* ============================================================================
 * 配置空间访问
 * ============================================================================ */

/**
 * @brief 生成 PCI 配置地址
 */
static inline uint32_t pci_config_address(uint8_t bus, uint8_t slot, 
                                          uint8_t func, uint8_t offset) {
    return (uint32_t)((1 << 31) |           // 启用位
                      ((uint32_t)bus << 16) |
                      ((uint32_t)slot << 11) |
                      ((uint32_t)func << 8) |
                      (offset & 0xFC));      // 4 字节对齐
}

uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = pci_config_address(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    return inb(PCI_CONFIG_DATA + (offset & 3));
}

uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = pci_config_address(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    return inw(PCI_CONFIG_DATA + (offset & 2));
}

uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = pci_config_address(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config8(uint8_t bus, uint8_t slot, uint8_t func, 
                       uint8_t offset, uint8_t value) {
    uint32_t address = pci_config_address(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    outb(PCI_CONFIG_DATA + (offset & 3), value);
}

void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, 
                        uint8_t offset, uint16_t value) {
    uint32_t address = pci_config_address(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    outw(PCI_CONFIG_DATA + (offset & 2), value);
}

void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, 
                        uint8_t offset, uint32_t value) {
    uint32_t address = pci_config_address(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    outl(PCI_CONFIG_DATA, value);
}

/* ============================================================================
 * BAR 操作
 * ============================================================================ */

/**
 * @brief 探测 BAR 大小
 */
static uint32_t pci_probe_bar_size(uint8_t bus, uint8_t slot, uint8_t func, 
                                   uint8_t bar_reg) {
    // 保存原始值
    uint32_t original = pci_read_config32(bus, slot, func, bar_reg);
    
    // 写入全 1
    pci_write_config32(bus, slot, func, bar_reg, 0xFFFFFFFF);
    
    // 读回值
    uint32_t size_mask = pci_read_config32(bus, slot, func, bar_reg);
    
    // 恢复原始值
    pci_write_config32(bus, slot, func, bar_reg, original);
    
    if (size_mask == 0 || size_mask == 0xFFFFFFFF) {
        return 0;
    }
    
    // 计算大小：清除低位标志，取反加 1
    if (original & PCI_BAR_TYPE_IO) {
        // I/O BAR：低 2 位是类型标志
        size_mask &= ~0x3;
    } else {
        // 内存 BAR：低 4 位是类型标志
        size_mask &= ~0xF;
    }
    
    return (~size_mask) + 1;
}

uint32_t pci_get_bar_address(pci_device_t *dev, int bar_index) {
    if (!dev || bar_index < 0 || bar_index >= 6) {
        return 0;
    }
    
    uint32_t bar = dev->bar[bar_index];
    
    if (bar & PCI_BAR_TYPE_IO) {
        // I/O BAR：低 2 位是标志
        return bar & ~0x3;
    } else {
        // 内存 BAR：低 4 位是标志
        return bar & ~0xF;
    }
}

uint32_t pci_get_bar_size(pci_device_t *dev, int bar_index) {
    if (!dev || bar_index < 0 || bar_index >= 6) {
        return 0;
    }
    return dev->bar_size[bar_index];
}

bool pci_bar_is_io(pci_device_t *dev, int bar_index) {
    if (!dev || bar_index < 0 || bar_index >= 6) {
        return false;
    }
    return (dev->bar[bar_index] & PCI_BAR_TYPE_IO) != 0;
}

/* ============================================================================
 * 设备使能
 * ============================================================================ */

void pci_enable_bus_master(pci_device_t *dev) {
    if (!dev) return;
    
    uint16_t cmd = pci_read_config16(dev->bus, dev->slot, dev->func, PCI_COMMAND);
    cmd |= PCI_CMD_BUS_MASTER;
    pci_write_config16(dev->bus, dev->slot, dev->func, PCI_COMMAND, cmd);
}

void pci_enable_memory_space(pci_device_t *dev) {
    if (!dev) return;
    
    uint16_t cmd = pci_read_config16(dev->bus, dev->slot, dev->func, PCI_COMMAND);
    cmd |= PCI_CMD_MEMORY_SPACE;
    pci_write_config16(dev->bus, dev->slot, dev->func, PCI_COMMAND, cmd);
}

void pci_enable_io_space(pci_device_t *dev) {
    if (!dev) return;
    
    uint16_t cmd = pci_read_config16(dev->bus, dev->slot, dev->func, PCI_COMMAND);
    cmd |= PCI_CMD_IO_SPACE;
    pci_write_config16(dev->bus, dev->slot, dev->func, PCI_COMMAND, cmd);
}

/* ============================================================================
 * 设备枚举
 * ============================================================================ */

/**
 * @brief 检查并添加设备
 */
static void pci_check_device(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t vendor_id = pci_read_config16(bus, slot, func, PCI_VENDOR_ID);
    
    // 0xFFFF 表示设备不存在
    if (vendor_id == 0xFFFF) {
        return;
    }
    
    if (pci_device_count >= PCI_MAX_DEVICES) {
        LOG_WARN_MSG("pci: Maximum device count reached\n");
        return;
    }
    
    pci_device_t *dev = &pci_devices[pci_device_count];
    memset(dev, 0, sizeof(pci_device_t));
    
    // 基本信息
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = vendor_id;
    dev->device_id = pci_read_config16(bus, slot, func, PCI_DEVICE_ID);
    
    // 类别信息
    dev->class_code = pci_read_config8(bus, slot, func, PCI_CLASS);
    dev->subclass = pci_read_config8(bus, slot, func, PCI_SUBCLASS);
    dev->prog_if = pci_read_config8(bus, slot, func, PCI_PROG_IF);
    dev->revision = pci_read_config8(bus, slot, func, PCI_REVISION_ID);
    
    // 头类型和中断信息
    dev->header_type = pci_read_config8(bus, slot, func, PCI_HEADER_TYPE);
    dev->interrupt_line = pci_read_config8(bus, slot, func, PCI_INTERRUPT_LINE);
    dev->interrupt_pin = pci_read_config8(bus, slot, func, PCI_INTERRUPT_PIN);
    
    // 读取 BAR
    for (int i = 0; i < 6; i++) {
        uint8_t bar_reg = PCI_BAR0 + i * 4;
        dev->bar[i] = pci_read_config32(bus, slot, func, bar_reg);
        
        if (dev->bar[i] != 0) {
            dev->bar_size[i] = pci_probe_bar_size(bus, slot, func, bar_reg);
            dev->bar_type[i] = (dev->bar[i] & PCI_BAR_TYPE_IO) ? 1 : 0;
            
            // 如果是 64 位 BAR，跳过下一个 BAR
            if (!(dev->bar[i] & PCI_BAR_TYPE_IO) && 
                (dev->bar[i] & PCI_BAR_MEM_TYPE_MASK) == PCI_BAR_MEM_TYPE_64) {
                i++;  // 跳过高 32 位部分
            }
        }
    }
    
    pci_device_count++;
    
    LOG_DEBUG_MSG("pci: Found device %02x:%02x.%x - %04x:%04x class %02x:%02x\n",
                  bus, slot, func, vendor_id, dev->device_id, 
                  dev->class_code, dev->subclass);
}

/**
 * @brief 扫描设备的所有功能
 */
static void pci_scan_slot(uint8_t bus, uint8_t slot) {
    uint16_t vendor_id = pci_read_config16(bus, slot, 0, PCI_VENDOR_ID);
    if (vendor_id == 0xFFFF) {
        return;
    }
    
    // 检查功能 0
    pci_check_device(bus, slot, 0);
    
    // 检查是否是多功能设备
    uint8_t header_type = pci_read_config8(bus, slot, 0, PCI_HEADER_TYPE);
    if (header_type & 0x80) {  // 多功能设备标志
        // 扫描其他功能
        for (uint8_t func = 1; func < PCI_MAX_FUNC; func++) {
            pci_check_device(bus, slot, func);
        }
    }
}

/* 前向声明：扫描总线（用于递归扫描） */
static void pci_scan_bus(uint8_t bus);

/**
 * @brief 检查并扫描 PCI-to-PCI Bridge 的次级总线
 */
static void pci_check_bridge(uint8_t bus, uint8_t slot, uint8_t func) {
    uint8_t class_code = pci_read_config8(bus, slot, func, PCI_CLASS);
    uint8_t subclass = pci_read_config8(bus, slot, func, PCI_SUBCLASS);
    uint8_t header_type = pci_read_config8(bus, slot, func, PCI_HEADER_TYPE);
    
    /* 检查是否是 PCI-to-PCI Bridge（Header Type 1）*/
    if (class_code == PCI_CLASS_BRIDGE && 
        subclass == PCI_SUBCLASS_PCI_BRIDGE &&
        (header_type & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_BRIDGE) {
        
        /* 读取次级总线号 */
        uint8_t secondary_bus = pci_read_config8(bus, slot, func, PCI_SECONDARY_BUS);
        
        LOG_DEBUG_MSG("pci: Found PCI-to-PCI Bridge at %02x:%02x.%x, secondary bus: %d\n",
                      bus, slot, func, secondary_bus);
        
        /* 递归扫描次级总线 */
        if (secondary_bus != 0 && secondary_bus != bus) {
            pci_scan_bus(secondary_bus);
        }
    }
}

/**
 * @brief 扫描一条总线
 */
static void pci_scan_bus(uint8_t bus) {
    for (uint8_t slot = 0; slot < PCI_MAX_SLOT; slot++) {
        pci_scan_slot(bus, slot);
    }
    
    /* 扫描完成后，检查是否有 PCI-to-PCI Bridge，并扫描其次级总线 */
    for (int i = 0; i < pci_device_count; i++) {
        pci_device_t *dev = &pci_devices[i];
        if (dev->bus == bus) {
            pci_check_bridge(dev->bus, dev->slot, dev->func);
        }
    }
}

int pci_scan_devices(void) {
    pci_device_count = 0;
    
    // 检查总线 0 设备 0 是否是多功能设备（可能有多个主机桥）
    uint8_t header_type = pci_read_config8(0, 0, 0, PCI_HEADER_TYPE);
    
    if ((header_type & 0x80) == 0) {
        // 单功能设备：只有一条总线
        pci_scan_bus(0);
    } else {
        // 多功能设备：可能有多条总线
        for (uint8_t func = 0; func < PCI_MAX_FUNC; func++) {
            if (pci_read_config16(0, 0, func, PCI_VENDOR_ID) != 0xFFFF) {
                pci_scan_bus(func);
            }
        }
    }
    
    LOG_INFO_MSG("pci: Found %d device(s)\n", pci_device_count);
    return pci_device_count;
}

/* ============================================================================
 * 设备查找
 * ============================================================================ */

pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id &&
            pci_devices[i].device_id == device_id) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].class_code == class_code &&
            (subclass == 0xFF || pci_devices[i].subclass == subclass)) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

int pci_get_device_count(void) {
    return pci_device_count;
}

pci_device_t *pci_get_device(int index) {
    if (index < 0 || index >= pci_device_count) {
        return NULL;
    }
    return &pci_devices[index];
}

/* ============================================================================
 * 初始化和调试
 * ============================================================================ */

void pci_init(void) {
    pci_device_count = 0;
    LOG_INFO_MSG("pci: PCI bus driver initialized\n");
}

/**
 * @brief 获取类别名称（调试用）
 */
static const char *pci_class_name(uint8_t class_code) {
    switch (class_code) {
        case 0x00: return "Unclassified";
        case 0x01: return "Storage";
        case 0x02: return "Network";
        case 0x03: return "Display";
        case 0x04: return "Multimedia";
        case 0x05: return "Memory";
        case 0x06: return "Bridge";
        case 0x07: return "Communication";
        case 0x08: return "System";
        case 0x09: return "Input";
        case 0x0A: return "Docking";
        case 0x0B: return "Processor";
        case 0x0C: return "Serial Bus";
        case 0x0D: return "Wireless";
        case 0x0E: return "I/O Controller";
        case 0x0F: return "Satellite";
        case 0x10: return "Encryption";
        case 0x11: return "Signal Processing";
        default:   return "Unknown";
    }
}

void pci_print_device(pci_device_t *dev) {
    if (!dev) return;
    
    kprintf("PCI %02x:%02x.%x:\n", dev->bus, dev->slot, dev->func);
    kprintf("  Vendor: 0x%04x  Device: 0x%04x\n", dev->vendor_id, dev->device_id);
    kprintf("  Class: %02x:%02x:%02x (%s)\n", 
            dev->class_code, dev->subclass, dev->prog_if,
            pci_class_name(dev->class_code));
    kprintf("  IRQ: %d (Pin %c)\n", 
            dev->interrupt_line,
            dev->interrupt_pin ? 'A' + dev->interrupt_pin - 1 : '-');
    
    for (int i = 0; i < 6; i++) {
        if (dev->bar[i] != 0) {
            uint32_t addr = pci_get_bar_address(dev, i);
            kprintf("  BAR%d: 0x%08x (%s, %u KB)\n",
                    i, addr,
                    pci_bar_is_io(dev, i) ? "I/O" : "MEM",
                    dev->bar_size[i] / 1024);
        }
    }
}

void pci_print_all_devices(void) {
    kprintf("\n===== PCI Devices (%d) =====\n", pci_device_count);
    for (int i = 0; i < pci_device_count; i++) {
        pci_print_device(&pci_devices[i]);
        kprintf("\n");
    }
}

