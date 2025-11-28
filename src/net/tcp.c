/**
 * @file tcp.c
 * @brief TCP 协议实现
 */

#include <net/tcp.h>
#include <net/ip.h>
#include <net/netdev.h>
#include <net/netbuf.h>
#include <net/checksum.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <drivers/timer.h>
#include <kernel/sync/spinlock.h>

// TCP PCB 链表
static tcp_pcb_t *tcp_pcbs = NULL;          // 活动连接
static tcp_pcb_t *tcp_listen_pcbs = NULL;   // 监听连接
static spinlock_t tcp_lock;

// 临时端口分配
#define TCP_EPHEMERAL_PORT_MIN  49152
#define TCP_EPHEMERAL_PORT_MAX  65535
static uint32_t next_ephemeral_port = TCP_EPHEMERAL_PORT_MIN;

// 初始序列号
static uint32_t tcp_isn = 0;

// 默认缓冲区大小
#define TCP_SEND_BUF_SIZE   8192
#define TCP_RECV_BUF_SIZE   8192

/**
 * @brief 生成初始序列号
 */
static uint32_t tcp_gen_isn(void) {
    // 简单实现：使用计时器值
    tcp_isn += (uint32_t)timer_get_uptime_ms() * 250000;
    return tcp_isn;
}

/**
 * @brief 查找匹配的 TCP PCB
 */
static tcp_pcb_t *tcp_find_pcb(uint32_t local_ip, uint16_t local_port,
                               uint32_t remote_ip, uint16_t remote_port) {
    // 首先在活动连接中查找
    for (tcp_pcb_t *pcb = tcp_pcbs; pcb != NULL; pcb = pcb->next) {
        if (pcb->local_port == local_port &&
            pcb->remote_port == remote_port &&
            pcb->remote_ip == remote_ip &&
            (pcb->local_ip == 0 || pcb->local_ip == local_ip)) {
            return pcb;
        }
    }
    
    // 在监听连接中查找
    for (tcp_pcb_t *pcb = tcp_listen_pcbs; pcb != NULL; pcb = pcb->next) {
        if (pcb->local_port == local_port &&
            (pcb->local_ip == 0 || pcb->local_ip == local_ip)) {
            return pcb;
        }
    }
    
    return NULL;
}

/**
 * @brief 发送 TCP 段
 */
static int tcp_send_segment(tcp_pcb_t *pcb, uint8_t flags, uint8_t *data, uint32_t len) {
    netdev_t *dev = netdev_get_default();
    if (!dev) {
        return -1;
    }
    
    // 计算 TCP 长度
    uint32_t tcp_len = TCP_HEADER_MIN_LEN + len;
    
    // 分配缓冲区
    netbuf_t *buf = netbuf_alloc(tcp_len);
    if (!buf) {
        return -1;
    }
    
    // 填充 TCP 段
    uint8_t *pkt = netbuf_put(buf, tcp_len);
    tcp_header_t *tcp = (tcp_header_t *)pkt;
    
    tcp->src_port = htons(pcb->local_port);
    tcp->dst_port = htons(pcb->remote_port);
    tcp->seq_num = htonl(pcb->snd_nxt);
    tcp->ack_num = htonl(pcb->rcv_nxt);
    tcp->data_offset = (TCP_HEADER_MIN_LEN / 4) << 4;
    tcp->flags = flags;
    tcp->window = htons(pcb->rcv_wnd);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;
    
    // 复制数据
    if (data && len > 0) {
        memcpy(pkt + TCP_HEADER_MIN_LEN, data, len);
    }
    
    // 计算校验和
    uint32_t src_ip = (pcb->local_ip != 0) ? pcb->local_ip : dev->ip_addr;
    tcp->checksum = tcp_checksum(src_ip, pcb->remote_ip, tcp, tcp_len);
    
    // 更新发送序列号
    if (flags & TCP_FLAG_SYN) {
        pcb->snd_nxt++;
    }
    if (flags & TCP_FLAG_FIN) {
        pcb->snd_nxt++;
    }
    pcb->snd_nxt += len;
    
    // 记录发送时间
    pcb->last_send_time = (uint32_t)timer_get_uptime_ms();
    
    // 发送
    int ret = ip_output(dev, buf, pcb->remote_ip, IP_PROTO_TCP);
    if (ret < 0) {
        netbuf_free(buf);
    }
    
    return ret;
}

