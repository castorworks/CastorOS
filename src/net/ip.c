/**
 * @file ip.c
 * @brief IPv4 协议实现
 */

#include <net/ip.h>
#include <net/ethernet.h>
#include <net/arp.h>
#include <net/netdev.h>
#include <net/netbuf.h>
#include <net/checksum.h>
#include <kernel/sync/spinlock.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <drivers/timer.h>

// IP 标识计数器
static uint16_t ip_id_counter = 0;

// IP 分片重组表
static ip_reassembly_t reass_table[IP_REASS_MAX_ENTRIES];

// 路由表
static ip_route_t route_table[IP_ROUTE_MAX];
static spinlock_t route_lock;  // 静态变量自动初始化为 0（未锁定状态）

// 前向声明上层协议处理函数
extern void icmp_input(netdev_t *dev, netbuf_t *buf, uint32_t src_ip);
extern void udp_input(netdev_t *dev, netbuf_t *buf, uint32_t src_ip, uint32_t dst_ip);
extern void tcp_input(netdev_t *dev, netbuf_t *buf, uint32_t src_ip, uint32_t dst_ip);

// ============================================================================
// IP 分片重组
// ============================================================================

/**
 * @brief 查找或创建重组条目
 */
static ip_reassembly_t *ip_reass_find(uint32_t src, uint32_t dst, 
                                       uint16_t id, uint8_t proto) {
    // 查找现有条目
    for (int i = 0; i < IP_REASS_MAX_ENTRIES; i++) {
        ip_reassembly_t *r = &reass_table[i];
        if (r->valid && r->src_ip == src && r->dst_ip == dst &&
            r->id == id && r->protocol == proto) {
            return r;
        }
    }
    
    // 创建新条目
    for (int i = 0; i < IP_REASS_MAX_ENTRIES; i++) {
        ip_reassembly_t *r = &reass_table[i];
        if (!r->valid) {
            memset(r, 0, sizeof(ip_reassembly_t));
            r->src_ip = src;
            r->dst_ip = dst;
            r->id = id;
            r->protocol = proto;
            r->timeout = (uint32_t)timer_get_uptime_ms() + IP_REASS_TIMEOUT;
            r->valid = true;
            return r;
        }
    }
    
    // 表满，尝试替换最旧的（超时的）条目
    uint32_t now = (uint32_t)timer_get_uptime_ms();
    for (int i = 0; i < IP_REASS_MAX_ENTRIES; i++) {
        ip_reassembly_t *r = &reass_table[i];
        if (r->valid && now >= r->timeout) {
            // 释放旧条目的分片
            ip_fragment_t *f = r->fragments;
            while (f) {
                ip_fragment_t *next = f->next;
                if (f->data) kfree(f->data);
                kfree(f);
                f = next;
            }
            
            // 重用条目
            memset(r, 0, sizeof(ip_reassembly_t));
            r->src_ip = src;
            r->dst_ip = dst;
            r->id = id;
            r->protocol = proto;
            r->timeout = now + IP_REASS_TIMEOUT;
            r->valid = true;
            return r;
        }
    }
    
    return NULL;
}

/**
 * @brief 释放重组条目
 */
static void ip_reass_free(ip_reassembly_t *r) {
    if (!r) return;
    
    ip_fragment_t *f = r->fragments;
    while (f) {
        ip_fragment_t *next = f->next;
        if (f->data) kfree(f->data);
        kfree(f);
        f = next;
    }
    
    r->fragments = NULL;
    r->valid = false;
}

/**
 * @brief 添加分片
 */
static int ip_reass_add_fragment(ip_reassembly_t *r, uint16_t offset, 
                                  uint8_t *data, uint16_t len, bool more_frags) {
    // 检查是否为最后一个分片
    if (!more_frags) {
        r->total_len = offset + len;
    }
    
    // 分配分片结构
    ip_fragment_t *frag = (ip_fragment_t *)kmalloc(sizeof(ip_fragment_t));
    if (!frag) return -1;
    
    frag->offset = offset;
    frag->len = len;
    frag->data = (uint8_t *)kmalloc(len);
    if (!frag->data) {
        kfree(frag);
        return -1;
    }
    memcpy(frag->data, data, len);
    
    // 按偏移插入链表
    ip_fragment_t **pp = &r->fragments;
    while (*pp && (*pp)->offset < offset) {
        pp = &(*pp)->next;
    }
    
    // 检查重叠（简化处理：有完全重复的则丢弃）
    if (*pp && (*pp)->offset == offset && (*pp)->len == len) {
        kfree(frag->data);
        kfree(frag);
        return 0;  // 重复分片
    }
    
    frag->next = *pp;
    *pp = frag;
    
    r->received_len += len;
    
    return 0;
}

