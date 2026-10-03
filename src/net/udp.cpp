/**
 * @file udp.c
 * @brief UDP 协议实现
 */

#include <net/udp.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/netdev.h>
#include <net/netbuf.h>
#include <net/checksum.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <kernel/sync/spinlock.h>

// UDP PCB 链表
static udp_pcb_t *udp_pcbs = NULL;
static sync::Spinlock udp_lock;

// 临时端口分配范围
#define UDP_EPHEMERAL_PORT_MIN  49152
#define UDP_EPHEMERAL_PORT_MAX  65535
static uint32_t next_ephemeral_port = UDP_EPHEMERAL_PORT_MIN;

/**
 * @brief 查找匹配的 UDP PCB
 */
static udp_pcb_t *udp_find_pcb(uint32_t local_ip, uint16_t local_port,
                               uint32_t remote_ip, uint16_t remote_port) {
    udp_pcb_t *pcb;
    udp_pcb_t *best_match = NULL;
    int best_score = -1;
    
    for (pcb = udp_pcbs; pcb != NULL; pcb = pcb->next) {
        int score = 0;
        
        // 检查本地端口（必须匹配）
        if (pcb->local_port != local_port) {
            continue;
        }
        
        // 检查本地 IP
        if (pcb->local_ip != 0) {
            if (pcb->local_ip != local_ip) {
                continue;
            }
            score += 1;
        }
        
        // 检查远程端口
        if (pcb->remote_port != 0) {
            if (pcb->remote_port != remote_port) {
                continue;
            }
            score += 2;
        }
        
        // 检查远程 IP
        if (pcb->remote_ip != 0) {
            if (pcb->remote_ip != remote_ip) {
                continue;
            }
            score += 4;
        }
        
        // 选择最佳匹配
        if (score > best_score) {
            best_score = score;
            best_match = pcb;
        }
    }
    
    return best_match;
}

void net::Udp::init() {
    udp_lock.init();
    udp_pcbs = NULL;
    next_ephemeral_port = UDP_EPHEMERAL_PORT_MIN;
    
    LOG_INFO_MSG("udp: UDP protocol initialized\n");
}

void net::Udp::input(net::Netdev *dev, net::Netbuf *buf, uint32_t src_ip, uint32_t dst_ip) {
    if (!dev || !buf) {
        return;
    }
    
    // 检查报文长度
    if (buf->len < UDP_HEADER_LEN) {
        LOG_WARN_MSG("udp: Packet too short (%u bytes)\n", buf->len);
        net::Netbuf::free(buf);
        return;
    }
    
    udp_header_t *udp = (udp_header_t *)buf->data;
    buf->transport_header = udp;
    
    uint16_t src_port = ntohs(udp->src_port);
    uint16_t dst_port = ntohs(udp->dst_port);
    uint16_t udp_len = ntohs(udp->length);
    
    // 验证长度
    if (udp_len < UDP_HEADER_LEN || udp_len > buf->len) {
        LOG_WARN_MSG("udp: Invalid length %u\n", udp_len);
        net::Netbuf::free(buf);
        return;
    }
    
    // 验证校验和（如果非零）
    if (udp->checksum != 0) {
        uint16_t orig_checksum = udp->checksum;
        udp->checksum = 0;
        uint16_t calc_checksum = net::Udp::checksum(src_ip, dst_ip, udp, udp_len);
        
        if (calc_checksum != orig_checksum) {
            LOG_WARN_MSG("udp: Invalid checksum\n");
            net::Netbuf::free(buf);
            return;
        }
        udp->checksum = orig_checksum;
    }
    
    // 查找匹配的 PCB
    bool irq_state;
    udp_lock.lock_irqsave(irq_state);
    
    udp_pcb_t *pcb = udp_find_pcb(dst_ip, dst_port, src_ip, src_port);
    
    if (pcb) {
        // 剥离 UDP 头部
        net::Netbuf::pull(buf, UDP_HEADER_LEN);
        
        // 保存源地址信息到缓冲区（用于 recvfrom）
        buf->src_ip = src_ip;
        buf->src_port = src_port;
        
        // 调用回调函数
        if (pcb->recv_callback) {
            udp_lock.unlock_irqrestore(irq_state);
            pcb->recv_callback(pcb, buf, src_ip, src_port);
            return;  // 回调函数负责释放 buf
        }
        
        // 如果没有回调，加入接收队列
        buf->next = NULL;
        if (!pcb->recv_queue) {
            pcb->recv_queue = buf;
        } else {
            net::Netbuf *tail = pcb->recv_queue;
            while (tail->next) {
                tail = tail->next;
            }
            tail->next = buf;
        }
        pcb->recv_queue_len++;
        
        udp_lock.unlock_irqrestore(irq_state);
        return;
    }
    
    udp_lock.unlock_irqrestore(irq_state);
    
    // 没有找到匹配的 PCB，发送 ICMP 端口不可达
    LOG_DEBUG_MSG("udp: No PCB for port %u, sending ICMP unreachable\n", dst_port);
    
    // 获取原始 IP 头部（在 buf->network_header 之前）
    ip_header_t *orig_ip = (ip_header_t *)buf->network_header;
    net::Icmp::send_dest_unreachable(src_ip, ICMP_PORT_UNREACHABLE, orig_ip, udp);
    
    net::Netbuf::free(buf);
}

