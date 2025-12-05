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

// 前向声明
static int tcp_send_segment(tcp_pcb_t *pcb, uint8_t flags, uint8_t *data, uint32_t len);
static void tcp_free_unacked(tcp_pcb_t *pcb);
static void tcp_free_ooseq(tcp_pcb_t *pcb);

/**
 * @brief 生成初始序列号
 */
static uint32_t tcp_gen_isn(void) {
    // 简单实现：使用计时器值
    tcp_isn += (uint32_t)timer_get_uptime_ms() * 250000;
    return tcp_isn;
}

// ============================================================================
// RTT 估算和重传定时器
// ============================================================================

/**
 * @brief 计算 RTO（基于 Jacobson 算法）
 */
static uint32_t tcp_calc_rto(tcp_pcb_t *pcb) {
    // RTO = SRTT + 4 * RTTVAR
    uint32_t rto = (pcb->srtt / 8) + pcb->rttvar;
    
    if (rto < TCP_RTO_MIN) rto = TCP_RTO_MIN;
    if (rto > TCP_RTO_MAX) rto = TCP_RTO_MAX;
    
    return rto;
}

/**
 * @brief 更新 RTT 估算
 */
static void tcp_update_rtt(tcp_pcb_t *pcb, uint32_t measured_rtt) {
    if (pcb->srtt == 0) {
        // 首次测量
        pcb->srtt = measured_rtt * 8;
        pcb->rttvar = measured_rtt * 2;
    } else {
        // Jacobson 算法
        int32_t delta = (int32_t)measured_rtt - (int32_t)(pcb->srtt / 8);
        pcb->srtt = (uint32_t)((int32_t)pcb->srtt + delta);
        if (pcb->srtt == 0) pcb->srtt = 1;
        
        if (delta < 0) delta = -delta;
        pcb->rttvar = (uint32_t)((int32_t)pcb->rttvar + (delta - (int32_t)(pcb->rttvar / 4)));
        if (pcb->rttvar == 0) pcb->rttvar = 1;
    }
    
    pcb->rto = tcp_calc_rto(pcb);
}

/**
 * @brief 将段加入未确认队列
 */
static int tcp_queue_unacked(tcp_pcb_t *pcb, uint32_t seq, uint8_t flags, 
                             uint8_t *data, uint32_t data_len) {
    tcp_segment_t *seg = (tcp_segment_t *)kmalloc(sizeof(tcp_segment_t));
    if (!seg) return -1;
    
    memset(seg, 0, sizeof(tcp_segment_t));
    seg->seq = seq;
    seg->flags = flags;
    seg->data_len = data_len;
    
    // 计算段长度（SYN 和 FIN 各占 1 个序列号）
    seg->len = data_len;
    if (flags & TCP_FLAG_SYN) seg->len++;
    if (flags & TCP_FLAG_FIN) seg->len++;
    
    // 复制数据
    if (data_len > 0 && data) {
        seg->data = (uint8_t *)kmalloc(data_len);
        if (!seg->data) {
            kfree(seg);
            return -1;
        }
        memcpy(seg->data, data, data_len);
    }
    
    seg->send_time = (uint32_t)timer_get_uptime_ms();
    seg->retransmit_time = seg->send_time + pcb->rto;
    seg->retries = 0;
    
    // 加入队列尾部
    seg->next = NULL;
    if (!pcb->unacked) {
        pcb->unacked = seg;
    } else {
        tcp_segment_t *tail = pcb->unacked;
        while (tail->next) tail = tail->next;
        tail->next = seg;
    }
    
    // 启动重传定时器
    if (pcb->timer_retransmit == 0) {
        pcb->timer_retransmit = seg->retransmit_time;
    }
    
    // 开始 RTT 测量（只对第一个未确认段测量）
    if (!pcb->rtt_measuring) {
        pcb->rtt_measuring = true;
        pcb->rtt_seq = seq;
    }
    
    return 0;
}

/**
 * @brief 处理 ACK，移除已确认的段
 */
