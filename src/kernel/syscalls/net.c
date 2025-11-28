/**
 * @file net.c
 * @brief 网络相关系统调用实现
 * 
 * 提供 ioctl 接口用于网络配置（ifconfig、arp 等功能）。
 * BSD Socket API 由 socket.c 实现。
 */

#include <kernel/syscalls/net.h>
#include <net/net.h>
#include <net/netdev.h>
#include <net/ip.h>
#include <net/arp.h>
#include <net/icmp.h>
#include <lib/string.h>
#include <lib/kprintf.h>
#include <lib/klog.h>
#include <drivers/timer.h>

/**
 * @brief 处理网络接口 ioctl
 */
static int32_t netif_ioctl(uint32_t request, struct ifreq *ifr) {
    if (!ifr) {
        return -1;
    }
    
    // 查找网络设备
    netdev_t *dev = NULL;
    if (ifr->ifr_name[0] != '\0') {
        dev = netdev_get_by_name(ifr->ifr_name);
    } else {
        dev = netdev_get_default();
    }
    
    if (!dev && request != SIOCGIFCONF) {
        return -1;
    }
    
    switch (request) {
        case SIOCGIFADDR:
            // 获取 IP 地址
            ifr->ifr_addr.sin_family = AF_INET;
            ifr->ifr_addr.sin_addr = dev->ip_addr;
            return 0;
            
        case SIOCSIFADDR:
            // 设置 IP 地址
            return netdev_set_ip(dev, ifr->ifr_addr.sin_addr, 
                                dev->netmask, dev->gateway);
            
        case SIOCGIFNETMASK:
            // 获取子网掩码
            ifr->ifr_netmask.sin_family = AF_INET;
            ifr->ifr_netmask.sin_addr = dev->netmask;
            return 0;
            
        case SIOCSIFNETMASK:
            // 设置子网掩码
            return netdev_set_ip(dev, dev->ip_addr, 
                                ifr->ifr_netmask.sin_addr, dev->gateway);
            
        case SIOCGIFGATEWAY:
            // 获取网关地址
            ifr->ifr_gateway.sin_family = AF_INET;
            ifr->ifr_gateway.sin_addr = dev->gateway;
            return 0;
            
        case SIOCSIFGATEWAY:
            // 设置网关地址
            return netdev_set_ip(dev, dev->ip_addr, 
                                dev->netmask, ifr->ifr_gateway.sin_addr);
            
        case SIOCGIFFLAGS:
            // 获取接口标志
            ifr->ifr_flags = 0;
            if (dev->state == NETDEV_UP) {
                ifr->ifr_flags |= IFF_UP | IFF_RUNNING;
            }
            ifr->ifr_flags |= IFF_BROADCAST;
            return 0;
            
        case SIOCSIFFLAGS:
            // 设置接口标志
            if (ifr->ifr_flags & IFF_UP) {
                return netdev_up(dev);
            } else {
                return netdev_down(dev);
            }
            
        case SIOCGIFHWADDR:
            // 获取 MAC 地址
            memcpy(ifr->ifr_hwaddr.sa_data, dev->mac, 6);
            return 0;
            
        case SIOCGIFMTU:
            // 获取 MTU
            ifr->ifr_mtu = dev->mtu;
            return 0;
            
        case SIOCSIFMTU:
            // 设置 MTU
            dev->mtu = ifr->ifr_mtu;
            return 0;
            
        default:
            return -1;
    }
}

/**
 * @brief 处理 ARP ioctl
 */
static int32_t arp_ioctl(uint32_t request, struct arpreq *arpreq) {
    if (!arpreq) {
        return -1;
    }
    
    uint32_t ip = arpreq->arp_pa.sin_addr;
    
    switch (request) {
        case SIOCGARP: {
            // 获取 ARP 条目
            uint8_t mac[6];
            if (arp_cache_lookup(ip, mac) == 0) {
                memcpy(arpreq->arp_ha.sa_data, mac, 6);
                arpreq->arp_flags = ATF_COM;
                return 0;
            }
            return -1;
        }
        
        case SIOCSARP:
            // 添加 ARP 条目
            arp_cache_update(ip, (uint8_t *)arpreq->arp_ha.sa_data);
            return 0;
            
        case SIOCDARP:
            // 删除 ARP 条目
            return arp_cache_delete(ip);
            
        default:
            return -1;
    }
}

/**
 * @brief 处理网络接口统计信息获取（CastorOS 扩展）
 */
