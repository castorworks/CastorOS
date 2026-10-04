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
static sync::Spinlock arp_cache_lock;

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
 * @brief 发送一条已从条目上摘下的等待队列，并释放其中的数据包
 *
 * 调用时不能持有 arp_cache_lock：发送会走到驱动（Mutex）。
 */
static void arp_send_queue(net::Netbuf *queue, net::Netdev *dev, const uint8_t *mac) {
    while (queue) {
        net::Netbuf *next = queue->next;
        queue->next = NULL;
        if (dev) {
            net::Ethernet::output(dev, queue, mac, ETH_TYPE_IP);
        }
        // 队列里的是 queue_packet() 克隆的副本，发送只是借用，这里负责释放
        net::Netbuf::free(queue);
        queue = next;
    }
}

/**
 * @brief 释放等待队列中的数据包
 */
static void arp_free_pending(arp_entry_t *entry) {
    net::Netbuf *buf = entry->pending_queue;
    while (buf) {
        net::Netbuf *next = buf->next;
        net::Netbuf::free(buf);
        buf = next;
    }
    entry->pending_queue = NULL;
    entry->pending_count = 0;
}

/**
 * @brief PENDING 条目到了重试时间时的处理（调用者持有 arp_cache_lock）
 * @return true 需要重发 ARP 请求（由调用者在解锁后发送）；
 *         false 不需要（还没到时间，或重试次数用尽、条目已释放）
 */
static bool arp_pending_retry_locked(arp_entry_t *entry, uint32_t now, bool *gave_up) {
    *gave_up = false;
    if (now - entry->last_request < ARP_RETRY_INTERVAL) {
        return false;
    }
    if (entry->retries >= ARP_MAX_RETRIES) {
        // 对端一直不应答：丢弃排队的数据包并释放条目，下次发送重新开始解析
        arp_free_pending(entry);
        entry->state = ARP_STATE_FREE;
        *gave_up = true;
        return false;
    }
    entry->retries++;
    entry->last_request = now;
    return true;
}

void net::Arp::init() {
    arp_cache_lock.init();
    memset(arp_cache, 0, sizeof(arp_cache));
    
    LOG_INFO_MSG("arp: ARP protocol initialized\n");
}

