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
static spinlock_t udp_lock;

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

void udp_init(void) {
    spinlock_init(&udp_lock);
    udp_pcbs = NULL;
    next_ephemeral_port = UDP_EPHEMERAL_PORT_MIN;
    
    LOG_INFO_MSG("udp: UDP protocol initialized\n");
}

void udp_input(netdev_t *dev, netbuf_t *buf, uint32_t src_ip, uint32_t dst_ip) {
    if (!dev || !buf) {
        return;
    }
    
    // 检查报文长度
    if (buf->len < UDP_HEADER_LEN) {
        LOG_WARN_MSG("udp: Packet too short (%u bytes)\n", buf->len);
        netbuf_free(buf);
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
        netbuf_free(buf);
        return;
    }
    
    // 验证校验和（如果非零）
    if (udp->checksum != 0) {
        uint16_t orig_checksum = udp->checksum;
        udp->checksum = 0;
        uint16_t calc_checksum = udp_checksum(src_ip, dst_ip, udp, udp_len);
        
        if (calc_checksum != orig_checksum) {
            LOG_WARN_MSG("udp: Invalid checksum\n");
            netbuf_free(buf);
            return;
        }
        udp->checksum = orig_checksum;
    }
    
    // 查找匹配的 PCB
    bool irq_state;
    spinlock_lock_irqsave(&udp_lock, &irq_state);
    
    udp_pcb_t *pcb = udp_find_pcb(dst_ip, dst_port, src_ip, src_port);
    
    if (pcb) {
        // 剥离 UDP 头部
        netbuf_pull(buf, UDP_HEADER_LEN);
        
        // 调用回调函数
        if (pcb->recv_callback) {
            spinlock_unlock_irqrestore(&udp_lock, irq_state);
            pcb->recv_callback(pcb, buf, src_ip, src_port);
            return;  // 回调函数负责释放 buf
        }
        
        // 如果没有回调，加入接收队列
        buf->next = NULL;
        if (!pcb->recv_queue) {
            pcb->recv_queue = buf;
        } else {
            netbuf_t *tail = pcb->recv_queue;
            while (tail->next) {
                tail = tail->next;
            }
            tail->next = buf;
        }
        pcb->recv_queue_len++;
        
        spinlock_unlock_irqrestore(&udp_lock, irq_state);
        return;
    }
    
    spinlock_unlock_irqrestore(&udp_lock, irq_state);
    
    // 没有找到匹配的 PCB，发送 ICMP 端口不可达
    LOG_DEBUG_MSG("udp: No PCB for port %u, sending ICMP unreachable\n", dst_port);
    
    // 获取原始 IP 头部（在 buf->network_header 之前）
    ip_header_t *orig_ip = (ip_header_t *)buf->network_header;
    icmp_send_dest_unreachable(src_ip, ICMP_PORT_UNREACHABLE, orig_ip, udp);
    
    netbuf_free(buf);
}

int udp_output(uint16_t src_port, uint32_t dst_ip, uint16_t dst_port,
               uint8_t *data, uint32_t len) {
    netdev_t *dev = netdev_get_default();
    if (!dev) {
        LOG_ERROR_MSG("udp: No network device available\n");
        return -1;
    }
    
    // 计算 UDP 长度
    uint32_t udp_len = UDP_HEADER_LEN + len;
    
    // 分配缓冲区
    netbuf_t *buf = netbuf_alloc(udp_len);
    if (!buf) {
        LOG_ERROR_MSG("udp: Failed to allocate buffer\n");
        return -1;
    }
    
    // 填充 UDP 数据报
    uint8_t *pkt = netbuf_put(buf, udp_len);
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
    udp->checksum = udp_checksum(dev->ip_addr, dst_ip, udp, udp_len);
    
    // 发送
    int ret = ip_output(dev, buf, dst_ip, IP_PROTO_UDP);
    if (ret < 0) {
        netbuf_free(buf);
    }
    
    return ret;
}

udp_pcb_t *udp_pcb_new(void) {
    udp_pcb_t *pcb = (udp_pcb_t *)kmalloc(sizeof(udp_pcb_t));
    if (!pcb) {
        return NULL;
    }
    
    memset(pcb, 0, sizeof(udp_pcb_t));
    
    // 添加到链表
    bool irq_state;
    spinlock_lock_irqsave(&udp_lock, &irq_state);
    pcb->next = udp_pcbs;
    udp_pcbs = pcb;
    spinlock_unlock_irqrestore(&udp_lock, irq_state);
    
    return pcb;
}

