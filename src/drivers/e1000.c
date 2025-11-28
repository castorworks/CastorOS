/**
 * @file e1000.c
 * @brief Intel E1000 千兆以太网控制器驱动
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
#include <lib/kprintf.h>
#include <lib/string.h>

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
 * @brief 读取 MMIO 寄存器
 */
static inline uint32_t e1000_read_reg(e1000_device_t *dev, uint32_t reg) {
    return dev->mmio_base[reg / 4];
}

/**
 * @brief 写入 MMIO 寄存器
 */
static inline void e1000_write_reg(e1000_device_t *dev, uint32_t reg, uint32_t value) {
    dev->mmio_base[reg / 4] = value;
}

/* ============================================================================
 * EEPROM 访问（读取 MAC 地址）
 * ============================================================================ */

/**
 * @brief 从 EEPROM 读取一个字
 */
static uint16_t e1000_eeprom_read(e1000_device_t *dev, uint8_t addr) {
    uint32_t val;
    int timeout = 10000;
    
    /* 写入读取命令 */
    e1000_write_reg(dev, E1000_REG_EERD, (uint32_t)addr << 8 | 1);
    
    /* 等待读取完成 */
    while (timeout-- > 0) {
        val = e1000_read_reg(dev, E1000_REG_EERD);
        if (val & (1 << 4)) {  // Done 位
            return (uint16_t)(val >> 16);
        }
    }
    
    LOG_WARN_MSG("e1000: EEPROM read timeout\n");
    return 0;
}

/**
 * @brief 读取 MAC 地址
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
 * @brief 初始化接收描述符环
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
    
    /* 获取物理地址 - 必须通过页表查询，因为堆内存不是恒等映射 */
    dev->rx_descs_phys = vmm_virt_to_phys((uint32_t)dev->rx_descs);
    if (!dev->rx_descs_phys) {
        LOG_ERROR_MSG("e1000: Failed to get physical address for RX descriptors\n");
        return -1;
    }
    
    /* 为每个描述符分配接收缓冲区 */
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        dev->rx_buffers[i] = (uint8_t *)kmalloc_aligned(E1000_RX_BUFFER_SIZE, 16);
        if (!dev->rx_buffers[i]) {
            LOG_ERROR_MSG("e1000: Failed to allocate RX buffer %d\n", i);
            return -1;
        }
        
        /* 设置描述符 - 必须通过页表查询获取真正的物理地址 */
        uint32_t buf_phys = vmm_virt_to_phys((uint32_t)dev->rx_buffers[i]);
        if (!buf_phys) {
            LOG_ERROR_MSG("e1000: Failed to get physical address for RX buffer %d\n", i);
            return -1;
        }
        dev->rx_descs[i].buffer_addr = buf_phys;
        dev->rx_descs[i].status = 0;
    }
    
    dev->rx_cur = 0;
    
    /* 配置接收描述符寄存器 */
    e1000_write_reg(dev, E1000_REG_RDBAL, dev->rx_descs_phys);
    e1000_write_reg(dev, E1000_REG_RDBAH, 0);  // 32 位系统
    e1000_write_reg(dev, E1000_REG_RDLEN, desc_size);
    e1000_write_reg(dev, E1000_REG_RDH, 0);
    e1000_write_reg(dev, E1000_REG_RDT, E1000_NUM_RX_DESC - 1);
    
    LOG_DEBUG_MSG("e1000: RX ring: descs_virt=0x%x descs_phys=0x%x\n", 
                  (uint32_t)dev->rx_descs, dev->rx_descs_phys);
    
    return 0;
}

/**
 * @brief 初始化发送描述符环
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
    
    /* 获取物理地址 - 必须通过页表查询，因为堆内存不是恒等映射 */
    dev->tx_descs_phys = vmm_virt_to_phys((uint32_t)dev->tx_descs);
    if (!dev->tx_descs_phys) {
        LOG_ERROR_MSG("e1000: Failed to get physical address for TX descriptors\n");
        return -1;
    }
    
    /* 为每个描述符分配发送缓冲区 */
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        dev->tx_buffers[i] = (uint8_t *)kmalloc_aligned(E1000_RX_BUFFER_SIZE, 16);
        if (!dev->tx_buffers[i]) {
            LOG_ERROR_MSG("e1000: Failed to allocate TX buffer %d\n", i);
            return -1;
        }
        
        /* 设置描述符 - 必须通过页表查询获取真正的物理地址 */
        uint32_t buf_phys = vmm_virt_to_phys((uint32_t)dev->tx_buffers[i]);
        if (!buf_phys) {
            LOG_ERROR_MSG("e1000: Failed to get physical address for TX buffer %d\n", i);
            return -1;
        }
        dev->tx_descs[i].buffer_addr = buf_phys;
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
    
    LOG_DEBUG_MSG("e1000: TX ring: descs_virt=0x%x descs_phys=0x%x\n", 
                  (uint32_t)dev->tx_descs, dev->tx_descs_phys);
    
    return 0;
}

/* ============================================================================
 * 设备初始化
 * ============================================================================ */

/**
 * @brief 重置设备
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
 * @brief 初始化接收功能
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
 * @brief 初始化发送功能
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
 * @brief 启用中断
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
 * @brief 更新链路状态
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
 * @brief 打开设备
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
 * @brief 关闭设备
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
 * @brief 发送数据包
 */
static int e1000_netdev_transmit(netdev_t *netdev, netbuf_t *buf) {
    e1000_device_t *dev = (e1000_device_t *)netdev->priv;
    
    if (!buf || buf->len == 0 || buf->len > 1518) {
        return -1;
    }
    
    mutex_lock(&e1000_mutex);
    
    uint32_t cur = dev->tx_cur;
    e1000_tx_desc_t *desc = &dev->tx_descs[cur];
    
    /* 等待描述符可用（带超时） */
    int timeout = 10000;
    while (!(desc->status & E1000_TXD_STAT_DD) && timeout-- > 0) {
        /* 忙等待 */
    }
    
    if (timeout <= 0) {
        LOG_WARN_MSG("e1000: TX descriptor not available (timeout)\n");
        mutex_unlock(&e1000_mutex);
        return -1;
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
    
    mutex_unlock(&e1000_mutex);
    
    return 0;
}

/**
 * @brief 设置 MAC 地址
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
 * @brief 处理接收到的数据包
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
 * @brief 中断处理程序
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
 * @brief 检测并初始化单个 E1000 设备
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
    
    /* 初始化 netdev 锁 */
    mutex_init(&dev->netdev.lock);
    
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
 * @brief 初始化 E1000 驱动
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
    buf.head = buf.data;
    buf.tail = buf.data + len;
    buf.end = buf.data + len;
    buf.total_size = len;
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