static int32_t ifstats_ioctl(struct ifstats *stats) {
    if (!stats) {
        return -1;
    }
    
    // 查找网络设备
    netdev_t *dev = NULL;
    if (stats->ifr_name[0] != '\0') {
        dev = netdev_get_by_name(stats->ifr_name);
    } else {
        dev = netdev_get_default();
    }
    
    if (!dev) {
        return -1;
    }
    
    // 填充统计信息
    strncpy(stats->ifr_name, dev->name, sizeof(stats->ifr_name) - 1);
    stats->ifr_name[sizeof(stats->ifr_name) - 1] = '\0';
    stats->rx_packets = dev->rx_packets;
    stats->tx_packets = dev->tx_packets;
    stats->rx_bytes = dev->rx_bytes;
    stats->tx_bytes = dev->tx_bytes;
    
    return 0;
}

/**
 * @brief 处理 ICMP ping（CastorOS 扩展）
 */
static int32_t ping_ioctl(struct ping_req *req) {
    if (!req) {
        return -1;
    }
    
    // 检查是否有网络设备
    netdev_t *dev = netdev_get_default();
    if (!dev) {
        LOG_WARN_MSG("ping: No network device available\n");
        return -1;
    }
    
    if (dev->ip_addr == 0) {
        LOG_WARN_MSG("ping: Network interface not configured\n");
        return -1;
    }
    
    // 解析目标 IP
    uint32_t dst_ip;
    if (str_to_ip(req->host, &dst_ip) < 0) {
        LOG_WARN_MSG("ping: Invalid IP address '%s'\n", req->host);
        return -1;
    }
    
    int count = req->count;
    if (count <= 0) count = 4;
    if (count > 100) count = 100;
    
    // 初始化结果
    req->sent = 0;
    req->received = 0;
    req->min_rtt = 0xFFFFFFFF;
    req->max_rtt = 0;
    req->avg_rtt = 0;
    
    uint32_t total_rtt = 0;
    
    // 打印 ping 开始信息
    char ip_str[16];
    ip_to_str(dst_ip, ip_str);
    kprintf("PING %s: %d data bytes\n", ip_str, 56);
    
    for (int i = 0; i < count; i++) {
        uint16_t id = (uint16_t)(dev->ip_addr & 0xFFFF);
        uint16_t seq = (uint16_t)i;
        
        // 发送 ping
        if (icmp_send_echo_request(dst_ip, id, seq, NULL, 56) < 0) {
            kprintf("ping: send failed\n");
            continue;
        }
        req->sent++;
        
        // 等待响应（简单实现：轮询等待）
        uint32_t start_time = (uint32_t)timer_get_uptime_ms();
        uint32_t timeout = req->timeout_ms > 0 ? (uint32_t)req->timeout_ms : 1000;
        
        while ((uint32_t)timer_get_uptime_ms() - start_time < timeout) {
            int32_t rtt = icmp_get_last_rtt();
            if (rtt >= 0) {
                req->received++;
                uint32_t rtt_u = (uint32_t)rtt;
                
                if (rtt_u < req->min_rtt) req->min_rtt = rtt_u;
                if (rtt_u > req->max_rtt) req->max_rtt = rtt_u;
                total_rtt += rtt_u;
                
                kprintf("Reply from %s: seq=%d time=%d ms\n", ip_str, seq, rtt_u);
                break;
            }
            
            // 简单延迟
            for (volatile int j = 0; j < 10000; j++);
        }
        
        // 间隔 1 秒
        if (i < count - 1) {
            uint32_t delay_start = (uint32_t)timer_get_uptime_ms();
            while ((uint32_t)timer_get_uptime_ms() - delay_start < 1000) {
                for (volatile int j = 0; j < 10000; j++);
            }
        }
    }
    
    // 计算平均 RTT
    if (req->received > 0) {
        req->avg_rtt = total_rtt / req->received;
    } else {
        req->min_rtt = 0;
    }
    
    // 打印统计
    kprintf("--- %s ping statistics ---\n", ip_str);
    kprintf("%u packets transmitted, %u received, %u%% packet loss\n",
            req->sent, req->received,
            req->sent > 0 ? ((req->sent - req->received) * 100 / req->sent) : 0);
    
    if (req->received > 0) {
        kprintf("rtt min/avg/max = %u/%u/%u ms\n",
                req->min_rtt, req->avg_rtt, req->max_rtt);
    }
    
    return 0;
}

/**
 * @brief ioctl 系统调用实现
 */
