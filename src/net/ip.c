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
#include <lib/string.h>
#include <lib/klog.h>
#include <lib/kprintf.h>

// IP 标识计数器
static uint16_t ip_id_counter = 0;

// 前向声明上层协议处理函数
extern void icmp_input(netdev_t *dev, netbuf_t *buf, uint32_t src_ip);
extern void udp_input(netdev_t *dev, netbuf_t *buf, uint32_t src_ip, uint32_t dst_ip);
extern void tcp_input(netdev_t *dev, netbuf_t *buf, uint32_t src_ip, uint32_t dst_ip);

void ip_init(void) {
    ip_id_counter = 0;
    LOG_INFO_MSG("ip: IPv4 protocol initialized\n");
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
    
    // 检查分片（暂不支持分片重组）
    uint16_t flags_frag = ntohs(ip->flags_fragment);
    if ((flags_frag & IP_FLAG_MF) || (flags_frag & IP_FRAG_OFFSET_MASK)) {
        LOG_WARN_MSG("ip: Fragmented packets not supported\n");
        netbuf_free(buf);
        return;
    }
    
    // 剥离 IP 头部
    netbuf_pull(buf, hdr_len);
    buf->transport_header = buf->data;
    
    // 根据协议分发到上层处理
    uint32_t src_ip = ip->src_addr;
    uint32_t dst_ip = ip->dst_addr;
    
    switch (ip->protocol) {
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
            LOG_DEBUG_MSG("ip: Unknown protocol %u\n", ip->protocol);
            netbuf_free(buf);
            break;
    }
}

int ip_output(netdev_t *dev, netbuf_t *buf, uint32_t dst_ip, uint8_t protocol) {
    if (!buf) {
        return -1;
    }
    
    // 如果未指定设备，使用默认设备
    if (!dev) {
        dev = netdev_get_default();
        if (!dev) {
            LOG_ERROR_MSG("ip: No network device available\n");
            return -1;
        }
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
    
    // 获取下一跳 IP 地址
    uint32_t next_hop = ip_get_next_hop(dev, dst_ip);
    
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
            // 队列失败，需要释放 buf 并返回错误
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

