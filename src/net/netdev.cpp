/**
 * @file netdev.c
 * @brief 网络设备抽象层实现
 */

#include <net/netdev.h>
#include <net/ethernet.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <kernel/task.h>
#include <kernel/interrupt.h>

// 已注册的网络设备
static net::Netdev *netdevs[MAX_NETDEV];
static int netdev_count = 0;

// 默认网络设备
static net::Netdev *default_netdev = NULL;

// 设备编号计数器（用于自动命名）
static int eth_dev_num = 0;

/* 接收队列（说明见 netdev_deliver 上方） */
#define NETDEV_RX_QUEUE_MAX 256

static sync::Spinlock rx_queue_lock;
static net::Netbuf *rx_queue_head = NULL;
static net::Netbuf *rx_queue_tail = NULL;
static uint32_t rx_queue_len = 0;
static bool rx_thread_started = false;

void net::Netdev::init() {
    memset(netdevs, 0, sizeof(netdevs));
    netdev_count = 0;
    rx_queue_lock.init();
    default_netdev = NULL;
    eth_dev_num = 0;
    
    LOG_INFO_MSG("netdev: Network device subsystem initialized\n");
}

net::Netdev *net::Netdev::alloc(const char *name) {
    net::Netdev *dev = (net::Netdev *)kmalloc(sizeof(net::Netdev));
    if (!dev) {
        LOG_ERROR_MSG("netdev: Failed to allocate device structure\n");
        return NULL;
    }
    
    memset(dev, 0, sizeof(net::Netdev));
    
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
    dev->lock.init();
    
    return dev;
}

void net::Netdev::free(net::Netdev *dev) {
    if (dev) {
        kfree(dev);
    }
}