void net::Arp::input(net::Netdev *dev, net::Netbuf *buf) {
    if (!dev || !buf) {
        return;
    }
    
    // 检查报文长度
    if (buf->len < sizeof(arp_header_t)) {
        LOG_WARN_MSG("arp: Packet too short (%u bytes)\n", buf->len);
        net::Netbuf::free(buf);
        return;
    }
    
    arp_header_t *arp = (arp_header_t *)buf->data;
    
    // 验证 ARP 报文
    if (arp_ntohs(arp->hardware_type) != ARP_HARDWARE_ETHERNET ||
        arp_ntohs(arp->protocol_type) != ARP_PROTOCOL_IP ||
        arp->hardware_len != 6 ||
        arp->protocol_len != 4) {
        LOG_WARN_MSG("arp: Invalid ARP packet\n");
        net::Netbuf::free(buf);
        return;
    }
    
    uint16_t op = arp_ntohs(arp->operation);
    
    // 更新 ARP 缓存（只要收到 ARP 报文，就更新发送方的地址映射）
    net::Arp::cache_update(arp->sender_ip, arp->sender_mac);
    
    // 检查目标 IP 是否是我们的 IP
    if (arp->target_ip != dev->ip_addr) {
        net::Netbuf::free(buf);
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
            net::Arp::reply(dev, arp->sender_ip, arp->sender_mac);
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
    
    net::Netbuf::free(buf);
}

int net::Arp::resolve(net::Netdev *dev, uint32_t ip, uint8_t *mac) {
    if (!dev || !mac) {
        return -2;
    }
    
    uint32_t now = (uint32_t)drivers::Timer::get_uptime_ms();
    
    {
        sync::SpinlockIrqGuard guard(arp_cache_lock);
        // 查找缓存
        arp_entry_t *entry = arp_cache_find(ip);
    
        if (entry && entry->state == ARP_STATE_RESOLVED) {
            // 已解析，复制 MAC 地址
            memcpy(mac, entry->mac_addr, 6);
            entry->timestamp = now;
            return 0;
        }
        
        if (entry && entry->state == ARP_STATE_PENDING) {
            // 正在解析中：到了重试间隔就重发请求，重试用尽则放弃
            bool gave_up;
            if (!arp_pending_retry_locked(entry, now, &gave_up)) {
                return gave_up ? -2 : -1;
            }
            entry->dev = dev;
        } else {
            // 创建新的待解析条目
            entry = arp_cache_find_free();
            if (!entry) {
                return -2;
            }
            // 释放被替换条目的等待队列
            arp_free_pending(entry);
        
            entry->ip_addr = ip;
            entry->state = ARP_STATE_PENDING;
            entry->timestamp = now;
            entry->last_request = now;
            entry->retries = 0;
            entry->dev = dev;
            memset(entry->mac_addr, 0, 6);
        }
    }
    
    // 发送 ARP 请求（锁外：发送会走到驱动）
    net::Arp::request(dev, ip);
    
    return -1;  // 正在解析
}

int net::Arp::request(net::Netdev *dev, uint32_t target_ip) {
    if (!dev) {
        return -1;
    }
    
    // 分配缓冲区
    net::Netbuf *buf = net::Netbuf::alloc(sizeof(arp_header_t));
    if (!buf) {
        LOG_ERROR_MSG("arp: Failed to allocate buffer\n");
        return -1;
    }
    
    // 填充 ARP 请求
    uint8_t *data = net::Netbuf::put(buf, sizeof(arp_header_t));
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
    int ret = net::Ethernet::output(dev, buf, ETH_BROADCAST_ADDR, ETH_TYPE_ARP);
    net::Netbuf::free(buf);  // 发送只是借用 buf
    
    return ret;
}

int net::Arp::reply(net::Netdev *dev, uint32_t target_ip, const uint8_t *target_mac) {
    if (!dev || !target_mac) {
        return -1;
    }
    
    // 分配缓冲区
    net::Netbuf *buf = net::Netbuf::alloc(sizeof(arp_header_t));
    if (!buf) {
        LOG_ERROR_MSG("arp: Failed to allocate buffer\n");
        return -1;
    }
    
    // 填充 ARP 应答
    uint8_t *data = net::Netbuf::put(buf, sizeof(arp_header_t));
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
    int ret = net::Ethernet::output(dev, buf, target_mac, ETH_TYPE_ARP);
    net::Netbuf::free(buf);  // 发送只是借用 buf
    
    return ret;
}

void net::Arp::cache_update(uint32_t ip, const uint8_t *mac) {
    if (!mac || mac_addr_is_zero(mac)) {
        return;
    }
    
    net::Netbuf *queue = NULL;
    net::Netdev *dev = NULL;
    
    {
        sync::SpinlockIrqGuard guard(arp_cache_lock);
        
        // 查找现有条目
        arp_entry_t *entry = arp_cache_find(ip);
        
        if (!entry) {
            // 创建新条目
            entry = arp_cache_find_free();
            if (!entry) {
                return;
            }
            // 被替换的条目可能还挂着等待队列
            arp_free_pending(entry);
            entry->ip_addr = ip;
            entry->dev = NULL;
        }
        
        // 摘下等待发送的数据包，解锁后再发送
        if (entry->state == ARP_STATE_PENDING) {
            queue = entry->pending_queue;
            dev = entry->dev;
        } else if (entry->pending_queue) {
            arp_free_pending(entry);
        }
        entry->pending_queue = NULL;
        entry->pending_count = 0;
        
        // 更新条目
        memcpy(entry->mac_addr, mac, 6);
        entry->state = ARP_STATE_RESOLVED;
        entry->timestamp = (uint32_t)drivers::Timer::get_uptime_ms();
        entry->retries = 0;
    }
    
    // 发送等待的数据包（不持有自旋锁）
    if (queue) {
        if (!dev) {
            dev = net::Netdev::get_default();
        }
        arp_send_queue(queue, dev, mac);
    }
}

int net::Arp::cache_lookup(uint32_t ip, uint8_t *mac) {
    if (!mac) {
        return -1;
    }
    
    sync::SpinlockIrqGuard guard(arp_cache_lock);
    
    arp_entry_t *entry = arp_cache_find(ip);
    
    if (entry && entry->state == ARP_STATE_RESOLVED) {
        memcpy(mac, entry->mac_addr, 6);
        return 0;
    }
    
    return -1;
}

int net::Arp::cache_add_static(uint32_t ip, const uint8_t *mac) {
    if (!mac) {
        return -1;
    }
    
    // 静态条目使用相同的更新函数
    net::Arp::cache_update(ip, mac);
    return 0;
}

int net::Arp::cache_delete(uint32_t ip) {
    sync::SpinlockIrqGuard guard(arp_cache_lock);
    
    arp_entry_t *entry = arp_cache_find(ip);
    
    if (entry) {
        arp_free_pending(entry);
        memset(entry, 0, sizeof(arp_entry_t));
        entry->state = ARP_STATE_FREE;
        return 0;
    }
    
    return -1;
}

void net::Arp::cache_cleanup() {
    uint32_t now = (uint32_t)drivers::Timer::get_uptime_ms();
    
    // 需要重发请求的条目先记下来，解锁后再发送
    struct { net::Netdev *dev; uint32_t ip; } retry[ARP_CACHE_SIZE];
    int retry_count = 0;
    
    {
        sync::SpinlockIrqGuard guard(arp_cache_lock);
        
        for (int i = 0; i < ARP_CACHE_SIZE; i++) {
            arp_entry_t *entry = &arp_cache[i];
            if (entry->state == ARP_STATE_RESOLVED) {
                if (now - entry->timestamp > ARP_CACHE_TIMEOUT) {
                    // 条目过期
                    arp_free_pending(entry);
                    entry->state = ARP_STATE_FREE;
                }
            } else if (entry->state == ARP_STATE_PENDING) {
                bool gave_up;
                if (arp_pending_retry_locked(entry, now, &gave_up) && entry->dev) {
                    retry[retry_count].dev = entry->dev;
                    retry[retry_count].ip = entry->ip_addr;
                    retry_count++;
                }
            }
        }
    }
    
    for (int i = 0; i < retry_count; i++) {
        net::Arp::request(retry[i].dev, retry[i].ip);
    }
}

void net::Arp::cache_clear() {
    sync::SpinlockIrqGuard guard(arp_cache_lock);
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_free_pending(&arp_cache[i]);
        memset(&arp_cache[i], 0, sizeof(arp_entry_t));
    }
}