void udp_pcb_free(udp_pcb_t *pcb) {
    if (!pcb) {
        return;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&udp_lock, &irq_state);
    
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
    
    spinlock_unlock_irqrestore(&udp_lock, irq_state);
    
    // 释放接收队列
    netbuf_t *buf = pcb->recv_queue;
    while (buf) {
        netbuf_t *next = buf->next;
        netbuf_free(buf);
        buf = next;
    }
    
    kfree(pcb);
}

int udp_bind(udp_pcb_t *pcb, uint32_t local_ip, uint16_t local_port) {
    if (!pcb) {
        return -1;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&udp_lock, &irq_state);
    
    // 检查端口是否已被使用
    for (udp_pcb_t *p = udp_pcbs; p != NULL; p = p->next) {
        if (p != pcb && p->local_port == local_port) {
            if (p->local_ip == 0 || local_ip == 0 || p->local_ip == local_ip) {
                spinlock_unlock_irqrestore(&udp_lock, irq_state);
                return -1;  // 端口已被使用
            }
        }
    }
    
    pcb->local_ip = local_ip;
    pcb->local_port = local_port;
    
    spinlock_unlock_irqrestore(&udp_lock, irq_state);
    return 0;
}

int udp_connect(udp_pcb_t *pcb, uint32_t remote_ip, uint16_t remote_port) {
    if (!pcb) {
        return -1;
    }
    
    pcb->remote_ip = remote_ip;
    pcb->remote_port = remote_port;
    
    // 如果未绑定本地端口，分配一个临时端口
    if (pcb->local_port == 0) {
        pcb->local_port = udp_alloc_port();
        if (pcb->local_port == 0) {
            return -1;
        }
    }
    
    return 0;
}

void udp_disconnect(udp_pcb_t *pcb) {
    if (pcb) {
        pcb->remote_ip = 0;
        pcb->remote_port = 0;
    }
}

int udp_send(udp_pcb_t *pcb, netbuf_t *buf) {
    if (!pcb || !buf) {
        return -1;
    }
    
    if (pcb->remote_ip == 0 || pcb->remote_port == 0) {
        return -1;  // 未连接
    }
    
    return udp_sendto(pcb, buf, pcb->remote_ip, pcb->remote_port);
}

int udp_sendto(udp_pcb_t *pcb, netbuf_t *buf, uint32_t dst_ip, uint16_t dst_port) {
    if (!pcb || !buf) {
        return -1;
    }
    
    netdev_t *dev = netdev_get_default();
    if (!dev) {
        return -1;
    }
    
    // 如果未绑定本地端口，分配一个临时端口
    if (pcb->local_port == 0) {
        pcb->local_port = udp_alloc_port();
        if (pcb->local_port == 0) {
            return -1;
        }
    }
    
    // 添加 UDP 头部
    uint8_t *header_ptr = netbuf_push(buf, UDP_HEADER_LEN);
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
    udp->checksum = udp_checksum(src_ip, dst_ip, udp, udp_len);
    
    // 发送
    return ip_output(dev, buf, dst_ip, IP_PROTO_UDP);
}

void udp_recv(udp_pcb_t *pcb,
              void (*callback)(udp_pcb_t *pcb, netbuf_t *buf,
                              uint32_t src_ip, uint16_t src_port),
              void *arg) {
    if (pcb) {
        pcb->recv_callback = callback;
        pcb->callback_arg = arg;
    }
}

uint16_t udp_checksum(uint32_t src_ip, uint32_t dst_ip, udp_header_t *udp, uint16_t len) {
    uint32_t sum = 0;
    
    // 计算伪首部校验和
    udp_pseudo_header_t pseudo;
    pseudo.src_addr = src_ip;
    pseudo.dst_addr = dst_ip;
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTO_UDP;
    pseudo.udp_length = htons(len);
    
    sum = checksum_partial(sum, &pseudo, sizeof(pseudo));
    
    // 计算 UDP 头部和数据校验和
    sum = checksum_partial(sum, udp, len);
    
    return checksum_finish(sum);
}

uint16_t udp_alloc_port(void) {
    bool irq_state;
    spinlock_lock_irqsave(&udp_lock, &irq_state);
    
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
            spinlock_unlock_irqrestore(&udp_lock, irq_state);
            return port;
        }
        
    } while (next_ephemeral_port != start_port);
    
    spinlock_unlock_irqrestore(&udp_lock, irq_state);
    return 0;  // 没有可用端口
}