int net::Udp::output(uint16_t src_port, uint32_t dst_ip, uint16_t dst_port,
               uint8_t *data, uint32_t len) {
    net::Netdev *dev = net::Netdev::get_default();
    if (!dev) {
        LOG_ERROR_MSG("udp: No network device available\n");
        return -1;
    }
    
    // 计算 UDP 长度
    uint32_t udp_len = UDP_HEADER_LEN + len;
    
    // 分配缓冲区
    net::Netbuf *buf = net::Netbuf::alloc(udp_len);
    if (!buf) {
        LOG_ERROR_MSG("udp: Failed to allocate buffer\n");
        return -1;
    }
    
    // 填充 UDP 数据报
    uint8_t *pkt = net::Netbuf::put(buf, udp_len);
    udp_header_t *udp = (udp_header_t *)pkt;
    
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length = htons(udp_len);
    udp->checksum = 0;
    
    // 复制数据
    if (data && len > 0) {
        memcpy(pkt + UDP_HEADER_LEN, data, len);
    }
    
    // 计算校验和
    udp->checksum = net::Udp::checksum(dev->ip_addr, dst_ip, udp, udp_len);
    
    // 发送
    int ret = net::Ip::output(dev, buf, dst_ip, IP_PROTO_UDP);
    if (ret < 0) {
        net::Netbuf::free(buf);
    }
    
    return ret;
}

udp_pcb_t *net::Udp::pcb_new() {
    udp_pcb_t *pcb = (udp_pcb_t *)kmalloc(sizeof(udp_pcb_t));
    if (!pcb) {
        return NULL;
    }
    
    memset(pcb, 0, sizeof(udp_pcb_t));
    
    // 添加到链表
    sync::SpinlockIrqGuard guard(udp_lock);
    pcb->next = udp_pcbs;
    udp_pcbs = pcb;
    
    return pcb;
}

void net::Udp::pcb_free(udp_pcb_t *pcb) {
    if (!pcb) {
        return;
    }
    
    {
        sync::SpinlockIrqGuard guard(udp_lock);
        // 从链表移除
        if (udp_pcbs == pcb) {
            udp_pcbs = pcb->next;
        } else {
            udp_pcb_t *prev = udp_pcbs;
            while (prev && prev->next != pcb) {
                prev = prev->next;
            }
            if (prev) {
                prev->next = pcb->next;
            }
        }
    }
    
    // 释放接收队列
    net::Netbuf *buf = pcb->recv_queue;
    while (buf) {
        net::Netbuf *next = buf->next;
        net::Netbuf::free(buf);
        buf = next;
    }
    
    kfree(pcb);
}

int net::Udp::bind(udp_pcb_t *pcb, uint32_t local_ip, uint16_t local_port) {
    if (!pcb) {
        return -1;
    }
    
    bool irq_state;
    udp_lock.lock_irqsave(irq_state);
    
    // 检查端口是否已被使用
    for (udp_pcb_t *p = udp_pcbs; p != NULL; p = p->next) {
        if (p != pcb && p->local_port == local_port) {
            if (p->local_ip == 0 || local_ip == 0 || p->local_ip == local_ip) {
                udp_lock.unlock_irqrestore(irq_state);
                return -1;  // 端口已被使用
            }
        }
    }
    
    pcb->local_ip = local_ip;
    pcb->local_port = local_port;
    
    udp_lock.unlock_irqrestore(irq_state);
    return 0;
}

int net::Udp::connect(udp_pcb_t *pcb, uint32_t remote_ip, uint16_t remote_port) {
    if (!pcb) {
        return -1;
    }
    
    pcb->remote_ip = remote_ip;
    pcb->remote_port = remote_port;
    
    // 如果未绑定本地端口，分配一个临时端口
    if (pcb->local_port == 0) {
        pcb->local_port = net::Udp::alloc_port();
        if (pcb->local_port == 0) {
            return -1;
        }
    }
    
    return 0;
}

void net::Udp::disconnect(udp_pcb_t *pcb) {
    if (pcb) {
        pcb->remote_ip = 0;
        pcb->remote_port = 0;
    }
}

int net::Udp::send(udp_pcb_t *pcb, net::Netbuf *buf) {
    if (!pcb || !buf) {
        return -1;
    }
    
    if (pcb->remote_ip == 0 || pcb->remote_port == 0) {
        return -1;  // 未连接
    }
    
    return net::Udp::sendto(pcb, buf, pcb->remote_ip, pcb->remote_port);
}