static void tcp_ack_received(tcp_pcb_t *pcb, uint32_t ack) {
    uint32_t now = (uint32_t)timer_get_uptime_ms();
    
    while (pcb->unacked) {
        tcp_segment_t *seg = pcb->unacked;
        uint32_t seg_end = seg->seq + seg->len;
        
        if (TCP_SEQ_LEQ(seg_end, ack)) {
            // 段已被完全确认
            
            // RTT 测量（只对未重传的段测量）
            if (pcb->rtt_measuring && seg->retries == 0 &&
                TCP_SEQ_LEQ(pcb->rtt_seq, seg->seq)) {
                uint32_t rtt = now - seg->send_time;
                tcp_update_rtt(pcb, rtt);
                pcb->rtt_measuring = false;
            }
            
            // 从队列移除
            pcb->unacked = seg->next;
            if (seg->data) kfree(seg->data);
            kfree(seg);
            
            // 拥塞控制：ACK 确认时增加 cwnd
            if (pcb->cwnd < pcb->ssthresh) {
                // 慢启动：指数增长
                pcb->cwnd += pcb->mss;
            } else {
                // 拥塞避免：线性增长
                pcb->cwnd += pcb->mss * pcb->mss / pcb->cwnd;
            }
        } else {
            break;
        }
    }
    
    // 更新重传定时器
    if (pcb->unacked) {
        pcb->timer_retransmit = pcb->unacked->retransmit_time;
    } else {
        pcb->timer_retransmit = 0;
    }
    
    // 重置重复 ACK 计数
    pcb->dup_ack_count = 0;
}

/**
 * @brief 处理重复 ACK（用于快速重传）
 */
static void tcp_dup_ack(tcp_pcb_t *pcb) {
    pcb->dup_ack_count++;
    
    // 收到 3 个重复 ACK，触发快速重传
    if (pcb->dup_ack_count == 3 && pcb->unacked) {
        tcp_segment_t *seg = pcb->unacked;
        
        // 快速重传：拥塞控制
        pcb->ssthresh = pcb->cwnd / 2;
        if (pcb->ssthresh < 2 * pcb->mss) {
            pcb->ssthresh = 2 * pcb->mss;
        }
        pcb->cwnd = pcb->ssthresh + 3 * pcb->mss;
        
        // 重传丢失的段
        LOG_DEBUG_MSG("tcp: Fast retransmit seq=%u\n", seg->seq);
        tcp_send_segment(pcb, seg->flags | TCP_FLAG_ACK, seg->data, seg->data_len);
        
        seg->retries++;
        seg->retransmit_time = (uint32_t)timer_get_uptime_ms() + pcb->rto;
    }
}

/**
 * @brief 释放未确认队列
 */
static void tcp_free_unacked(tcp_pcb_t *pcb) {
    tcp_segment_t *seg = pcb->unacked;
    while (seg) {
        tcp_segment_t *next = seg->next;
        if (seg->data) kfree(seg->data);
        kfree(seg);
        seg = next;
    }
    pcb->unacked = NULL;
    pcb->timer_retransmit = 0;
}

// ============================================================================
// 乱序报文处理
// ============================================================================

/**
 * @brief 将乱序段加入队列
 */
static int tcp_ooseq_add(tcp_pcb_t *pcb, uint32_t seq, uint8_t *data, uint32_t len) {
    // 检查是否超过最大数量
    if (pcb->ooseq_count >= TCP_MAX_OOSEQ) {
        return -1;  // 队列满，丢弃
    }
    
    // 检查是否在接收窗口内
    if (!TCP_SEQ_BETWEEN(seq, pcb->rcv_nxt, pcb->rcv_nxt + pcb->rcv_wnd)) {
        return -1;  // 不在窗口内
    }
    
    // 分配段结构
    tcp_ooseq_t *seg = (tcp_ooseq_t *)kmalloc(sizeof(tcp_ooseq_t));
    if (!seg) return -1;
    
    seg->seq = seq;
    seg->len = len;
    seg->data = (uint8_t *)kmalloc(len);
    if (!seg->data) {
        kfree(seg);
        return -1;
    }
    memcpy(seg->data, data, len);
    
    // 按序列号插入链表
    tcp_ooseq_t **pp = &pcb->ooseq;
    while (*pp && TCP_SEQ_LT((*pp)->seq, seq)) {
        pp = &(*pp)->next;
    }
    
    // 检查重叠（简化处理：有重叠则丢弃）
    if (*pp && (*pp)->seq == seq) {
        kfree(seg->data);
        kfree(seg);
        return 0;  // 重复段
    }
    
    seg->next = *pp;
    *pp = seg;
    pcb->ooseq_count++;
    
    LOG_DEBUG_MSG("tcp: Queued out-of-order segment seq=%u len=%u (count=%u)\n",
                  seq, len, pcb->ooseq_count);
    
    return 0;
}

