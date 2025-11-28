/**
 * @file netdev.c
 * @brief 网络设备抽象层实现
 */

#include <net/netdev.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <lib/kprintf.h>

// 已注册的网络设备
static netdev_t *netdevs[MAX_NETDEV];
static int netdev_count = 0;

// 默认网络设备
static netdev_t *default_netdev = NULL;

// 设备编号计数器（用于自动命名）
static int eth_dev_num = 0;

void netdev_init(void) {
    memset(netdevs, 0, sizeof(netdevs));
    netdev_count = 0;
    default_netdev = NULL;
    eth_dev_num = 0;
    
    LOG_INFO_MSG("netdev: Network device subsystem initialized\n");
}

netdev_t *netdev_alloc(const char *name) {
    netdev_t *dev = (netdev_t *)kmalloc(sizeof(netdev_t));
    if (!dev) {
        LOG_ERROR_MSG("netdev: Failed to allocate device structure\n");
        return NULL;
    }
    
    memset(dev, 0, sizeof(netdev_t));
    
    // 生成设备名称
    if (name) {
        snprintf(dev->name, NETDEV_NAME_LEN, "%s%d", name, eth_dev_num++);
    } else {
        snprintf(dev->name, NETDEV_NAME_LEN, "eth%d", eth_dev_num++);
    }
    
    // 默认值
    dev->state = NETDEV_DOWN;
    dev->mtu = 1500;
    
    // 初始化互斥锁
    mutex_init(&dev->lock);
    
    return dev;
}

void netdev_free(netdev_t *dev) {
    if (dev) {
        kfree(dev);
    }
}

int netdev_register(netdev_t *dev) {
    if (!dev) {
        return -1;
    }
    
    if (netdev_count >= MAX_NETDEV) {
        LOG_ERROR_MSG("netdev: Maximum device count reached\n");
        return -1;
    }
    
    // 检查是否已注册
    for (int i = 0; i < netdev_count; i++) {
        if (netdevs[i] == dev) {
            LOG_WARN_MSG("netdev: Device %s already registered\n", dev->name);
            return -1;
        }
    }
    
    netdevs[netdev_count++] = dev;
    
    // 如果是第一个设备，设置为默认设备
    if (default_netdev == NULL) {
        default_netdev = dev;
    }
    
    LOG_INFO_MSG("netdev: Registered device %s (MAC: %02x:%02x:%02x:%02x:%02x:%02x)\n",
                 dev->name,
                 dev->mac[0], dev->mac[1], dev->mac[2],
                 dev->mac[3], dev->mac[4], dev->mac[5]);
    
    return 0;
}

int netdev_unregister(netdev_t *dev) {
    if (!dev) {
        return -1;
    }
    
    int found = -1;
    for (int i = 0; i < netdev_count; i++) {
        if (netdevs[i] == dev) {
            found = i;
            break;
        }
    }
    
    if (found < 0) {
        LOG_WARN_MSG("netdev: Device %s not found\n", dev->name);
        return -1;
    }
    
    // 如果是默认设备，清除默认设备
    if (default_netdev == dev) {
        default_netdev = NULL;
    }
    
    // 移除设备（将后面的设备前移）
    for (int i = found; i < netdev_count - 1; i++) {
        netdevs[i] = netdevs[i + 1];
    }
    netdevs[--netdev_count] = NULL;
    
    // 如果还有其他设备，选择第一个作为默认设备
    if (default_netdev == NULL && netdev_count > 0) {
        default_netdev = netdevs[0];
    }
    
    LOG_INFO_MSG("netdev: Unregistered device %s\n", dev->name);
    
    return 0;
}

netdev_t *netdev_get_by_name(const char *name) {
    if (!name) {
        return NULL;
    }
    
    for (int i = 0; i < netdev_count; i++) {
        if (strcmp(netdevs[i]->name, name) == 0) {
            return netdevs[i];
        }
    }
    
    return NULL;
}

netdev_t *netdev_get_default(void) {
    return default_netdev;
}

void netdev_set_default(netdev_t *dev) {
    default_netdev = dev;
}

int netdev_up(netdev_t *dev) {
    if (!dev) {
        return -1;
    }
    
    mutex_lock(&dev->lock);
    
    if (dev->state == NETDEV_UP) {
        mutex_unlock(&dev->lock);
        return 0;  // 已经启用
    }
    
    // 调用驱动的 open 函数
    if (dev->ops && dev->ops->open) {
        int ret = dev->ops->open(dev);
        if (ret < 0) {
            mutex_unlock(&dev->lock);
            LOG_ERROR_MSG("netdev: Failed to open device %s\n", dev->name);
            return ret;
        }
    }
    
    dev->state = NETDEV_UP;
    
    mutex_unlock(&dev->lock);
    
    LOG_INFO_MSG("netdev: Device %s is up\n", dev->name);
    
    return 0;
}