int net::Udp::sendto(udp_pcb_t *pcb, net::Netbuf *buf, uint32_t dst_ip, uint16_t dst_port) {
    if (!pcb || !buf) {
        return -1;
    }
    
    net::Netdev *dev = net::Netdev::get_default();
    if (!dev) {
        return -1;
    }
    
    // 如果未绑定本地端口，分配一个临时端口
    if (pcb->local_port == 0) {
        pcb->local_port = net::Udp::alloc_port();
        if (pcb->local_port == 0) {
            return -1;
        }
    }
    
    // 添加 UDP 头部
    uint8_t *header_ptr = net::Netbuf::push(buf, UDP_HEADER_LEN);
    if (!header_ptr) {
        return -1;
    }
    
    udp_header_t *udp = (udp_header_t *)buf->data;
    uint16_t udp_len = buf->len;
    
    udp->src_port = htons(pcb->local_port);
    udp->dst_port = htons(dst_port);
    udp->length = htons(udp_len);
    udp->checksum = 0;
    
    // 计算校验和
    uint32_t src_ip = (pcb->local_ip != 0) ? pcb->local_ip : dev->ip_addr;
    udp->checksum = net::Udp::checksum(src_ip, dst_ip, udp, udp_len);
    
    // 发送
    return net::Ip::output(dev, buf, dst_ip, IP_PROTO_UDP);
}

void net::Udp::recv(udp_pcb_t *pcb,
              void (*callback)(udp_pcb_t *pcb, net::Netbuf *buf,
                              uint32_t src_ip, uint16_t src_port),
              void *arg) {
    if (pcb) {
        pcb->recv_callback = callback;
        pcb->callback_arg = arg;
    }
}

net::Netbuf *net::Udp::recv_poll(udp_pcb_t *pcb) {
    if (!pcb) return NULL;
    
    sync::SpinlockIrqGuard guard(udp_lock);
    
    net::Netbuf *buf = NULL;
    if (pcb->recv_queue) {
        buf = pcb->recv_queue;
        pcb->recv_queue = buf->next;
        pcb->recv_queue_len--;
        buf->next = NULL;  // 断开链表
    }
    
    return buf;
}

bool net::Udp::has_data(udp_pcb_t *pcb) {
    if (!pcb) return false;
    
    sync::SpinlockIrqGuard guard(udp_lock);
    bool has_data = (pcb->recv_queue != NULL);
    
    return has_data;
}

uint16_t net::Udp::checksum(uint32_t src_ip, uint32_t dst_ip, udp_header_t *udp, uint16_t len) {
    uint32_t sum = 0;
    
    // 计算伪首部校验和
    udp_pseudo_header_t pseudo;
    pseudo.src_addr = src_ip;
    pseudo.dst_addr = dst_ip;
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTO_UDP;
    pseudo.udp_length = htons(len);
    
    sum = net::Checksum::partial(sum, &pseudo, sizeof(pseudo));
    
    // 计算 UDP 头部和数据校验和
    sum = net::Checksum::partial(sum, udp, len);
    
    return net::Checksum::finish(sum);
}

uint16_t net::Udp::alloc_port() {
    bool irq_state;
    udp_lock.lock_irqsave(irq_state);
    
    uint16_t start_port = next_ephemeral_port;
    
    do {
        uint16_t port = next_ephemeral_port++;
        if (next_ephemeral_port > UDP_EPHEMERAL_PORT_MAX) {
            next_ephemeral_port = UDP_EPHEMERAL_PORT_MIN;
        }
        
        // 检查端口是否已被使用
        bool in_use = false;
        for (udp_pcb_t *pcb = udp_pcbs; pcb != NULL; pcb = pcb->next) {
            if (pcb->local_port == port) {
                in_use = true;
                break;
            }
        }
        
        if (!in_use) {
            udp_lock.unlock_irqrestore(irq_state);
            return port;
        }
        
    } while (next_ephemeral_port != start_port);
    
    udp_lock.unlock_irqrestore(irq_state);
    return 0;  // 没有可用端口
}

int net::Udp::pcb_list_dump(char *buf, size_t size) {
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
    udp_lock.lock_irqsave(irq_state);
    
    // 表头
    OUTPUT("UDP Endpoints:\n");
    OUTPUT("Proto  Local Address          Remote Address\n");
    OUTPUT("--------------------------------------------------------------------------------\n");
    
    for (udp_pcb_t *pcb = udp_pcbs; pcb != NULL; pcb = pcb->next) {
        if (to_buf && len >= (int)size - 100) break;
        
        char local_ip_str[16], remote_ip_str[16];
        if (pcb->local_ip == 0) {
            strcpy(local_ip_str, "0.0.0.0");
        } else {
            net::Ip::to_str(pcb->local_ip, local_ip_str);
        }
        
        if (pcb->remote_ip == 0 && pcb->remote_port == 0) {
            OUTPUT("udp    %s:%-5u          0.0.0.0:*\n",
                   local_ip_str, pcb->local_port);
        } else {
            if (pcb->remote_ip == 0) {
                strcpy(remote_ip_str, "0.0.0.0");
            } else {
                net::Ip::to_str(pcb->remote_ip, remote_ip_str);
            }
            OUTPUT("udp    %s:%-5u  %s:%-5u\n",
                   local_ip_str, pcb->local_port,
                   remote_ip_str, pcb->remote_port);
        }
    }
    
    udp_lock.unlock_irqrestore(irq_state);
    
    #undef OUTPUT
    return len;
}

