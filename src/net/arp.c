/**
 * @file arp.c
 * @brief ARP 协议实现
 */

#include <net/arp.h>
#include <net/ethernet.h>
#include <net/netdev.h>
#include <net/netbuf.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <drivers/timer.h>
#include <kernel/sync/spinlock.h>

// ARP 缓存表
static arp_entry_t arp_cache[ARP_CACHE_SIZE];
static spinlock_t arp_cache_lock;

// 字节序转换
static inline uint16_t arp_ntohs(uint16_t n) {
    return ((n & 0xFF) << 8) | ((n >> 8) & 0xFF);
}

static inline uint16_t arp_htons(uint16_t h) {
    return arp_ntohs(h);
}

/**
 * @brief 查找空闲或可替换的 ARP 缓存条目
 */
static arp_entry_t *arp_cache_find_free(void) {
    arp_entry_t *oldest = NULL;
    uint32_t oldest_time = 0xFFFFFFFF;
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].state == ARP_STATE_FREE) {
            return &arp_cache[i];
        }
        // 记录最旧的条目（LRU 替换）
        if (arp_cache[i].timestamp < oldest_time) {
            oldest_time = arp_cache[i].timestamp;
            oldest = &arp_cache[i];
        }
    }
    
    // 如果没有空闲条目，返回最旧的条目
    return oldest;
}

/**
 * @brief 查找 IP 地址对应的 ARP 缓存条目
 */
static arp_entry_t *arp_cache_find(uint32_t ip) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].state != ARP_STATE_FREE && 
            arp_cache[i].ip_addr == ip) {
            return &arp_cache[i];
        }
    }
    return NULL;
}

/**
 * @brief 发送等待队列中的数据包
 */
static void arp_send_pending(arp_entry_t *entry, netdev_t *dev) {
    netbuf_t *buf = entry->pending_queue;
    while (buf) {
        netbuf_t *next = buf->next;
        buf->next = NULL;
        
        // 发送数据包（通过以太网层）
        // 注意：ethernet_output 成功后，buf 由网卡驱动负责释放
        // 如果失败，需要我们释放
        int ret = ethernet_output(dev, buf, entry->mac_addr, ETH_TYPE_IP);
        if (ret < 0) {
            netbuf_free(buf);
        }
        
        buf = next;
    }
    entry->pending_queue = NULL;
}

/**
 * @brief 释放等待队列中的数据包
 */
static void arp_free_pending(arp_entry_t *entry) {
    netbuf_t *buf = entry->pending_queue;
    while (buf) {
        netbuf_t *next = buf->next;
        netbuf_free(buf);
        buf = next;
    }
    entry->pending_queue = NULL;
}

void arp_init(void) {
    spinlock_init(&arp_cache_lock);
    memset(arp_cache, 0, sizeof(arp_cache));
    
    LOG_INFO_MSG("arp: ARP protocol initialized\n");
}

void arp_input(netdev_t *dev, netbuf_t *buf) {
    if (!dev || !buf) {
        return;
    }
    
    // 检查报文长度
    if (buf->len < sizeof(arp_header_t)) {
        LOG_WARN_MSG("arp: Packet too short (%u bytes)\n", buf->len);
        netbuf_free(buf);
        return;
    }
    
    arp_header_t *arp = (arp_header_t *)buf->data;
    
    // 验证 ARP 报文
    if (arp_ntohs(arp->hardware_type) != ARP_HARDWARE_ETHERNET ||
        arp_ntohs(arp->protocol_type) != ARP_PROTOCOL_IP ||
        arp->hardware_len != 6 ||
        arp->protocol_len != 4) {
        LOG_WARN_MSG("arp: Invalid ARP packet\n");
        netbuf_free(buf);
        return;
    }
    
    uint16_t op = arp_ntohs(arp->operation);
    
    // 更新 ARP 缓存（只要收到 ARP 报文，就更新发送方的地址映射）
    arp_cache_update(arp->sender_ip, arp->sender_mac);
    
    // 检查目标 IP 是否是我们的 IP
    if (arp->target_ip != dev->ip_addr) {
        netbuf_free(buf);
        return;
    }
    
    switch (op) {
        case ARP_OP_REQUEST:
            // 收到 ARP 请求，发送应答
            LOG_DEBUG_MSG("arp: Received ARP request from %u.%u.%u.%u\n",
                         (arp->sender_ip) & 0xFF,
                         (arp->sender_ip >> 8) & 0xFF,
                         (arp->sender_ip >> 16) & 0xFF,
                         (arp->sender_ip >> 24) & 0xFF);
            arp_reply(dev, arp->sender_ip, arp->sender_mac);
            break;
            
        case ARP_OP_REPLY:
            // 收到 ARP 应答，缓存已在上面更新
            LOG_DEBUG_MSG("arp: Received ARP reply from %u.%u.%u.%u\n",
                         (arp->sender_ip) & 0xFF,
                         (arp->sender_ip >> 8) & 0xFF,
                         (arp->sender_ip >> 16) & 0xFF,
                         (arp->sender_ip >> 24) & 0xFF);
            break;
            
        default:
            LOG_WARN_MSG("arp: Unknown operation %u\n", op);
            break;
    }
    
    netbuf_free(buf);
}