int netdev_down(netdev_t *dev) {
    if (!dev) {
        return -1;
    }
    
    mutex_lock(&dev->lock);
    
    if (dev->state == NETDEV_DOWN) {
        mutex_unlock(&dev->lock);
        return 0;  // 已经禁用
    }
    
    // 调用驱动的 close 函数
    if (dev->ops && dev->ops->close) {
        int ret = dev->ops->close(dev);
        if (ret < 0) {
            mutex_unlock(&dev->lock);
            LOG_ERROR_MSG("netdev: Failed to close device %s\n", dev->name);
            return ret;
        }
    }
    
    dev->state = NETDEV_DOWN;
    
    mutex_unlock(&dev->lock);
    
    LOG_INFO_MSG("netdev: Device %s is down\n", dev->name);
    
    return 0;
}

int netdev_transmit(netdev_t *dev, netbuf_t *buf) {
    if (!dev || !buf) {
        return -1;
    }
    
    if (dev->state != NETDEV_UP) {
        LOG_WARN_MSG("netdev: Cannot transmit on device %s (down)\n", dev->name);
        dev->tx_dropped++;
        return -1;
    }
    
    if (!dev->ops || !dev->ops->transmit) {
        LOG_ERROR_MSG("netdev: Device %s has no transmit function\n", dev->name);
        dev->tx_errors++;
        return -1;
    }
    
    int ret = dev->ops->transmit(dev, buf);
    
    if (ret < 0) {
        dev->tx_errors++;
    } else {
        dev->tx_packets++;
        dev->tx_bytes += buf->len;
    }
    
    return ret;
}

// 前向声明以太网输入处理函数
extern void ethernet_input(netdev_t *dev, netbuf_t *buf);

void netdev_receive(netdev_t *dev, netbuf_t *buf) {
    if (!dev || !buf) {
        return;
    }
    
    if (dev->state != NETDEV_UP) {
        netbuf_free(buf);
        dev->rx_dropped++;
        return;
    }
    
    // 更新统计信息
    dev->rx_packets++;
    dev->rx_bytes += buf->len;
    
    // 设置接收设备
    buf->dev = dev;
    
    // 传递给以太网层处理
    ethernet_input(dev, buf);
}

int netdev_set_ip(netdev_t *dev, uint32_t ip, uint32_t netmask, uint32_t gateway) {
    if (!dev) {
        return -1;
    }
    
    mutex_lock(&dev->lock);
    
    dev->ip_addr = ip;
    dev->netmask = netmask;
    dev->gateway = gateway;
    
    mutex_unlock(&dev->lock);
    
    return 0;
}

int netdev_get_all(netdev_t **devs, int max_count) {
    if (!devs || max_count <= 0) {
        return 0;
    }
    
    int count = (netdev_count < max_count) ? netdev_count : max_count;
    for (int i = 0; i < count; i++) {
        devs[i] = netdevs[i];
    }
    
    return count;
}

/**
 * @brief 将 IP 地址转换为字符串（用于打印）
 */
static void ip_to_str_internal(uint32_t ip, char *buf) {
    // IP 地址是网络字节序（大端），需要按字节提取
    uint8_t *bytes = (uint8_t *)&ip;
    snprintf(buf, 16, "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
}

void netdev_print_info(netdev_t *dev) {
    if (!dev) {
        return;
    }
    
    char ip_str[16], netmask_str[16], gateway_str[16];
    ip_to_str_internal(dev->ip_addr, ip_str);
    ip_to_str_internal(dev->netmask, netmask_str);
    ip_to_str_internal(dev->gateway, gateway_str);
    
    kprintf("%s: flags=%s  mtu %u\n", dev->name,
            dev->state == NETDEV_UP ? "UP" : "DOWN", dev->mtu);
    kprintf("        inet %s  netmask %s  gateway %s\n",
            ip_str, netmask_str, gateway_str);
    kprintf("        ether %02x:%02x:%02x:%02x:%02x:%02x\n",
            dev->mac[0], dev->mac[1], dev->mac[2],
            dev->mac[3], dev->mac[4], dev->mac[5]);
    kprintf("        RX packets %llu  bytes %llu  errors %llu  dropped %llu\n",
            dev->rx_packets, dev->rx_bytes, dev->rx_errors, dev->rx_dropped);
    kprintf("        TX packets %llu  bytes %llu  errors %llu  dropped %llu\n",
            dev->tx_packets, dev->tx_bytes, dev->tx_errors, dev->tx_dropped);
}

void netdev_print_all(void) {
    if (netdev_count == 0) {
        kprintf("No network devices registered.\n");
        return;
    }
    
    for (int i = 0; i < netdev_count; i++) {
        netdev_print_info(netdevs[i]);
        if (i < netdev_count - 1) {
            kprintf("\n");
        }
    }
}