void net::Arp::cache_dump() {
    kprintf("ARP Cache:\n");
    kprintf("%-16s %-18s %-10s\n", "IP Address", "MAC Address", "State");
    kprintf("------------------------------------------------\n");
    
    bool irq_state;
    arp_cache_lock.lock_irqsave(irq_state);
    
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
    
    arp_cache_lock.unlock_irqrestore(irq_state);
    
    if (count == 0) {
        kprintf("(empty)\n");
    }
}

int net::Arp::cache_count() {
    int count = 0;
    
    sync::SpinlockIrqGuard guard(arp_cache_lock);
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].state != ARP_STATE_FREE) {
            count++;
        }
    }
    
    return count;
}

int net::Arp::cache_get_entry(int index, uint32_t *ip, uint8_t *mac, uint8_t *state) {
    if (index < 0 || index >= ARP_CACHE_SIZE || !ip || !mac || !state) {
        return -1;
    }
    
    sync::SpinlockIrqGuard guard(arp_cache_lock);
    
    if (arp_cache[index].state == ARP_STATE_FREE) {
        return -1;
    }
    
    *ip = arp_cache[index].ip_addr;
    memcpy(mac, arp_cache[index].mac_addr, 6);
    *state = arp_cache[index].state;
    
    return 0;
}

int net::Arp::queue_packet(uint32_t ip, net::Netbuf *buf) {
    if (!buf) {
        return -1;
    }
    
    // 队列持有自己的副本：调用者发送后照常释放 buf，
    // 不会和 ARP 解析完成后的发送/释放打架。
    net::Netbuf *copy = net::Netbuf::clone(buf);
    if (!copy) {
        return -1;
    }
    
    net::Netbuf *dropped = NULL;
    {
        sync::SpinlockIrqGuard guard(arp_cache_lock);
        
        arp_entry_t *entry = arp_cache_find(ip);
        
        if (!entry || entry->state != ARP_STATE_PENDING) {
            dropped = copy;
            copy = NULL;
        } else {
            // 队列有上限：满了就丢弃最旧的数据包
            if (entry->pending_count >= ARP_PENDING_MAX && entry->pending_queue) {
                dropped = entry->pending_queue;
                entry->pending_queue = dropped->next;
                dropped->next = NULL;
                entry->pending_count--;
            }
            
            // 添加到等待队列尾部
            copy->next = NULL;
            if (!entry->pending_queue) {
                entry->pending_queue = copy;
            } else {
                net::Netbuf *tail = entry->pending_queue;
                while (tail->next) {
                    tail = tail->next;
                }
                tail->next = copy;
            }
            entry->pending_count++;
        }
    }
    
    net::Netbuf::free(dropped);
    return copy ? 0 : -1;
}