int arp_resolve(netdev_t *dev, uint32_t ip, uint8_t *mac) {
    if (!dev || !mac) {
        return -2;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&arp_cache_lock, &irq_state);
    
    // 查找缓存
    arp_entry_t *entry = arp_cache_find(ip);
    
    if (entry) {
        if (entry->state == ARP_STATE_RESOLVED) {
            // 已解析，复制 MAC 地址
            memcpy(mac, entry->mac_addr, 6);
            entry->timestamp = (uint32_t)timer_get_uptime_ms();
            spinlock_unlock_irqrestore(&arp_cache_lock, irq_state);
            return 0;
        } else if (entry->state == ARP_STATE_PENDING) {
            // 正在解析中
            spinlock_unlock_irqrestore(&arp_cache_lock, irq_state);
            return -1;
        }
    }
    
    // 创建新的待解析条目
    entry = arp_cache_find_free();
    if (entry) {
        // 释放旧条目的等待队列
        if (entry->pending_queue) {
            arp_free_pending(entry);
        }
        
        entry->ip_addr = ip;
        entry->state = ARP_STATE_PENDING;
        entry->timestamp = (uint32_t)timer_get_uptime_ms();
        entry->retries = 0;
        entry->pending_queue = NULL;
        memset(entry->mac_addr, 0, 6);
    }
    
    spinlock_unlock_irqrestore(&arp_cache_lock, irq_state);
    
    // 发送 ARP 请求
    arp_request(dev, ip);
    
    return -1;  // 正在解析
}

int arp_request(netdev_t *dev, uint32_t target_ip) {
    if (!dev) {
        return -1;
    }
    
    // 分配缓冲区
    netbuf_t *buf = netbuf_alloc(sizeof(arp_header_t));
    if (!buf) {
        LOG_ERROR_MSG("arp: Failed to allocate buffer\n");
        return -1;
    }
    
    // 填充 ARP 请求
    uint8_t *data = netbuf_put(buf, sizeof(arp_header_t));
    arp_header_t *arp = (arp_header_t *)data;
    
    arp->hardware_type = arp_htons(ARP_HARDWARE_ETHERNET);
    arp->protocol_type = arp_htons(ARP_PROTOCOL_IP);
    arp->hardware_len = 6;
    arp->protocol_len = 4;
    arp->operation = arp_htons(ARP_OP_REQUEST);
    
    memcpy(arp->sender_mac, dev->mac, 6);
    arp->sender_ip = dev->ip_addr;
    memset(arp->target_mac, 0, 6);  // 目标 MAC 未知
    arp->target_ip = target_ip;
    
    // 发送 ARP 请求（广播）
    int ret = ethernet_output(dev, buf, ETH_BROADCAST_ADDR, ETH_TYPE_ARP);
    if (ret < 0) {
        netbuf_free(buf);
    }
    
    return ret;
}