int32_t sys_ioctl(int32_t fd, uint32_t request, void *argp) {
    (void)fd;  // 暂时不使用 fd，直接根据 request 类型处理
    
    // 网络接口 ioctl
    if (request >= SIOCGIFADDR && request <= SIOCSIFGATEWAY) {
        return netif_ioctl(request, (struct ifreq *)argp);
    }
    
    // ARP ioctl
    if (request >= SIOCSARP && request <= SIOCDARP) {
        return arp_ioctl(request, (struct arpreq *)argp);
    }
    
    // Ping ioctl（CastorOS 扩展）
    if (request == SIOCPING) {
        return ping_ioctl((struct ping_req *)argp);
    }
    
    // 网络接口统计 ioctl（CastorOS 扩展）
    if (request == SIOCGIFSTATS) {
        return ifstats_ioctl((struct ifstats *)argp);
    }
    
    LOG_WARN_MSG("ioctl: Unsupported request 0x%x\n", request);
    return -1;
}

/* ============================================================================
 * 兼容性函数（保持向后兼容，供 kernel_shell 使用）
 * 这些函数直接调用 ioctl 实现
 * ============================================================================ */

/**
 * @brief 获取网络接口信息（供内核 shell 使用）
 */
int32_t netif_get_info(const char *name, netif_info_t *info) {
    if (!info) {
        return -1;
    }
    
    netdev_t *dev = NULL;
    if (name && name[0] != '\0') {
        dev = netdev_get_by_name(name);
    } else {
        dev = netdev_get_default();
    }
    
    if (!dev) {
        return -1;
    }
    
    strncpy(info->name, dev->name, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    memcpy(info->mac, dev->mac, 6);
    info->ip_addr = dev->ip_addr;
    info->netmask = dev->netmask;
    info->gateway = dev->gateway;
    info->mtu = dev->mtu;
    info->state = (dev->state == NETDEV_UP) ? 1 : 0;
    info->rx_packets = dev->rx_packets;
    info->tx_packets = dev->tx_packets;
    info->rx_bytes = dev->rx_bytes;
    info->tx_bytes = dev->tx_bytes;
    
    return 0;
}

/**
 * @brief 设置网络接口配置（供内核 shell 使用）
 */
int32_t netif_set_config(const char *name, uint32_t ip, uint32_t netmask, uint32_t gateway) {
    netdev_t *dev = NULL;
    if (name && name[0] != '\0') {
        dev = netdev_get_by_name(name);
    } else {
        dev = netdev_get_default();
    }
    
    if (!dev) {
        return -1;
    }
    
    return netdev_set_ip(dev, ip, netmask, gateway);
}

/**
 * @brief 启用/禁用网络接口（供内核 shell 使用）
 */
int32_t netif_set_state(const char *name, bool up) {
    netdev_t *dev = NULL;
    if (name && name[0] != '\0') {
        dev = netdev_get_by_name(name);
    } else {
        dev = netdev_get_default();
    }
    
    if (!dev) {
        return -1;
    }
    
    return up ? netdev_up(dev) : netdev_down(dev);
}

/**
 * @brief 执行 ping（供内核 shell 使用）
 */
int32_t netif_ping(const char *host, int count, int timeout_ms,
                   uint32_t *sent, uint32_t *received,
                   uint32_t *min_rtt, uint32_t *max_rtt, uint32_t *avg_rtt) {
    struct ping_req req;
    
    strncpy(req.host, host, sizeof(req.host) - 1);
    req.host[sizeof(req.host) - 1] = '\0';
    req.count = count;
    req.timeout_ms = timeout_ms;
    
    int ret = ping_ioctl(&req);
    
    if (sent) *sent = req.sent;
    if (received) *received = req.received;
    if (min_rtt) *min_rtt = req.min_rtt;
    if (max_rtt) *max_rtt = req.max_rtt;
    if (avg_rtt) *avg_rtt = req.avg_rtt;
    
    return ret;
}

/**
 * @brief 获取 ARP 缓存（供内核 shell 使用）
 */
int32_t netif_arp_get(arp_entry_info_t *entries, int32_t max_entries) {
    if (!entries || max_entries <= 0) {
        return -1;
    }
    
    int count = 0;
    
    // 遍历 ARP 缓存
    for (int i = 0; i < ARP_CACHE_SIZE && count < max_entries; i++) {
        uint32_t ip;
        uint8_t mac[6];
        uint8_t state;
        
        if (arp_cache_get_entry(i, &ip, mac, &state) == 0) {
            entries[count].ip_addr = ip;
            memcpy(entries[count].mac, mac, 6);
            entries[count].state = state;
            count++;
        }
    }
    
    return count;
}

/**
 * @brief 删除 ARP 条目（供内核 shell 使用）
 */
int32_t netif_arp_delete(const char *ip_str) {
    if (!ip_str) {
        return -1;
    }
    
    uint32_t ip;
    if (str_to_ip(ip_str, &ip) < 0) {
        return -1;
    }
    
    return arp_cache_delete(ip);
}