int net::Netdev::register_device(net::Netdev *dev) {
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

int net::Netdev::unregister_device(net::Netdev *dev) {
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

net::Netdev *net::Netdev::get_by_name(const char *name) {
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

net::Netdev *net::Netdev::get_default() {
    return default_netdev;
}

void net::Netdev::set_default(net::Netdev *dev) {
    default_netdev = dev;
}

int net::Netdev::up(net::Netdev *dev) {
    if (!dev) {
        return -1;
    }
    
    dev->lock.lock();
    
    if (dev->state == NETDEV_UP) {
        dev->lock.unlock();
        return 0;  // 已经启用
    }
    
    // 调用驱动的 open 函数
    if (dev->ops) {
        int ret = dev->ops->open(dev);
        if (ret < 0) {
            dev->lock.unlock();
            LOG_ERROR_MSG("netdev: Failed to open device %s\n", dev->name);
            return ret;
        }
    }
    
    dev->state = NETDEV_UP;
    
    dev->lock.unlock();
    
    LOG_INFO_MSG("netdev: Device %s is up\n", dev->name);
    
    return 0;
}

int net::Netdev::down(net::Netdev *dev) {
    if (!dev) {
        return -1;
    }
    
    dev->lock.lock();
    
    if (dev->state == NETDEV_DOWN) {
        dev->lock.unlock();
        return 0;  // 已经禁用
    }
    
    // 调用驱动的 close 函数
    if (dev->ops) {
        int ret = dev->ops->close(dev);
        if (ret < 0) {
            dev->lock.unlock();
            LOG_ERROR_MSG("netdev: Failed to close device %s\n", dev->name);
            return ret;
        }
    }
    
    dev->state = NETDEV_DOWN;
    
    dev->lock.unlock();
    
    LOG_INFO_MSG("netdev: Device %s is down\n", dev->name);
    
    return 0;
}

int net::Netdev::transmit(net::Netdev *dev, net::Netbuf *buf) {
    if (!dev || !buf) {
        return -1;
    }
    
    if (dev->state != NETDEV_UP) {
        LOG_WARN_MSG("netdev: Cannot transmit on device %s (down)\n", dev->name);
        dev->tx_dropped++;
        return -1;
    }
    
    if (!dev->ops) {
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

/* ----------------------------------------------------------------------------
 * 接收队列
 *
 * 中断处理函数只把数据包挂到队列上并唤醒接收线程；以太网/IP/TCP 等协议处理
 * 全部在任务上下文进行（接收线程，或调用 poll()/wait_tick() 的任务）。
 * 这样协议栈可以使用 Mutex、可以发包，也不会和被打断的任务重入同一段代码。
 * -------------------------------------------------------------------------- */

/** 把一个数据包交给协议栈（任务上下文） */
static void netdev_deliver(net::Netdev *dev, net::Netbuf *buf) {
    if (dev->state != NETDEV_UP) {
        net::Netbuf::free(buf);
        dev->rx_dropped++;
        return;
    }
    
    // 更新统计信息
    dev->rx_packets++;
    dev->rx_bytes += buf->len;
    
    // 设置接收设备
    buf->dev = dev;
    
    // 传递给以太网层处理
    net::Ethernet::input(dev, buf);
}

void net::Netdev::receive(net::Netdev *dev, net::Netbuf *buf) {
    if (!dev || !buf) {
        return;
    }
    
    // 任务上下文（环回、测试）直接处理
    if (!in_interrupt()) {
        netdev_deliver(dev, buf);
        return;
    }
    
    // 中断上下文：入队，由接收线程处理
    if (net::Netdev::loopback(dev, buf) < 0) {
        dev->rx_dropped++;
    }
}

int net::Netdev::loopback(net::Netdev *dev, net::Netbuf *buf) {
    if (!dev || !buf) {
        return -1;
    }
    
    buf->dev = dev;
    buf->next = NULL;
    bool dropped = false;
    {
        sync::SpinlockIrqGuard guard(rx_queue_lock);
        if (rx_queue_len >= NETDEV_RX_QUEUE_MAX) {
            dropped = true;
        } else {
            if (rx_queue_tail) {
                rx_queue_tail->next = buf;
            } else {
                rx_queue_head = buf;
            }
            rx_queue_tail = buf;
            rx_queue_len++;
        }
    }
    if (dropped) {
        net::Netbuf::free(buf);
        return -1;
    }
    kernel::Scheduler::wakeup(&rx_queue_head);
    return 0;
}

void net::Netdev::poll() {
    while (true) {
        net::Netbuf *buf;
        {
            sync::SpinlockIrqGuard guard(rx_queue_lock);
            buf = rx_queue_head;
            if (!buf) {
                return;
            }
            rx_queue_head = buf->next;
            if (!rx_queue_head) {
                rx_queue_tail = NULL;
            }
            rx_queue_len--;
        }
        buf->next = NULL;
        netdev_deliver(buf->dev, buf);
    }
}

void net::Netdev::wait_tick() {
    net::Netdev::poll();
    if (kernel::Scheduler::get_current()) {
        kernel::Scheduler::sleep(10);
    }
}

static void netdev_rx_thread(void) {
    while (true) {
        net::Netdev::poll();
        
        // 关中断后再检查一次队列，为空才阻塞：检查和阻塞之间不能插进一次入队
        bool irq_state = kernel::Interrupts::disable();
        if (rx_queue_head == NULL) {
            kernel::Scheduler::block(&rx_queue_head);
        }
        kernel::Interrupts::restore(irq_state);
    }
}

void net::Netdev::start_rx_thread() {
    if (rx_thread_started) {
        return;
    }
    rx_thread_started = true;
    kernel::Scheduler::create_kernel_thread(netdev_rx_thread, "net_rx");
}

void net::Netdev::set_ipaddr(net::Netdev *dev, uint32_t ip) {
    if (!dev) return;
    
    sync::MutexGuard guard(dev->lock);
    dev->ip_addr = ip;
}

void net::Netdev::set_netmask(net::Netdev *dev, uint32_t netmask) {
    if (!dev) return;
    
    sync::MutexGuard guard(dev->lock);
    dev->netmask = netmask;
}

void net::Netdev::set_gateway(net::Netdev *dev, uint32_t gateway) {
    if (!dev) return;
    
    sync::MutexGuard guard(dev->lock);
    dev->gateway = gateway;
}

int net::Netdev::get_all(net::Netdev **devs, int max_count) {
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

void net::Netdev::print_info(net::Netdev *dev) {
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

void net::Netdev::print_all() {
    if (netdev_count == 0) {
        kprintf("No network devices registered.\n");
        return;
    }
    
    for (int i = 0; i < netdev_count; i++) {
        net::Netdev::print_info(netdevs[i]);
        if (i < netdev_count - 1) {
            kprintf("\n");
        }
    }
}