int arp_reply(netdev_t *dev, uint32_t target_ip, const uint8_t *target_mac) {
    if (!dev || !target_mac) {
        return -1;
    }
    
    // 分配缓冲区
    netbuf_t *buf = netbuf_alloc(sizeof(arp_header_t));
    if (!buf) {
        LOG_ERROR_MSG("arp: Failed to allocate buffer\n");
        return -1;
    }
    
    // 填充 ARP 应答
    uint8_t *data = netbuf_put(buf, sizeof(arp_header_t));
    arp_header_t *arp = (arp_header_t *)data;
    
    arp->hardware_type = arp_htons(ARP_HARDWARE_ETHERNET);
    arp->protocol_type = arp_htons(ARP_PROTOCOL_IP);
    arp->hardware_len = 6;
    arp->protocol_len = 4;
    arp->operation = arp_htons(ARP_OP_REPLY);
    
    memcpy(arp->sender_mac, dev->mac, 6);
    arp->sender_ip = dev->ip_addr;
    memcpy(arp->target_mac, target_mac, 6);
    arp->target_ip = target_ip;
    
    // 发送 ARP 应答（单播）
    int ret = ethernet_output(dev, buf, target_mac, ETH_TYPE_ARP);
    if (ret < 0) {
        netbuf_free(buf);
    }
    
    return ret;
}

void arp_cache_update(uint32_t ip, const uint8_t *mac) {
    if (!mac || mac_addr_is_zero(mac)) {
        return;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&arp_cache_lock, &irq_state);
    
    // 查找现有条目
    arp_entry_t *entry = arp_cache_find(ip);
    
    if (!entry) {
        // 创建新条目
        entry = arp_cache_find_free();
        if (!entry) {
            spinlock_unlock_irqrestore(&arp_cache_lock, irq_state);
            return;
        }
        entry->ip_addr = ip;
        entry->pending_queue = NULL;
    }
    
    // 如果有等待发送的数据包，发送它们
    bool had_pending = (entry->state == ARP_STATE_PENDING && entry->pending_queue);
    netdev_t *dev = netdev_get_default();
    
    // 更新条目
    memcpy(entry->mac_addr, mac, 6);
    entry->state = ARP_STATE_RESOLVED;
    entry->timestamp = (uint32_t)timer_get_uptime_ms();
    entry->retries = 0;
    
    // 发送等待的数据包
    if (had_pending && dev) {
        arp_send_pending(entry, dev);
    }
    
    spinlock_unlock_irqrestore(&arp_cache_lock, irq_state);
}

int arp_cache_lookup(uint32_t ip, uint8_t *mac) {
    if (!mac) {
        return -1;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&arp_cache_lock, &irq_state);
    
    arp_entry_t *entry = arp_cache_find(ip);
    
    if (entry && entry->state == ARP_STATE_RESOLVED) {
        memcpy(mac, entry->mac_addr, 6);
        spinlock_unlock_irqrestore(&arp_cache_lock, irq_state);
        return 0;
    }
    
    spinlock_unlock_irqrestore(&arp_cache_lock, irq_state);
    return -1;
}

int arp_cache_add_static(uint32_t ip, const uint8_t *mac) {
    if (!mac) {
        return -1;
    }
    
    // 静态条目使用相同的更新函数
    arp_cache_update(ip, mac);
    return 0;
}

int arp_cache_delete(uint32_t ip) {
    bool irq_state;
    spinlock_lock_irqsave(&arp_cache_lock, &irq_state);
    
    arp_entry_t *entry = arp_cache_find(ip);
    
    if (entry) {
        arp_free_pending(entry);
        memset(entry, 0, sizeof(arp_entry_t));
        entry->state = ARP_STATE_FREE;
        spinlock_unlock_irqrestore(&arp_cache_lock, irq_state);
        return 0;
    }
    
    spinlock_unlock_irqrestore(&arp_cache_lock, irq_state);
    return -1;
}