/**
 * @brief 尝试从乱序队列合并连续数据
 */
static void tcp_ooseq_merge(tcp_pcb_t *pcb) {
    while (pcb->ooseq) {
        tcp_ooseq_t *seg = pcb->ooseq;
        
        // 检查是否可以合并
        if (seg->seq == pcb->rcv_nxt) {
            // 复制数据到接收缓冲区
            uint32_t copy_len = seg->len;
            if (pcb->recv_buf && pcb->recv_len + copy_len <= pcb->recv_buf_size) {
                memcpy(pcb->recv_buf + pcb->recv_len, seg->data, copy_len);
                pcb->recv_len += copy_len;
            }
            pcb->rcv_nxt += seg->len;
            
            LOG_DEBUG_MSG("tcp: Merged out-of-order segment seq=%u len=%u\n",
                          seg->seq, seg->len);
            
            // 从队列移除
            pcb->ooseq = seg->next;
            pcb->ooseq_count--;
            kfree(seg->data);
            kfree(seg);
        } else if (TCP_SEQ_LT(seg->seq, pcb->rcv_nxt)) {
            // 段已过期（被前面的数据覆盖），移除
            pcb->ooseq = seg->next;
            pcb->ooseq_count--;
            kfree(seg->data);
            kfree(seg);
        } else {
            // 还有空洞，停止合并
            break;
        }
    }
}

/**
 * @brief 释放乱序队列
 */
static void tcp_free_ooseq(tcp_pcb_t *pcb) {
    tcp_ooseq_t *seg = pcb->ooseq;
    while (seg) {
        tcp_ooseq_t *next = seg->next;
        kfree(seg->data);
        kfree(seg);
        seg = next;
    }
    pcb->ooseq = NULL;
    pcb->ooseq_count = 0;
}

/**
 * @brief 处理接收数据（支持乱序处理）
 */
