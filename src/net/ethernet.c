/**
 * @file ethernet.c
 * @brief 以太网帧处理实现
 */

#include <net/ethernet.h>
#include <net/netdev.h>
#include <net/netbuf.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <lib/kprintf.h>

// 广播 MAC 地址 (FF:FF:FF:FF:FF:FF)
const uint8_t ETH_BROADCAST_ADDR[ETH_ADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// 零 MAC 地址
const uint8_t ETH_ZERO_ADDR[ETH_ADDR_LEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// 前向声明上层协议处理函数
extern void arp_input(netdev_t *dev, netbuf_t *buf);
extern void ip_input(netdev_t *dev, netbuf_t *buf);

// 字节序转换（小端到大端）
static inline uint16_t eth_ntohs(uint16_t n) {
    return ((n & 0xFF) << 8) | ((n >> 8) & 0xFF);
}

static inline uint16_t eth_htons(uint16_t h) {
    return eth_ntohs(h);
}

void ethernet_init(void) {
    LOG_INFO_MSG("ethernet: Ethernet layer initialized\n");
}

void ethernet_input(netdev_t *dev, netbuf_t *buf) {
    if (!dev || !buf) {
        return;
    }
    
    // 检查帧长度是否足够
    if (buf->len < ETH_HEADER_LEN) {
        LOG_WARN_MSG("ethernet: Frame too short (%u bytes)\n", buf->len);
        netbuf_free(buf);
        return;
    }
    
    // 获取以太网头部
    eth_header_t *eth = (eth_header_t *)buf->data;
    buf->mac_header = eth;
    
    // 检查目的 MAC 地址
    // 接受：广播地址、多播地址、本机地址
    if (!mac_addr_is_broadcast(eth->dst) &&
        !mac_addr_is_multicast(eth->dst) &&
        mac_addr_cmp(eth->dst, dev->mac) != 0) {
        // 不是发给我们的帧，丢弃
        netbuf_free(buf);
        return;
    }
    
    // 获取 EtherType
    uint16_t type = eth_ntohs(eth->type);
    
    // 剥离以太网头部，将数据指针移到上层协议数据
    netbuf_pull(buf, ETH_HEADER_LEN);
    buf->network_header = buf->data;
    
    // 根据 EtherType 分发到对应的协议处理函数
    switch (type) {
        case ETH_TYPE_ARP:
            arp_input(dev, buf);
            break;
            
        case ETH_TYPE_IP:
            ip_input(dev, buf);
            break;
            
        case ETH_TYPE_IPV6:
            // IPv6 暂不支持
            LOG_DEBUG_MSG("ethernet: IPv6 not supported\n");
            netbuf_free(buf);
            break;
            
        default:
            LOG_DEBUG_MSG("ethernet: Unknown EtherType 0x%04x\n", type);
            netbuf_free(buf);
            break;
    }
}

int ethernet_output(netdev_t *dev, netbuf_t *buf, const uint8_t *dst_mac, uint16_t type) {
    if (!dev || !buf || !dst_mac) {
        return -1;
    }
    
    // 在数据前添加以太网头部空间
    uint8_t *header_ptr = netbuf_push(buf, ETH_HEADER_LEN);
    if (!header_ptr) {
        LOG_ERROR_MSG("ethernet: No headroom for Ethernet header\n");
        return -1;
    }
    
    // 填充以太网头部
    eth_header_t *eth = (eth_header_t *)buf->data;
    mac_addr_copy(eth->dst, dst_mac);
    mac_addr_copy(eth->src, dev->mac);
    eth->type = eth_htons(type);
    
    buf->mac_header = eth;
    
    // 确保帧至少达到最小长度（不含 FCS）
    // 注：实际的填充通常由网卡硬件完成
    
    // 发送帧
    return netdev_transmit(dev, buf);
}

int mac_addr_cmp(const uint8_t *a, const uint8_t *b) {
    return memcmp(a, b, ETH_ADDR_LEN);
}

void mac_addr_copy(uint8_t *dst, const uint8_t *src) {
    memcpy(dst, src, ETH_ADDR_LEN);
}

bool mac_addr_is_broadcast(const uint8_t *addr) {
    return mac_addr_cmp(addr, ETH_BROADCAST_ADDR) == 0;
}

bool mac_addr_is_multicast(const uint8_t *addr) {
    // 多播地址的第一个字节最低位为 1
    return (addr[0] & 0x01) != 0;
}

bool mac_addr_is_zero(const uint8_t *addr) {
    return mac_addr_cmp(addr, ETH_ZERO_ADDR) == 0;
}

char *mac_to_str(const uint8_t *mac, char *buf) {
    snprintf(buf, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