void arp_cache_cleanup(void) {
    uint32_t now = (uint32_t)timer_get_uptime_ms();
    
    bool irq_state;
    spinlock_lock_irqsave(&arp_cache_lock, &irq_state);
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].state == ARP_STATE_RESOLVED) {
            if (now - arp_cache[i].timestamp > ARP_CACHE_TIMEOUT) {
                // 条目过期
                arp_cache[i].state = ARP_STATE_FREE;
            }
        } else if (arp_cache[i].state == ARP_STATE_PENDING) {
            if (arp_cache[i].retries >= ARP_MAX_RETRIES) {
                // 重试次数过多，释放
                arp_free_pending(&arp_cache[i]);
                arp_cache[i].state = ARP_STATE_FREE;
            }
        }
    }
    
    spinlock_unlock_irqrestore(&arp_cache_lock, irq_state);
}

void arp_cache_clear(void) {
    bool irq_state;
    spinlock_lock_irqsave(&arp_cache_lock, &irq_state);
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_free_pending(&arp_cache[i]);
        memset(&arp_cache[i], 0, sizeof(arp_entry_t));
    }
    
    spinlock_unlock_irqrestore(&arp_cache_lock, irq_state);
}

void arp_cache_dump(void) {
    kprintf("ARP Cache:\n");
    kprintf("%-16s %-18s %-10s\n", "IP Address", "MAC Address", "State");
    kprintf("------------------------------------------------\n");
    
    bool irq_state;
    spinlock_lock_irqsave(&arp_cache_lock, &irq_state);
    
    int count = 0;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].state != ARP_STATE_FREE) {
            uint8_t *ip = (uint8_t *)&arp_cache[i].ip_addr;
            char mac_str[18];
            mac_to_str(arp_cache[i].mac_addr, mac_str);
            
            const char *state_str = "???";
            switch (arp_cache[i].state) {
                case ARP_STATE_PENDING:  state_str = "PENDING"; break;
                case ARP_STATE_RESOLVED: state_str = "RESOLVED"; break;
                default: break;
            }
            
            kprintf("%3u.%3u.%3u.%3u  %s  %s\n",
                    ip[0], ip[1], ip[2], ip[3],
                    mac_str, state_str);
            count++;
        }
    }
    
    spinlock_unlock_irqrestore(&arp_cache_lock, irq_state);
    
    if (count == 0) {
        kprintf("(empty)\n");
    }
}

int arp_cache_count(void) {
    int count = 0;
    
    bool irq_state;
    spinlock_lock_irqsave(&arp_cache_lock, &irq_state);
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].state != ARP_STATE_FREE) {
            count++;
        }
    }
    
    spinlock_unlock_irqrestore(&arp_cache_lock, irq_state);
    
    return count;
}

int arp_cache_get_entry(int index, uint32_t *ip, uint8_t *mac, uint8_t *state) {
    if (index < 0 || index >= ARP_CACHE_SIZE || !ip || !mac || !state) {
        return -1;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&arp_cache_lock, &irq_state);
    
    if (arp_cache[index].state == ARP_STATE_FREE) {
        spinlock_unlock_irqrestore(&arp_cache_lock, irq_state);
        return -1;
    }
    
    *ip = arp_cache[index].ip_addr;
    memcpy(mac, arp_cache[index].mac_addr, 6);
    *state = arp_cache[index].state;
    
    spinlock_unlock_irqrestore(&arp_cache_lock, irq_state);
    return 0;
}

int arp_queue_packet(uint32_t ip, netbuf_t *buf) {
    if (!buf) {
        return -1;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&arp_cache_lock, &irq_state);
    
    arp_entry_t *entry = arp_cache_find(ip);
    
    if (entry && entry->state == ARP_STATE_PENDING) {
        // 添加到等待队列尾部
        buf->next = NULL;
        if (!entry->pending_queue) {
            entry->pending_queue = buf;
        } else {
            netbuf_t *tail = entry->pending_queue;
            while (tail->next) {
                tail = tail->next;
            }
            tail->next = buf;
        }
        spinlock_unlock_irqrestore(&arp_cache_lock, irq_state);
        return 0;
    }
    
    spinlock_unlock_irqrestore(&arp_cache_lock, irq_state);
    return -1;
}