static void tcp_process_data(tcp_pcb_t *pcb, uint32_t seq, uint8_t *data, uint32_t data_len) {
    if (data_len == 0) return;
    
    if (seq == pcb->rcv_nxt) {
        // 按序到达，直接复制到接收缓冲区
        uint32_t copy_len = data_len;
        if (pcb->recv_buf && pcb->recv_len + copy_len <= pcb->recv_buf_size) {
            memcpy(pcb->recv_buf + pcb->recv_len, data, copy_len);
            pcb->recv_len += copy_len;
        }
        pcb->rcv_nxt += data_len;
        
        // 尝试合并乱序队列
        tcp_ooseq_merge(pcb);
        
    } else if (TCP_SEQ_GT(seq, pcb->rcv_nxt)) {
        // 乱序到达，加入乱序队列
        tcp_ooseq_add(pcb, seq, data, data_len);
    }
    // 如果 seq < rcv_nxt，这是重复数据，忽略
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
    
    // 保存当前序列号（用于加入未确认队列）
    uint32_t seq = pcb->snd_nxt;
    
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
        return ret;
    }
    
    // 将需要确认的段加入未确认队列（SYN、FIN 或带数据的段）
    bool needs_ack = (flags & TCP_FLAG_SYN) || (flags & TCP_FLAG_FIN) || (len > 0);
    if (needs_ack && pcb->state != TCP_LISTEN) {
        tcp_queue_unacked(pcb, seq, flags, data, len);
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
                tcp_free_unacked(pcb);
                tcp_free_ooseq(pcb);
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
                    // 新的 ACK，处理确认
                    pcb->snd_una = ack;
                    tcp_ack_received(pcb, ack);
                } else if (ack == pcb->snd_una && pcb->unacked) {
                    // 重复 ACK，可能需要快速重传
                    tcp_dup_ack(pcb);
                }
                
                if (pcb->state == TCP_FIN_WAIT_1 && ack == pcb->snd_nxt) {
                    pcb->state = TCP_FIN_WAIT_2;
                }
            }
            
            // 处理数据（支持乱序）
            if (data_len > 0) {
                // 使用新的数据处理函数（支持乱序报文）
                uint32_t old_rcv_nxt = pcb->rcv_nxt;
                tcp_process_data(pcb, seq, data, data_len);
                
                // 如果有数据被接收（按序或乱序合并后）
                if (pcb->rcv_nxt != old_rcv_nxt) {
                    spinlock_unlock_irqrestore(&tcp_lock, irq_state);
                    
                    // 发送 ACK
                    tcp_send_segment(pcb, TCP_FLAG_ACK, NULL, 0);
                    
                    // 调用接收回调
                    if (pcb->recv_callback) {
                        pcb->recv_callback(pcb, pcb->callback_arg);
                    }
                    
                    netbuf_free(buf);
                    return;
                } else if (TCP_SEQ_GT(seq, pcb->rcv_nxt)) {
                    // 乱序数据，发送重复 ACK
                    spinlock_unlock_irqrestore(&tcp_lock, irq_state);
                    tcp_send_segment(pcb, TCP_FLAG_ACK, NULL, 0);
                    netbuf_free(buf);
                    return;
                }
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
                        // 启动 TIME_WAIT 定时器 (2MSL = 60秒)
                        pcb->timer_time_wait = (uint32_t)timer_get_uptime_ms() + TCP_TIME_WAIT_TIMEOUT;
                        tcp_free_unacked(pcb);
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
    
    // 初始化拥塞控制
    pcb->cwnd = pcb->mss;               // 初始拥塞窗口为 1 个 MSS
    pcb->ssthresh = 65535;              // 初始慢启动阈值为最大值
    
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
    
    // 释放未确认队列
    tcp_free_unacked(pcb);
    
    // 释放乱序队列
    tcp_free_ooseq(pcb);
    
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

int tcp_pcb_list_dump(char *buf, size_t size) {
    int len = 0;
    bool to_buf = (buf != NULL && size > 0);
    
    // 辅助宏：输出到缓冲区或控制台
    #define OUTPUT(fmt, ...) do { \
        if (to_buf) { \
            len += ksnprintf(buf + len, size - (size_t)len, fmt, ##__VA_ARGS__); \
        } else { \
            kprintf(fmt, ##__VA_ARGS__); \
        } \
    } while(0)
    
    bool irq_state;
    spinlock_lock_irqsave(&tcp_lock, &irq_state);
    
    // 表头
    OUTPUT("TCP Connections:\n");
    OUTPUT("Proto  Local Address          Remote Address         State\n");
    OUTPUT("--------------------------------------------------------------------------------\n");
    
    // 打印监听连接
    for (tcp_pcb_t *pcb = tcp_listen_pcbs; pcb != NULL; pcb = pcb->next) {
        if (to_buf && len >= (int)size - 100) break;
        
        char local_ip_str[16];
        if (pcb->local_ip == 0) {
            strcpy(local_ip_str, "0.0.0.0");
        } else {
            ip_to_str(pcb->local_ip, local_ip_str);
        }
        
        OUTPUT("tcp    %s:%-5u          0.0.0.0:*              %s\n",
               local_ip_str, pcb->local_port, tcp_state_name(pcb->state));
    }
    
    // 打印活动连接
    for (tcp_pcb_t *pcb = tcp_pcbs; pcb != NULL; pcb = pcb->next) {
        if (to_buf && len >= (int)size - 100) break;
        if (pcb->state == TCP_CLOSED || pcb->state == TCP_LISTEN) {
            continue;
        }
        
        char local_ip_str[16], remote_ip_str[16];
        if (pcb->local_ip == 0) {
            strcpy(local_ip_str, "0.0.0.0");
        } else {
            ip_to_str(pcb->local_ip, local_ip_str);
        }
        if (pcb->remote_ip == 0) {
            strcpy(remote_ip_str, "0.0.0.0");
        } else {
            ip_to_str(pcb->remote_ip, remote_ip_str);
        }
        
        OUTPUT("tcp    %s:%-5u  %s:%-5u  %s\n",
               local_ip_str, pcb->local_port,
               remote_ip_str, pcb->remote_port,
               tcp_state_name(pcb->state));
    }
    
    spinlock_unlock_irqrestore(&tcp_lock, irq_state);
    
    #undef OUTPUT
    return len;
}

/**
 * @brief TCP 定时器处理（需要定期调用）
 * 
 * 处理：
 * - 重传定时器：超时重传未确认的段
 * - TIME_WAIT 定时器：等待 2MSL 后关闭连接
 */
void tcp_timer(void) {
    uint32_t now = (uint32_t)timer_get_uptime_ms();
    
    bool irq_state;
    spinlock_lock_irqsave(&tcp_lock, &irq_state);
    
    // 遍历所有活动 PCB
    tcp_pcb_t *pcb = tcp_pcbs;
    while (pcb != NULL) {
        tcp_pcb_t *next = pcb->next;  // 保存下一个，因为当前可能被删除
        
        // 处理重传定时器
        if (pcb->timer_retransmit != 0 && now >= pcb->timer_retransmit) {
            tcp_segment_t *seg = pcb->unacked;
            if (seg) {
                if (seg->retries >= TCP_MAX_RETRIES) {
                    // 重传次数过多，中止连接
                    LOG_WARN_MSG("tcp: Max retries exceeded for port %u, aborting connection\n",
                                 pcb->local_port);
                    pcb->state = TCP_CLOSED;
                    tcp_free_unacked(pcb);
                    tcp_free_ooseq(pcb);
                    
                    if (pcb->error_callback) {
                        spinlock_unlock_irqrestore(&tcp_lock, irq_state);
                        pcb->error_callback(pcb, -1, pcb->callback_arg);
                        spinlock_lock_irqsave(&tcp_lock, &irq_state);
                    }
                } else {
                    // 重传
                    LOG_DEBUG_MSG("tcp: Retransmit seq=%u, retry=%d, rto=%u\n",
                                  seg->seq, seg->retries + 1, pcb->rto);
                    
                    // 暂时解锁以发送段
                    spinlock_unlock_irqrestore(&tcp_lock, irq_state);
                    
                    // 重新发送段（不通过 tcp_send_segment 以避免再次加入队列）
                    netdev_t *dev = netdev_get_default();
                    if (dev) {
                        uint32_t tcp_len = TCP_HEADER_MIN_LEN + seg->data_len;
                        netbuf_t *buf = netbuf_alloc(tcp_len);
                        if (buf) {
                            uint8_t *pkt = netbuf_put(buf, tcp_len);
                            tcp_header_t *tcp = (tcp_header_t *)pkt;
                            
                            tcp->src_port = htons(pcb->local_port);
                            tcp->dst_port = htons(pcb->remote_port);
                            tcp->seq_num = htonl(seg->seq);
                            tcp->ack_num = htonl(pcb->rcv_nxt);
                            tcp->data_offset = (TCP_HEADER_MIN_LEN / 4) << 4;
                            tcp->flags = seg->flags | TCP_FLAG_ACK;
                            tcp->window = htons(pcb->rcv_wnd);
                            tcp->checksum = 0;
                            tcp->urgent_ptr = 0;
                            
                            if (seg->data && seg->data_len > 0) {
                                memcpy(pkt + TCP_HEADER_MIN_LEN, seg->data, seg->data_len);
                            }
                            
                            uint32_t src_ip = (pcb->local_ip != 0) ? pcb->local_ip : dev->ip_addr;
                            tcp->checksum = tcp_checksum(src_ip, pcb->remote_ip, tcp, tcp_len);
                            
                            int ret = ip_output(dev, buf, pcb->remote_ip, IP_PROTO_TCP);
                            if (ret < 0) {
                                netbuf_free(buf);
                            }
                        }
                    }
                    
                    spinlock_lock_irqsave(&tcp_lock, &irq_state);
                    
                    // 更新重传信息
                    seg->retries++;
                    
                    // 指数退避
                    uint32_t backoff_rto = pcb->rto * (1 << seg->retries);
                    if (backoff_rto > TCP_RTO_MAX) backoff_rto = TCP_RTO_MAX;
                    
                    seg->retransmit_time = now + backoff_rto;
                    pcb->timer_retransmit = seg->retransmit_time;
                    
                    // 拥塞控制：超时重传时减小窗口
                    pcb->ssthresh = pcb->cwnd / 2;
                    if (pcb->ssthresh < 2 * pcb->mss) {
                        pcb->ssthresh = 2 * pcb->mss;
                    }
                    pcb->cwnd = pcb->mss;  // 重新慢启动
                }
            }
        }
        
        // 处理 TIME_WAIT 定时器
        if (pcb->state == TCP_TIME_WAIT && 
            pcb->timer_time_wait != 0 && now >= pcb->timer_time_wait) {
            LOG_DEBUG_MSG("tcp: TIME_WAIT timeout for port %u, closing connection\n",
                          pcb->local_port);
            pcb->state = TCP_CLOSED;
            pcb->timer_time_wait = 0;
        }
        
        pcb = next;
    }
    
    spinlock_unlock_irqrestore(&tcp_lock, irq_state);
}

