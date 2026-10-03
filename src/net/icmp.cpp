/**
 * @file icmp.c
 * @brief ICMP 协议实现
 */

#include <net/icmp.h>
#include <net/ip.h>
#include <net/netdev.h>
#include <net/netbuf.h>
#include <net/checksum.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <drivers/timer.h>

// Ping 回调函数
static ping_callback_t ping_callback = NULL;

// 最后发送的 ping 信息（用于计算 RTT）
static struct {
    uint16_t id;
    uint16_t seq;
    uint32_t send_time;
    bool     waiting;
} last_ping = {0, 0, 0, false};

// 最后的 RTT
static int32_t last_rtt = -1;

void icmp_init(void) {
    ping_callback = NULL;
    last_ping.waiting = false;
    last_rtt = -1;
    
    LOG_INFO_MSG("icmp: ICMP protocol initialized\n");
}

void icmp_input(netdev_t *dev, netbuf_t *buf, uint32_t src_ip) {
    if (!dev || !buf) {
        return;
    }
    
    // 检查报文长度
    if (buf->len < sizeof(icmp_header_t)) {
        LOG_WARN_MSG("icmp: Packet too short (%u bytes)\n", buf->len);
        netbuf_free(buf);
        return;
    }
    
    icmp_header_t *icmp = (icmp_header_t *)buf->data;
    
    // 验证校验和
    if (checksum(icmp, buf->len) != 0) {
        LOG_WARN_MSG("icmp: Invalid checksum\n");
        netbuf_free(buf);
        return;
    }
    
    switch (icmp->type) {
        case ICMP_ECHO_REQUEST: {
            // 收到 ping 请求，发送 ping 响应
            LOG_DEBUG_MSG("icmp: Received Echo Request from %u.%u.%u.%u\n",
                         (src_ip) & 0xFF,
                         (src_ip >> 8) & 0xFF,
                         (src_ip >> 16) & 0xFF,
                         (src_ip >> 24) & 0xFF);
            
            // 提取 Echo 数据
            uint16_t id = ntohs(icmp->un.echo.id);
            uint16_t seq = ntohs(icmp->un.echo.sequence);
            uint32_t data_len = buf->len - sizeof(icmp_header_t);
            uint8_t *data = (data_len > 0) ? (uint8_t *)(icmp + 1) : NULL;
            
            icmp_send_echo_reply(src_ip, id, seq, data, data_len);
            break;
        }
        
        case ICMP_ECHO_REPLY: {
            // 收到 ping 响应
            uint16_t id = ntohs(icmp->un.echo.id);
            uint16_t seq = ntohs(icmp->un.echo.sequence);
            
            LOG_DEBUG_MSG("icmp: Received Echo Reply from %u.%u.%u.%u (id=%u, seq=%u)\n",
                         (src_ip) & 0xFF,
                         (src_ip >> 8) & 0xFF,
                         (src_ip >> 16) & 0xFF,
                         (src_ip >> 24) & 0xFF,
                         id, seq);
            
            // 计算 RTT
            if (last_ping.waiting && last_ping.id == id && last_ping.seq == seq) {
                uint32_t now = (uint32_t)timer_get_uptime_ms();
                uint32_t rtt = now - last_ping.send_time;
                last_rtt = rtt;
                last_ping.waiting = false;
                
                // 调用回调函数
                if (ping_callback) {
                    ping_callback(src_ip, seq, rtt, true);
                }
            }
            break;
        }
        
        case ICMP_DEST_UNREACHABLE: {
            const char *reason = "Unknown";
            switch (icmp->code) {
                case ICMP_NET_UNREACHABLE:   reason = "Network unreachable"; break;
                case ICMP_HOST_UNREACHABLE:  reason = "Host unreachable"; break;
                case ICMP_PROTO_UNREACHABLE: reason = "Protocol unreachable"; break;
                case ICMP_PORT_UNREACHABLE:  reason = "Port unreachable"; break;
                case ICMP_FRAG_NEEDED:       reason = "Fragmentation needed"; break;
            }
            LOG_WARN_MSG("icmp: Destination unreachable: %s\n", reason);
            break;
        }
        
        case ICMP_TIME_EXCEEDED: {
            LOG_WARN_MSG("icmp: Time exceeded (TTL expired)\n");
            break;
        }
        
        default:
            LOG_DEBUG_MSG("icmp: Unknown type %u\n", icmp->type);
            break;
    }
    
    netbuf_free(buf);
}