/**
 * @brief 发送 RST 段
 */
static void tcp_send_rst(uint32_t src_ip, uint32_t dst_ip,
                         uint16_t src_port, uint16_t dst_port,
                         uint32_t seq, uint32_t ack, bool ack_valid) {
    netdev_t *dev = netdev_get_default();
    if (!dev) {
        return;
    }
    
    // 分配缓冲区
    netbuf_t *buf = netbuf_alloc(TCP_HEADER_MIN_LEN);
    if (!buf) {
        return;
    }
    
    // 填充 TCP 段
    uint8_t *pkt = netbuf_put(buf, TCP_HEADER_MIN_LEN);
    tcp_header_t *tcp = (tcp_header_t *)pkt;
    
    tcp->src_port = htons(src_port);
    tcp->dst_port = htons(dst_port);
    tcp->seq_num = htonl(seq);
    tcp->ack_num = htonl(ack);
    tcp->data_offset = (TCP_HEADER_MIN_LEN / 4) << 4;
    tcp->flags = TCP_FLAG_RST | (ack_valid ? TCP_FLAG_ACK : 0);
    tcp->window = 0;
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;
    
    // 计算校验和
    tcp->checksum = tcp_checksum(src_ip, dst_ip, tcp, TCP_HEADER_MIN_LEN);
    
    // 发送
    int ret = ip_output(dev, buf, dst_ip, IP_PROTO_TCP);
    if (ret < 0) {
        netbuf_free(buf);
    }
}

void tcp_init(void) {
    spinlock_init(&tcp_lock);
    tcp_pcbs = NULL;
    tcp_listen_pcbs = NULL;
    next_ephemeral_port = TCP_EPHEMERAL_PORT_MIN;
    tcp_isn = (uint32_t)timer_get_uptime_ms();
    
    LOG_INFO_MSG("tcp: TCP protocol initialized\n");
}