/**
 * @brief 检查并重组完整数据包
 */
static netbuf_t *ip_reass_complete(ip_reassembly_t *r, netdev_t *dev, uint8_t protocol) {
    // 检查是否知道总长度
    if (r->total_len == 0) return NULL;
    
    // 检查是否有空洞
    uint16_t expected_offset = 0;
    for (ip_fragment_t *f = r->fragments; f != NULL; f = f->next) {
        if (f->offset != expected_offset) {
            return NULL;  // 有空洞
        }
        expected_offset += f->len;
    }
    
    if (expected_offset != r->total_len) {
        return NULL;  // 不完整
    }
    
    // 分配缓冲区并重组
    netbuf_t *buf = netbuf_alloc(r->total_len);
    if (!buf) return NULL;
    
    uint8_t *dest = netbuf_put(buf, r->total_len);
    for (ip_fragment_t *f = r->fragments; f != NULL; f = f->next) {
        memcpy(dest + f->offset, f->data, f->len);
    }
    
    buf->dev = dev;
    
    LOG_DEBUG_MSG("ip: Reassembled packet id=%u, len=%u, proto=%u\n",
                  r->id, r->total_len, protocol);
    
    // 清理条目
    ip_reass_free(r);
    
    return buf;
}

/**
 * @brief 处理 IP 分片
 */
static netbuf_t *ip_reassemble(netdev_t *dev, netbuf_t *buf, ip_header_t *ip) {
    uint16_t flags_frag = ntohs(ip->flags_fragment);
    uint16_t offset = (flags_frag & IP_FRAG_OFFSET_MASK) * 8;
    bool more_frags = (flags_frag & IP_FLAG_MF) != 0;
    
    // 查找或创建重组条目
    ip_reassembly_t *r = ip_reass_find(ip->src_addr, ip->dst_addr,
                                        ntohs(ip->identification), ip->protocol);
    if (!r) {
        LOG_WARN_MSG("ip: No reassembly entry available\n");
        netbuf_free(buf);
        return NULL;
    }
    
    // 添加分片
    uint8_t hdr_len = ip_header_len(ip);
    uint8_t *data = (uint8_t *)ip + hdr_len;
    uint16_t data_len = ntohs(ip->total_length) - hdr_len;
    
    if (ip_reass_add_fragment(r, offset, data, data_len, more_frags) < 0) {
        netbuf_free(buf);
        return NULL;
    }
    
    netbuf_free(buf);
    
    // 尝试重组
    return ip_reass_complete(r, dev, ip->protocol);
}

/**
 * @brief IP 分片重组定时器
 */
void ip_reass_timer(void) {
    uint32_t now = (uint32_t)timer_get_uptime_ms();
    
    for (int i = 0; i < IP_REASS_MAX_ENTRIES; i++) {
        ip_reassembly_t *r = &reass_table[i];
        if (r->valid && now >= r->timeout) {
            LOG_DEBUG_MSG("ip: Reassembly timeout for id=%u\n", r->id);
            ip_reass_free(r);
        }
    }
}

void ip_init(void) {
    ip_id_counter = 0;
    
    // 初始化重组表
    memset(reass_table, 0, sizeof(reass_table));
    
    // 初始化路由表
    memset(route_table, 0, sizeof(route_table));
    
    LOG_INFO_MSG("ip: IPv4 protocol initialized\n");
}

// ============================================================================
// 路由表
// ============================================================================