int icmp_send_echo_request(uint32_t dst_ip, uint16_t id, uint16_t seq,
                           uint8_t *data, uint32_t len) {
    // 计算总长度
    uint32_t icmp_len = sizeof(icmp_header_t) + len;
    
    // 分配缓冲区
    netbuf_t *buf = netbuf_alloc(icmp_len);
    if (!buf) {
        LOG_ERROR_MSG("icmp: Failed to allocate buffer\n");
        return -1;
    }
    
    // 填充 ICMP 报文
    uint8_t *pkt = netbuf_put(buf, icmp_len);
    icmp_header_t *icmp = (icmp_header_t *)pkt;
    
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->un.echo.id = htons(id);
    icmp->un.echo.sequence = htons(seq);
    
    // 复制数据
    if (data && len > 0) {
        memcpy(pkt + sizeof(icmp_header_t), data, len);
    }
    
    // 计算校验和
    icmp->checksum = checksum(icmp, icmp_len);
    
    // 记录发送时间（用于计算 RTT）
    last_ping.id = id;
    last_ping.seq = seq;
    last_ping.send_time = (uint32_t)timer_get_uptime_ms();
    last_ping.waiting = true;
    last_rtt = -1;  // 重置 RTT，等待新的回复
    
    // 发送
    int ret = ip_output(NULL, buf, dst_ip, IP_PROTO_ICMP);
    if (ret < 0) {
        netbuf_free(buf);
        last_ping.waiting = false;
    }
    
    return ret;
}

int icmp_send_echo_reply(uint32_t dst_ip, uint16_t id, uint16_t seq,
                         uint8_t *data, uint32_t len) {
    // 计算总长度
    uint32_t icmp_len = sizeof(icmp_header_t) + len;
    
    // 分配缓冲区
    netbuf_t *buf = netbuf_alloc(icmp_len);
    if (!buf) {
        LOG_ERROR_MSG("icmp: Failed to allocate buffer\n");
        return -1;
    }
    
    // 填充 ICMP 报文
    uint8_t *pkt = netbuf_put(buf, icmp_len);
    icmp_header_t *icmp = (icmp_header_t *)pkt;
    
    icmp->type = ICMP_ECHO_REPLY;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->un.echo.id = htons(id);
    icmp->un.echo.sequence = htons(seq);
    
    // 复制数据
    if (data && len > 0) {
        memcpy(pkt + sizeof(icmp_header_t), data, len);
    }
    
    // 计算校验和
    icmp->checksum = checksum(icmp, icmp_len);
    
    // 发送
    int ret = ip_output(NULL, buf, dst_ip, IP_PROTO_ICMP);
    if (ret < 0) {
        netbuf_free(buf);
    }
    
    return ret;
}

int icmp_send_dest_unreachable(uint32_t dst_ip, uint8_t code,
                               void *orig_header, void *orig_data) {
    // ICMP 目的不可达消息包含原始 IP 头部 + 8 字节原始数据
    uint32_t orig_len = IP_HEADER_MIN_LEN + 8;
    uint32_t icmp_len = sizeof(icmp_header_t) + orig_len;
    
    // 分配缓冲区
    netbuf_t *buf = netbuf_alloc(icmp_len);
    if (!buf) {
        LOG_ERROR_MSG("icmp: Failed to allocate buffer\n");
        return -1;
    }
    
    // 填充 ICMP 报文
    uint8_t *pkt = netbuf_put(buf, icmp_len);
    icmp_header_t *icmp = (icmp_header_t *)pkt;
    
    icmp->type = ICMP_DEST_UNREACHABLE;
    icmp->code = code;
    icmp->checksum = 0;
    icmp->un.unused = 0;
    
    // 复制原始 IP 头部 + 8 字节数据
    uint8_t *payload = pkt + sizeof(icmp_header_t);
    if (orig_header) {
        memcpy(payload, orig_header, IP_HEADER_MIN_LEN);
    }
    if (orig_data) {
        memcpy(payload + IP_HEADER_MIN_LEN, orig_data, 8);
    }
    
    // 计算校验和
    icmp->checksum = checksum(icmp, icmp_len);
    
    // 发送
    int ret = ip_output(NULL, buf, dst_ip, IP_PROTO_ICMP);
    if (ret < 0) {
        netbuf_free(buf);
    }
    
    return ret;
}

void icmp_register_ping_callback(ping_callback_t callback) {
    ping_callback = callback;
}

int32_t icmp_get_last_rtt(void) {
    return last_rtt;
}