void tcp_input(netdev_t *dev, netbuf_t *buf, uint32_t src_ip, uint32_t dst_ip) {
    if (!dev || !buf) {
        return;
    }
    
    // 检查报文长度
    if (buf->len < TCP_HEADER_MIN_LEN) {
        LOG_WARN_MSG("tcp: Packet too short (%u bytes)\n", buf->len);
        netbuf_free(buf);
        return;
    }
    
    tcp_header_t *tcp = (tcp_header_t *)buf->data;
    buf->transport_header = tcp;
    
    // 获取头部长度
    uint8_t hdr_len = tcp_header_len(tcp);
    if (hdr_len < TCP_HEADER_MIN_LEN || hdr_len > buf->len) {
        LOG_WARN_MSG("tcp: Invalid header length %u\n", hdr_len);
        netbuf_free(buf);
        return;
    }
    
    // 验证校验和
    uint16_t orig_checksum = tcp->checksum;
    tcp->checksum = 0;
    uint16_t calc_checksum = tcp_checksum(src_ip, dst_ip, tcp, buf->len);
    
    if (calc_checksum != orig_checksum) {
        LOG_WARN_MSG("tcp: Invalid checksum\n");
        netbuf_free(buf);
        return;
    }
    tcp->checksum = orig_checksum;
    
    // 解析字段
    uint16_t src_port = ntohs(tcp->src_port);
    uint16_t dst_port = ntohs(tcp->dst_port);
    uint32_t seq = ntohl(tcp->seq_num);
    uint32_t ack = ntohl(tcp->ack_num);
    uint8_t flags = tcp->flags;
    
    // 计算数据长度
    uint32_t data_len = buf->len - hdr_len;
    uint8_t *data = (data_len > 0) ? (uint8_t *)tcp + hdr_len : NULL;
    
    // 查找匹配的 PCB
    bool irq_state;
    spinlock_lock_irqsave(&tcp_lock, &irq_state);
    
    tcp_pcb_t *pcb = tcp_find_pcb(dst_ip, dst_port, src_ip, src_port);
    
    if (!pcb) {
        spinlock_unlock_irqrestore(&tcp_lock, irq_state);
        
        // 没有匹配的连接，发送 RST
        if (!(flags & TCP_FLAG_RST)) {
            if (flags & TCP_FLAG_ACK) {
                tcp_send_rst(dst_ip, src_ip, dst_port, src_port, ack, 0, false);
            } else {
                uint32_t rst_seq = 0;
                uint32_t rst_ack = seq + data_len;
                if (flags & TCP_FLAG_SYN) rst_ack++;
                if (flags & TCP_FLAG_FIN) rst_ack++;
                tcp_send_rst(dst_ip, src_ip, dst_port, src_port, rst_seq, rst_ack, true);
            }
        }
        
        netbuf_free(buf);
        return;
    }
    
    // 根据状态处理
    switch (pcb->state) {
        case TCP_LISTEN: {
            // 只处理 SYN
            if (flags & TCP_FLAG_RST) {
                break;
            }
            if (flags & TCP_FLAG_ACK) {
                tcp_send_rst(dst_ip, src_ip, dst_port, src_port, ack, 0, false);
                break;
            }
            if (flags & TCP_FLAG_SYN) {
                // 检查是否超过 backlog
                if (pcb->pending_count >= pcb->backlog) {
                    break;
                }
                
                // 创建新的 PCB 用于这个连接
                tcp_pcb_t *new_pcb = tcp_pcb_new();
                if (!new_pcb) {
                    break;
                }
                
                new_pcb->local_ip = dst_ip;
                new_pcb->local_port = dst_port;
                new_pcb->remote_ip = src_ip;
                new_pcb->remote_port = src_port;
                new_pcb->state = TCP_SYN_RECEIVED;
                new_pcb->irs = seq;
                new_pcb->rcv_nxt = seq + 1;
                new_pcb->iss = tcp_gen_isn();
                new_pcb->snd_nxt = new_pcb->iss;
                new_pcb->snd_una = new_pcb->iss;
                new_pcb->listen_pcb = pcb;
                
                // 加入待处理队列
                new_pcb->next = pcb->pending_queue;
                pcb->pending_queue = new_pcb;
                pcb->pending_count++;
                
                spinlock_unlock_irqrestore(&tcp_lock, irq_state);
                
                // 发送 SYN+ACK
                tcp_send_segment(new_pcb, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
                
                netbuf_free(buf);
                return;
            }
            break;
        }
        
        case TCP_SYN_SENT: {
            // 等待 SYN+ACK
            if (flags & TCP_FLAG_ACK) {
                if (ack != pcb->snd_nxt) {
                    // ACK 不正确
                    if (!(flags & TCP_FLAG_RST)) {
                        tcp_send_rst(dst_ip, src_ip, dst_port, src_port, ack, 0, false);
                    }
                    break;
                }
            }
            if (flags & TCP_FLAG_RST) {
                if (flags & TCP_FLAG_ACK) {
                    pcb->state = TCP_CLOSED;
                    if (pcb->error_callback) {
                        spinlock_unlock_irqrestore(&tcp_lock, irq_state);
                        pcb->error_callback(pcb, -1, pcb->callback_arg);
                        netbuf_free(buf);
                        return;
                    }
                }
                break;
            }
            if (flags & TCP_FLAG_SYN) {
                pcb->irs = seq;
                pcb->rcv_nxt = seq + 1;
                if (flags & TCP_FLAG_ACK) {
                    pcb->snd_una = ack;
                }
                
                if (TCP_SEQ_GT(pcb->snd_una, pcb->iss)) {
                    // 连接建立
                    pcb->state = TCP_ESTABLISHED;
                    spinlock_unlock_irqrestore(&tcp_lock, irq_state);
                    
                    // 发送 ACK
                    tcp_send_segment(pcb, TCP_FLAG_ACK, NULL, 0);
                    
                    netbuf_free(buf);
                    return;
                } else {
                    // 同时打开
                    pcb->state = TCP_SYN_RECEIVED;
                    spinlock_unlock_irqrestore(&tcp_lock, irq_state);
                    
                    tcp_send_segment(pcb, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
                    
                    netbuf_free(buf);
                    return;
                }
            }
            break;
        }
        
        case TCP_SYN_RECEIVED: {
            if (flags & TCP_FLAG_RST) {
                pcb->state = TCP_CLOSED;
                break;
            }
            if (flags & TCP_FLAG_ACK) {
                if (ack == pcb->snd_nxt) {
                    pcb->snd_una = ack;
                    pcb->state = TCP_ESTABLISHED;
                    
                    // 如果是被动连接，加入 accept 队列
                    if (pcb->listen_pcb) {
                        tcp_pcb_t *listen = pcb->listen_pcb;
                        
                        // 从待处理队列移除
                        tcp_pcb_t **pp = &listen->pending_queue;
                        while (*pp && *pp != pcb) {
                            pp = &(*pp)->next;
                        }
                        if (*pp == pcb) {
                            *pp = pcb->next;
                            listen->pending_count--;
                        }
                        
                        // 加入 accept 队列
                        pcb->next = listen->accept_queue;
                        listen->accept_queue = pcb;
                        pcb->listen_pcb = NULL;
                        
                        // 调用回调
                        if (listen->accept_callback) {
                            spinlock_unlock_irqrestore(&tcp_lock, irq_state);
                            listen->accept_callback(pcb, listen->callback_arg);
                            netbuf_free(buf);
                            return;
                        }
                    }
                }
            }
            break;
        }
        
        case TCP_ESTABLISHED:
        case TCP_FIN_WAIT_1:
        case TCP_FIN_WAIT_2:
        case TCP_CLOSE_WAIT: {
            // 处理 RST
            if (flags & TCP_FLAG_RST) {
                pcb->state = TCP_CLOSED;
                if (pcb->error_callback) {
                    spinlock_unlock_irqrestore(&tcp_lock, irq_state);
                    pcb->error_callback(pcb, -1, pcb->callback_arg);
                    netbuf_free(buf);
                    return;
                }
                break;
            }
            
            // 处理 ACK
            if (flags & TCP_FLAG_ACK) {
                if (TCP_SEQ_GT(ack, pcb->snd_una) && TCP_SEQ_LEQ(ack, pcb->snd_nxt)) {
                    pcb->snd_una = ack;
                }
                
                if (pcb->state == TCP_FIN_WAIT_1 && ack == pcb->snd_nxt) {
                    pcb->state = TCP_FIN_WAIT_2;
                }
            }
            
            // 处理数据
            if (data_len > 0 && seq == pcb->rcv_nxt) {
                // 复制数据到接收缓冲区
                uint32_t copy_len = data_len;
                if (pcb->recv_buf && pcb->recv_len + copy_len <= pcb->recv_buf_size) {
                    memcpy(pcb->recv_buf + pcb->recv_len, data, copy_len);
                    pcb->recv_len += copy_len;
                }
                pcb->rcv_nxt += data_len;
                
                spinlock_unlock_irqrestore(&tcp_lock, irq_state);
                
                // 发送 ACK
                tcp_send_segment(pcb, TCP_FLAG_ACK, NULL, 0);
                
                // 调用接收回调
                if (pcb->recv_callback) {
                    pcb->recv_callback(pcb, pcb->callback_arg);
                }
                
                netbuf_free(buf);
                return;
            }
            
            // 处理 FIN
            if (flags & TCP_FLAG_FIN) {
                pcb->rcv_nxt++;
                
                switch (pcb->state) {
                    case TCP_ESTABLISHED:
                        pcb->state = TCP_CLOSE_WAIT;
                        break;
                    case TCP_FIN_WAIT_1:
                        pcb->state = TCP_CLOSING;
                        break;
                    case TCP_FIN_WAIT_2:
                        pcb->state = TCP_TIME_WAIT;
                        // TODO: 启动 TIME_WAIT 定时器
                        break;
                    default:
                        break;
                }
                
                spinlock_unlock_irqrestore(&tcp_lock, irq_state);
                
                // 发送 ACK
                tcp_send_segment(pcb, TCP_FLAG_ACK, NULL, 0);
                
                netbuf_free(buf);
                return;
            }
            break;
        }
        
        case TCP_CLOSING: {
            if (flags & TCP_FLAG_ACK) {
                if (ack == pcb->snd_nxt) {
                    pcb->state = TCP_TIME_WAIT;
                }
            }
            break;
        }
        
        case TCP_LAST_ACK: {
            if (flags & TCP_FLAG_ACK) {
                if (ack == pcb->snd_nxt) {
                    pcb->state = TCP_CLOSED;
                }
            }
            break;
        }
        
        default:
            break;
    }
    
    spinlock_unlock_irqrestore(&tcp_lock, irq_state);
    netbuf_free(buf);
}

tcp_pcb_t *tcp_pcb_new(void) {
    tcp_pcb_t *pcb = (tcp_pcb_t *)kmalloc(sizeof(tcp_pcb_t));
    if (!pcb) {
        return NULL;
    }
    
    memset(pcb, 0, sizeof(tcp_pcb_t));
    
    pcb->state = TCP_CLOSED;
    pcb->rcv_wnd = TCP_DEFAULT_WINDOW;
    pcb->snd_wnd = TCP_DEFAULT_WINDOW;
    pcb->mss = TCP_DEFAULT_MSS;
    pcb->rto = TCP_DEFAULT_RTO;
    
    // 分配缓冲区
    pcb->recv_buf = (uint8_t *)kmalloc(TCP_RECV_BUF_SIZE);
    pcb->recv_buf_size = TCP_RECV_BUF_SIZE;
    pcb->send_buf = (uint8_t *)kmalloc(TCP_SEND_BUF_SIZE);
    pcb->send_buf_size = TCP_SEND_BUF_SIZE;
    
    if (!pcb->recv_buf || !pcb->send_buf) {
        if (pcb->recv_buf) kfree(pcb->recv_buf);
        if (pcb->send_buf) kfree(pcb->send_buf);
        kfree(pcb);
        return NULL;
    }
    
    mutex_init(&pcb->lock);
    
    // 添加到活动链表
    bool irq_state;
    spinlock_lock_irqsave(&tcp_lock, &irq_state);
    pcb->next = tcp_pcbs;
    tcp_pcbs = pcb;
    spinlock_unlock_irqrestore(&tcp_lock, irq_state);
    
    return pcb;
}

void tcp_pcb_free(tcp_pcb_t *pcb) {
    if (!pcb) {
        return;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&tcp_lock, &irq_state);
    
    // 从活动链表移除
    tcp_pcb_t **pp = &tcp_pcbs;
    while (*pp && *pp != pcb) {
        pp = &(*pp)->next;
    }
    if (*pp == pcb) {
        *pp = pcb->next;
    }
    
    // 从监听链表移除
    pp = &tcp_listen_pcbs;
    while (*pp && *pp != pcb) {
        pp = &(*pp)->next;
    }
    if (*pp == pcb) {
        *pp = pcb->next;
    }
    
    spinlock_unlock_irqrestore(&tcp_lock, irq_state);
    
    // 释放缓冲区
    if (pcb->recv_buf) kfree(pcb->recv_buf);
    if (pcb->send_buf) kfree(pcb->send_buf);
    
    kfree(pcb);
}

int tcp_bind(tcp_pcb_t *pcb, uint32_t local_ip, uint16_t local_port) {
    if (!pcb || pcb->state != TCP_CLOSED) {
        return -1;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&tcp_lock, &irq_state);
    
    // 检查端口是否已被使用
    for (tcp_pcb_t *p = tcp_pcbs; p != NULL; p = p->next) {
        if (p != pcb && p->local_port == local_port) {
            if (p->local_ip == 0 || local_ip == 0 || p->local_ip == local_ip) {
                spinlock_unlock_irqrestore(&tcp_lock, irq_state);
                return -1;
            }
        }
    }
    for (tcp_pcb_t *p = tcp_listen_pcbs; p != NULL; p = p->next) {
        if (p != pcb && p->local_port == local_port) {
            if (p->local_ip == 0 || local_ip == 0 || p->local_ip == local_ip) {
                spinlock_unlock_irqrestore(&tcp_lock, irq_state);
                return -1;
            }
        }
    }
    
    pcb->local_ip = local_ip;
    pcb->local_port = local_port;
    
    spinlock_unlock_irqrestore(&tcp_lock, irq_state);
    return 0;
}

int tcp_listen(tcp_pcb_t *pcb, int backlog) {
    if (!pcb || pcb->state != TCP_CLOSED) {
        return -1;
    }
    
    if (pcb->local_port == 0) {
        return -1;  // 必须先绑定
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&tcp_lock, &irq_state);
    
    // 从活动链表移到监听链表
    tcp_pcb_t **pp = &tcp_pcbs;
    while (*pp && *pp != pcb) {
        pp = &(*pp)->next;
    }
    if (*pp == pcb) {
        *pp = pcb->next;
    }
    
    pcb->next = tcp_listen_pcbs;
    tcp_listen_pcbs = pcb;
    
    pcb->state = TCP_LISTEN;
    pcb->backlog = (backlog > 0) ? backlog : 5;
    
    spinlock_unlock_irqrestore(&tcp_lock, irq_state);
    return 0;
}

int tcp_connect(tcp_pcb_t *pcb, uint32_t remote_ip, uint16_t remote_port) {
    if (!pcb || pcb->state != TCP_CLOSED) {
        return -1;
    }
    
    // 如果未绑定，分配临时端口
    if (pcb->local_port == 0) {
        pcb->local_port = tcp_alloc_port();
        if (pcb->local_port == 0) {
            return -1;
        }
    }
    
    pcb->remote_ip = remote_ip;
    pcb->remote_port = remote_port;
    
    // 生成初始序列号
    pcb->iss = tcp_gen_isn();
    pcb->snd_una = pcb->iss;
    pcb->snd_nxt = pcb->iss;
    
    pcb->state = TCP_SYN_SENT;
    
    // 发送 SYN
    return tcp_send_segment(pcb, TCP_FLAG_SYN, NULL, 0);
}

tcp_pcb_t *tcp_accept(tcp_pcb_t *pcb) {
    if (!pcb || pcb->state != TCP_LISTEN) {
        return NULL;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&tcp_lock, &irq_state);
    
    tcp_pcb_t *new_pcb = pcb->accept_queue;
    if (new_pcb) {
        pcb->accept_queue = new_pcb->next;
        new_pcb->next = NULL;
    }
    
    spinlock_unlock_irqrestore(&tcp_lock, irq_state);
    
    return new_pcb;
}

int tcp_write(tcp_pcb_t *pcb, const void *data, uint32_t len) {
    if (!pcb || !data || len == 0) {
        return -1;
    }
    
    if (pcb->state != TCP_ESTABLISHED && pcb->state != TCP_CLOSE_WAIT) {
        return -1;
    }
    
    // 复制数据到发送缓冲区
    uint32_t copy_len = len;
    if (pcb->send_len + copy_len > pcb->send_buf_size) {
        copy_len = pcb->send_buf_size - pcb->send_len;
    }
    
    if (copy_len == 0) {
        return 0;  // 缓冲区满
    }
    
    memcpy(pcb->send_buf + pcb->send_len, data, copy_len);
    pcb->send_len += copy_len;
    
    // 发送数据
    uint32_t send_len = copy_len;
    if (send_len > pcb->mss) {
        send_len = pcb->mss;
    }
    
    tcp_send_segment(pcb, TCP_FLAG_ACK | TCP_FLAG_PSH, 
                     pcb->send_buf, send_len);
    
    // 移除已发送的数据
    if (send_len < pcb->send_len) {
        memmove(pcb->send_buf, pcb->send_buf + send_len, 
                pcb->send_len - send_len);
    }
    pcb->send_len -= send_len;
    
    return copy_len;
}

int tcp_read(tcp_pcb_t *pcb, void *buf, uint32_t len) {
    if (!pcb || !buf || len == 0) {
        return -1;
    }
    
    if (pcb->recv_len == 0) {
        if (pcb->state == TCP_CLOSE_WAIT || pcb->state == TCP_CLOSED) {
            return 0;  // 连接关闭
        }
        return -1;  // 暂无数据
    }
    
    // 复制数据
    uint32_t copy_len = len;
    if (copy_len > pcb->recv_len - pcb->recv_read_pos) {
        copy_len = pcb->recv_len - pcb->recv_read_pos;
    }
    
    memcpy(buf, pcb->recv_buf + pcb->recv_read_pos, copy_len);
    pcb->recv_read_pos += copy_len;
    
    // 如果所有数据都已读取，重置缓冲区
    if (pcb->recv_read_pos >= pcb->recv_len) {
        pcb->recv_len = 0;
        pcb->recv_read_pos = 0;
    }
    
    return copy_len;
}

int tcp_close(tcp_pcb_t *pcb) {
    if (!pcb) {
        return -1;
    }
    
    switch (pcb->state) {
        case TCP_CLOSED:
        case TCP_LISTEN:
        case TCP_SYN_SENT:
            pcb->state = TCP_CLOSED;
            break;
            
        case TCP_SYN_RECEIVED:
        case TCP_ESTABLISHED:
            pcb->state = TCP_FIN_WAIT_1;
            tcp_send_segment(pcb, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
            break;
            
        case TCP_CLOSE_WAIT:
            pcb->state = TCP_LAST_ACK;
            tcp_send_segment(pcb, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
            break;
            
        default:
            return -1;
    }
    
    return 0;
}

void tcp_abort(tcp_pcb_t *pcb) {
    if (!pcb) {
        return;
    }
    
    if (pcb->state != TCP_CLOSED && pcb->state != TCP_LISTEN) {
        netdev_t *dev = netdev_get_default();
        if (dev) {
            tcp_send_rst(dev->ip_addr, pcb->remote_ip,
                        pcb->local_port, pcb->remote_port,
                        pcb->snd_nxt, 0, false);
        }
    }
    
    pcb->state = TCP_CLOSED;
}

void tcp_accept_callback(tcp_pcb_t *pcb,
                         void (*callback)(tcp_pcb_t *new_pcb, void *arg),
                         void *arg) {
    if (pcb) {
        pcb->accept_callback = callback;
        pcb->callback_arg = arg;
    }
}

void tcp_recv_callback(tcp_pcb_t *pcb,
                       void (*callback)(tcp_pcb_t *pcb, void *arg),
                       void *arg) {
    if (pcb) {
        pcb->recv_callback = callback;
        pcb->callback_arg = arg;
    }
}

uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, tcp_header_t *tcp, uint16_t len) {
    uint32_t sum = 0;
    
    // 计算伪首部校验和
    tcp_pseudo_header_t pseudo;
    pseudo.src_addr = src_ip;
    pseudo.dst_addr = dst_ip;
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTO_TCP;
    pseudo.tcp_length = htons(len);
    
    sum = checksum_partial(sum, &pseudo, sizeof(pseudo));
    
    // 计算 TCP 头部和数据校验和
    sum = checksum_partial(sum, tcp, len);
    
    return checksum_finish(sum);
}

const char *tcp_state_name(tcp_state_t state) {
    static const char *names[] = {
        "CLOSED", "LISTEN", "SYN_SENT", "SYN_RECEIVED",
        "ESTABLISHED", "FIN_WAIT_1", "FIN_WAIT_2", "CLOSE_WAIT",
        "CLOSING", "LAST_ACK", "TIME_WAIT"
    };
    
    if (state >= 0 && state <= TCP_TIME_WAIT) {
        return names[state];
    }
    return "UNKNOWN";
}

uint16_t tcp_alloc_port(void) {
    bool irq_state;
    spinlock_lock_irqsave(&tcp_lock, &irq_state);
    
    uint16_t start_port = next_ephemeral_port;
    
    do {
        uint16_t port = next_ephemeral_port++;
        if (next_ephemeral_port > TCP_EPHEMERAL_PORT_MAX) {
            next_ephemeral_port = TCP_EPHEMERAL_PORT_MIN;
        }
        
        // 检查端口是否已被使用
        bool in_use = false;
        for (tcp_pcb_t *pcb = tcp_pcbs; pcb != NULL; pcb = pcb->next) {
            if (pcb->local_port == port) {
                in_use = true;
                break;
            }
        }
        if (!in_use) {
            for (tcp_pcb_t *pcb = tcp_listen_pcbs; pcb != NULL; pcb = pcb->next) {
                if (pcb->local_port == port) {
                    in_use = true;
                    break;
                }
            }
        }
        
        if (!in_use) {
            spinlock_unlock_irqrestore(&tcp_lock, irq_state);
            return port;
        }
        
    } while (next_ephemeral_port != start_port);
    
    spinlock_unlock_irqrestore(&tcp_lock, irq_state);
    return 0;
}

void tcp_timer(void) {
    // TODO: 实现重传定时器、TIME_WAIT 超时等
}