netdev_t *ip_route_lookup(uint32_t dst_ip, uint32_t *next_hop) {
    netdev_t *best_dev = NULL;
    uint32_t best_mask = 0;
    uint32_t best_gateway = 0;
    uint32_t best_metric = 0xFFFFFFFF;
    
    // 查找最长前缀匹配的路由
    for (int i = 0; i < IP_ROUTE_MAX; i++) {
        ip_route_t *r = &route_table[i];
        if (!r->valid) continue;
        
        // 检查是否匹配
        if ((dst_ip & r->netmask) == r->dest) {
            // 最长前缀匹配（掩码更长的优先）
            // 如果掩码相同，选择度量值更小的
            uint32_t mask_len = 0;
            uint32_t mask = r->netmask;
            while (mask) {
                mask_len += (mask & 1);
                mask >>= 1;
            }
            
            bool better = false;
            if (best_dev == NULL) {
                better = true;
            } else {
                uint32_t best_mask_len = 0;
                uint32_t bm = best_mask;
                while (bm) {
                    best_mask_len += (bm & 1);
                    bm >>= 1;
                }
                
                if (mask_len > best_mask_len) {
                    better = true;
                } else if (mask_len == best_mask_len && r->metric < best_metric) {
                    better = true;
                }
            }
            
            if (better) {
                best_dev = r->dev;
                best_mask = r->netmask;
                best_gateway = r->gateway;
                best_metric = r->metric;
            }
        }
    }
    
    // 如果找到了路由，设置下一跳
    if (best_dev && next_hop) {
        if (best_gateway != 0) {
            *next_hop = best_gateway;
        } else {
            *next_hop = dst_ip;  // 直连路由，下一跳就是目的地址
        }
    }
    
    // 如果没有找到路由，使用默认设备
    if (!best_dev) {
        best_dev = netdev_get_default();
        if (best_dev && next_hop) {
            // 使用设备的网关作为下一跳
            if (ip_same_subnet(best_dev->ip_addr, dst_ip, best_dev->netmask)) {
                *next_hop = dst_ip;
            } else if (best_dev->gateway != 0) {
                *next_hop = best_dev->gateway;
            } else {
                *next_hop = dst_ip;
            }
        }
    }
    
    return best_dev;
}

int ip_route_add(uint32_t dest, uint32_t netmask, uint32_t gateway, 
                 netdev_t *dev, uint32_t metric) {
    // 查找空闲条目或相同路由
    for (int i = 0; i < IP_ROUTE_MAX; i++) {
        ip_route_t *r = &route_table[i];
        
        // 检查是否已存在相同路由
        if (r->valid && r->dest == dest && r->netmask == netmask) {
            // 更新现有路由
            r->gateway = gateway;
            r->dev = dev;
            r->metric = metric;
            return 0;
        }
    }
    
    // 查找空闲条目
    for (int i = 0; i < IP_ROUTE_MAX; i++) {
        ip_route_t *r = &route_table[i];
        if (!r->valid) {
            r->dest = dest;
            r->netmask = netmask;
            r->gateway = gateway;
            r->dev = dev;
            r->metric = metric;
            r->valid = true;
            return 0;
        }
    }
    
    return -1;  // 路由表满
}

int ip_route_del(uint32_t dest, uint32_t netmask) {
    for (int i = 0; i < IP_ROUTE_MAX; i++) {
        ip_route_t *r = &route_table[i];
        if (r->valid && r->dest == dest && r->netmask == netmask) {
            r->valid = false;
            return 0;
        }
    }
    return -1;  // 路由不存在
}

int ip_route_dump(char *buf, size_t size) {
    int len = 0;
    bool to_buf = (buf != NULL && size > 0);
    
    #define OUTPUT(fmt, ...) do { \
        if (to_buf) { \
            len += ksnprintf(buf + len, size - (size_t)len, fmt, ##__VA_ARGS__); \
        } else { \
            kprintf(fmt, ##__VA_ARGS__); \
        } \
    } while(0)
    
    bool irq_state;
    spinlock_lock_irqsave(&route_lock, &irq_state);
    
    // 表头
    OUTPUT("Kernel IP Routing Table\n");
    OUTPUT("Destination     Gateway         Netmask         Iface    Metric\n");
    OUTPUT("--------------------------------------------------------------------------------\n");
    
    for (int i = 0; i < IP_ROUTE_MAX; i++) {
        if (to_buf && len >= (int)size - 100) break;
        
        ip_route_t *r = &route_table[i];
        if (!r->valid) continue;
        
        char dest_str[16], gw_str[16], mask_str[16];
        ip_to_str(r->dest, dest_str);
        if (r->gateway == 0) {
            strcpy(gw_str, "*");
        } else {
            ip_to_str(r->gateway, gw_str);
        }
        ip_to_str(r->netmask, mask_str);
        
        OUTPUT("%-15s %-15s %-15s %-8s %u\n",
               dest_str, gw_str, mask_str,
               r->dev ? r->dev->name : "N/A", r->metric);
    }
    
    spinlock_unlock_irqrestore(&route_lock, irq_state);
    
    #undef OUTPUT
    return len;
}

void ip_input(netdev_t *dev, netbuf_t *buf) {
    if (!dev || !buf) {
        return;
    }
    
    // 检查数据包长度
    if (buf->len < IP_HEADER_MIN_LEN) {
        LOG_WARN_MSG("ip: Packet too short (%u bytes)\n", buf->len);
        netbuf_free(buf);
        return;
    }
    
    ip_header_t *ip = (ip_header_t *)buf->data;
    buf->network_header = ip;
    
    // 验证 IP 版本
    if (ip_version(ip) != IP_VERSION_4) {
        LOG_WARN_MSG("ip: Invalid version %u\n", ip_version(ip));
        netbuf_free(buf);
        return;
    }
    
    // 获取头部长度
    uint8_t hdr_len = ip_header_len(ip);
    if (hdr_len < IP_HEADER_MIN_LEN || hdr_len > buf->len) {
        LOG_WARN_MSG("ip: Invalid header length %u\n", hdr_len);
        netbuf_free(buf);
        return;
    }
    
    // 验证校验和
    if (ip_checksum(ip, hdr_len) != 0) {
        LOG_WARN_MSG("ip: Invalid checksum\n");
        netbuf_free(buf);
        return;
    }
    
    // 获取总长度
    uint16_t total_len = ntohs(ip->total_length);
    if (total_len < hdr_len || total_len > buf->len) {
        LOG_WARN_MSG("ip: Invalid total length %u\n", total_len);
        netbuf_free(buf);
        return;
    }
    
    // 检查目的 IP 地址
    // 接受：本机 IP、广播地址、多播地址
    uint32_t dst = ip->dst_addr;
    bool is_for_us = (dst == dev->ip_addr) ||
                     (dst == 0xFFFFFFFF) ||  // 全网广播
                     ((dst & ~dev->netmask) == ~dev->netmask);  // 定向广播
    
    if (!is_for_us) {
        // 不是发给我们的，丢弃（不转发）
        netbuf_free(buf);
        return;
    }
    
    // 检查分片
    uint16_t flags_frag = ntohs(ip->flags_fragment);
    uint8_t protocol = ip->protocol;
    uint32_t src_ip = ip->src_addr;
    uint32_t dst_ip = ip->dst_addr;
    
    if ((flags_frag & IP_FLAG_MF) || (flags_frag & IP_FRAG_OFFSET_MASK)) {
        // 分片包，进行重组
        buf = ip_reassemble(dev, buf, ip);
        if (!buf) {
            // 重组未完成或失败，等待更多分片
            return;
        }
        // 重组完成，buf 现在包含完整的上层协议数据
        buf->transport_header = buf->data;
    } else {
        // 非分片包，剥离 IP 头部
        netbuf_pull(buf, hdr_len);
        buf->transport_header = buf->data;
    }
    
    // 根据协议分发到上层处理
    switch (protocol) {
        case IP_PROTO_ICMP:
            icmp_input(dev, buf, src_ip);
            break;
            
        case IP_PROTO_UDP:
            udp_input(dev, buf, src_ip, dst_ip);
            break;
            
        case IP_PROTO_TCP:
            tcp_input(dev, buf, src_ip, dst_ip);
            break;
            
        default:
            LOG_DEBUG_MSG("ip: Unknown protocol %u\n", protocol);
            netbuf_free(buf);
            break;
    }
}

int ip_output(netdev_t *dev, netbuf_t *buf, uint32_t dst_ip, uint8_t protocol) {
    if (!buf) {
        return -1;
    }
    
    uint32_t next_hop = dst_ip;
    
    // 如果未指定设备，使用路由表查找
    if (!dev) {
        dev = ip_route_lookup(dst_ip, &next_hop);
        if (!dev) {
            LOG_ERROR_MSG("ip: No route to host\n");
            return -1;
        }
    } else {
        // 使用指定设备，但仍需确定下一跳
        next_hop = ip_get_next_hop(dev, dst_ip);
    }
    
    // 检查设备是否有 IP 地址
    if (dev->ip_addr == 0) {
        LOG_ERROR_MSG("ip: Device %s has no IP address\n", dev->name);
        return -1;
    }
    
    // 添加 IP 头部空间
    uint8_t *header_ptr = netbuf_push(buf, IP_HEADER_MIN_LEN);
    if (!header_ptr) {
        LOG_ERROR_MSG("ip: No headroom for IP header\n");
        return -1;
    }
    
    // 填充 IP 头部
    ip_header_t *ip = (ip_header_t *)buf->data;
    buf->network_header = ip;
    
    ip->version_ihl = (IP_VERSION_4 << 4) | (IP_HEADER_MIN_LEN / 4);
    ip->tos = 0;
    ip->total_length = htons(buf->len);
    uint16_t id = ip_id_counter++;
    ip->identification = htons(id);
    ip->flags_fragment = htons(IP_FLAG_DF);  // Don't Fragment
    ip->ttl = IP_DEFAULT_TTL;
    ip->protocol = protocol;
    ip->checksum = 0;  // 先设为 0
    ip->src_addr = dev->ip_addr;
    ip->dst_addr = dst_ip;
    
    // 计算校验和
    ip->checksum = ip_checksum(ip, IP_HEADER_MIN_LEN);
    
    // 解析下一跳的 MAC 地址
    uint8_t dst_mac[6];
    int ret = arp_resolve(dev, next_hop, dst_mac);
    
    if (ret == 0) {
        // ARP 解析成功，发送
        return ethernet_output(dev, buf, dst_mac, ETH_TYPE_IP);
    } else if (ret == -1) {
        // 正在 ARP 解析中，将数据包加入等待队列
        if (arp_queue_packet(next_hop, buf) == 0) {
            return 0;  // 返回成功，数据包会在 ARP 解析完成后发送
        } else {
            // 队列失败，可能 ARP 已经解析完成（竞态条件），重试一次
            ret = arp_cache_lookup(next_hop, dst_mac);
            if (ret == 0) {
                // ARP 已解析，直接发送
                return ethernet_output(dev, buf, dst_mac, ETH_TYPE_IP);
            }
            LOG_WARN_MSG("ip: Failed to queue packet for ARP resolution\n");
            return -1;  // 调用者需要释放 buf
        }
    } else {
        // ARP 解析失败
        LOG_ERROR_MSG("ip: ARP resolution failed for %u.%u.%u.%u\n",
                     (next_hop) & 0xFF,
                     (next_hop >> 8) & 0xFF,
                     (next_hop >> 16) & 0xFF,
                     (next_hop >> 24) & 0xFF);
        return -1;
    }
}

uint16_t ip_checksum(void *header, int len) {
    return checksum(header, len);
}

char *ip_to_str(uint32_t ip, char *buf) {
    uint8_t *bytes = (uint8_t *)&ip;
    snprintf(buf, 16, "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
    return buf;
}

int str_to_ip(const char *str, uint32_t *ip) {
    if (!str || !ip) {
        return -1;
    }
    
    uint32_t a, b, c, d;
    int count = 0;
    const char *p = str;
    
    // 解析第一个数字
    a = 0;
    while (*p >= '0' && *p <= '9') {
        a = a * 10 + (*p - '0');
        p++;
        count++;
    }
    if (*p != '.' || a > 255 || count == 0) return -1;
    p++;
    
    // 解析第二个数字
    b = 0;
    count = 0;
    while (*p >= '0' && *p <= '9') {
        b = b * 10 + (*p - '0');
        p++;
        count++;
    }
    if (*p != '.' || b > 255 || count == 0) return -1;
    p++;
    
    // 解析第三个数字
    c = 0;
    count = 0;
    while (*p >= '0' && *p <= '9') {
        c = c * 10 + (*p - '0');
        p++;
        count++;
    }
    if (*p != '.' || c > 255 || count == 0) return -1;
    p++;
    
    // 解析第四个数字
    d = 0;
    count = 0;
    while (*p >= '0' && *p <= '9') {
        d = d * 10 + (*p - '0');
        p++;
        count++;
    }
    if (d > 255 || count == 0) return -1;
    
    // 检查字符串是否结束
    if (*p != '\0') return -1;
    
    // 构造 IP 地址（网络字节序）
    *ip = IP_ADDR(a, b, c, d);
    
    return 0;
}

bool ip_same_subnet(uint32_t ip1, uint32_t ip2, uint32_t netmask) {
    return (ip1 & netmask) == (ip2 & netmask);
}

uint32_t ip_get_next_hop(netdev_t *dev, uint32_t dst_ip) {
    if (!dev) {
        return dst_ip;
    }
    
    // 如果目的 IP 在同一子网，直接发送
    if (ip_same_subnet(dev->ip_addr, dst_ip, dev->netmask)) {
        return dst_ip;
    }
    
    // 否则发送到网关
    if (dev->gateway != 0) {
        return dev->gateway;
    }
    
    // 没有网关，尝试直接发送
    return dst_ip;
}

