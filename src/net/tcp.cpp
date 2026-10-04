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
static sync::Spinlock tcp_lock;

static tcp_pcb_t *tcp_pcb_alloc(void);
static void tcp_pcb_link_locked(tcp_pcb_t *pcb);

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
static int tcp_xmit(tcp_pcb_t *pcb, uint32_t seq, uint8_t flags,
                    const uint8_t *data, uint32_t len);
static void tcp_output(tcp_pcb_t *pcb);
static void tcp_pcb_release(tcp_pcb_t *pcb);
static void tcp_free_unacked(tcp_pcb_t *pcb);
static void tcp_free_ooseq(tcp_pcb_t *pcb);

/**
 * @brief 生成初始序列号
 */
static uint32_t tcp_gen_isn(void) {
    // 简单实现：使用计时器值
    tcp_isn += (uint32_t)drivers::Timer::get_uptime_ms() * 250000;
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
    
    seg->send_time = (uint32_t)drivers::Timer::get_uptime_ms();
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
    uint32_t now = (uint32_t)drivers::Timer::get_uptime_ms();
    
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
 * @return true 表示收到第 3 个重复 ACK：调用者应在解锁后用 tcp_xmit()
 *         以原序列号重传 pcb->unacked 的第一个段
 */
static bool tcp_dup_ack(tcp_pcb_t *pcb) {
    pcb->dup_ack_count++;

    // 收到 3 个重复 ACK，触发快速重传
    if (pcb->dup_ack_count != 3 || !pcb->unacked) {
        return false;
    }

    tcp_segment_t *seg = pcb->unacked;

    // 快速重传：拥塞控制
    pcb->ssthresh = pcb->cwnd / 2;
    if (pcb->ssthresh < 2u * pcb->mss) {
        pcb->ssthresh = 2u * pcb->mss;
    }
    pcb->cwnd = pcb->ssthresh + 3u * pcb->mss;

    LOG_DEBUG_MSG("tcp: Fast retransmit seq=%u\n", seg->seq);

    seg->retries++;
    seg->retransmit_time = (uint32_t)drivers::Timer::get_uptime_ms() + pcb->rto;
    pcb->timer_retransmit = seg->retransmit_time;
    return true;
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
    
    // 整个段必须落在接收窗口内（窗口 = 接收缓冲区的剩余空间）
    uint32_t offset = seq - pcb->rcv_nxt;
    if (offset >= pcb->rcv_wnd || len > pcb->rcv_wnd - offset) {
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
 * @brief 接收缓冲区的剩余空间
 */
static uint32_t tcp_recv_space(tcp_pcb_t *pcb) {
    if (!pcb->recv_buf || pcb->recv_len >= pcb->recv_buf_size) {
        return 0;
    }
    return pcb->recv_buf_size - pcb->recv_len;
}

/**
 * @brief 通告窗口跟随接收缓冲区的剩余空间
 */
static void tcp_update_rcv_wnd(tcp_pcb_t *pcb) {
    pcb->rcv_wnd = tcp_recv_space(pcb);
}

/**
 * @brief 尝试从乱序队列合并连续数据
 */
static void tcp_ooseq_merge(tcp_pcb_t *pcb) {
    while (pcb->ooseq) {
        tcp_ooseq_t *seg = pcb->ooseq;

        // 检查是否可以合并
        if (seg->seq == pcb->rcv_nxt) {
            // 放不下就留在队列里：没有存下来的数据不能确认
            if (seg->len > tcp_recv_space(pcb)) {
                break;
            }
            memcpy(pcb->recv_buf + pcb->recv_len, seg->data, seg->len);
            pcb->recv_len += seg->len;
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
 *
 * rcv_nxt 只按实际存入接收缓冲区的字节数前进：缓冲区放不下的部分既不保存
 * 也不确认，对端会重传。
 */
static void tcp_process_data(tcp_pcb_t *pcb, uint32_t seq, uint8_t *data, uint32_t data_len) {
    if (data_len == 0) return;

    // 段的前一部分已经收到过（重传与新数据重叠）：只取新的部分
    if (TCP_SEQ_LT(seq, pcb->rcv_nxt)) {
        uint32_t skip = pcb->rcv_nxt - seq;
        if (skip >= data_len) {
            return;  // 完全是重复数据
        }
        data += skip;
        data_len -= skip;
        seq = pcb->rcv_nxt;
    }

    if (seq == pcb->rcv_nxt) {
        // 按序到达，复制能放下的部分到接收缓冲区
        uint32_t copy_len = data_len;
        uint32_t space = tcp_recv_space(pcb);
        if (copy_len > space) {
            copy_len = space;
        }
        if (copy_len > 0) {
            memcpy(pcb->recv_buf + pcb->recv_len, data, copy_len);
            pcb->recv_len += copy_len;
            pcb->rcv_nxt += copy_len;
        }

        // 尝试合并乱序队列
        if (copy_len == data_len) {
            tcp_ooseq_merge(pcb);
        }
    } else {
        // 乱序到达，加入乱序队列
        tcp_ooseq_add(pcb, seq, data, data_len);
    }

    tcp_update_rcv_wnd(pcb);
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
 * @brief 构造并发送一个 TCP 段，序列号由调用者给定
 *
 * 只负责把段放到线上：不修改 snd_nxt，也不碰重传队列。首次发送
 * （tcp_send_segment）、超时重传和快速重传都走这里，所以重传的段带的
 * 一定是它原来的序列号。
 */
static int tcp_xmit(tcp_pcb_t *pcb, uint32_t seq, uint8_t flags,
                    const uint8_t *data, uint32_t len) {
    net::Netdev *dev = net::Netdev::get_default();
    if (!dev) {
        return -1;
    }

    // SYN 段带 MSS 选项
    uint32_t opt_len = (flags & TCP_FLAG_SYN) ? 4 : 0;
    uint32_t hdr_len = TCP_HEADER_MIN_LEN + opt_len;
    uint32_t tcp_len = hdr_len + len;

    // 分配缓冲区
    net::Netbuf *buf = net::Netbuf::alloc(tcp_len);
    if (!buf) {
        return -1;
    }

    // 填充 TCP 段
    uint8_t *pkt = net::Netbuf::put(buf, tcp_len);
    if (!pkt) {
        net::Netbuf::free(buf);
        return -1;
    }
    tcp_header_t *tcp = (tcp_header_t *)pkt;

    tcp->src_port = htons(pcb->local_port);
    tcp->dst_port = htons(pcb->remote_port);
    tcp->seq_num = htonl(seq);
    tcp->ack_num = htonl(pcb->rcv_nxt);
    tcp->data_offset = (uint8_t)((hdr_len / 4) << 4);
    tcp->flags = flags;
    uint32_t wnd = (pcb->rcv_wnd > 0xFFFF) ? 0xFFFF : pcb->rcv_wnd;
    tcp->window = htons((uint16_t)wnd);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;

    if (opt_len) {
        pkt[TCP_HEADER_MIN_LEN + 0] = 2;  // kind = MSS
        pkt[TCP_HEADER_MIN_LEN + 1] = 4;  // length
        pkt[TCP_HEADER_MIN_LEN + 2] = (uint8_t)(TCP_DEFAULT_MSS >> 8);
        pkt[TCP_HEADER_MIN_LEN + 3] = (uint8_t)(TCP_DEFAULT_MSS & 0xFF);
    }

    // 复制数据
    if (data && len > 0) {
        memcpy(pkt + hdr_len, data, len);
    }

    // 计算校验和
    uint32_t src_ip = (pcb->local_ip != 0) ? pcb->local_ip : dev->ip_addr;
    tcp->checksum = net::Tcp::checksum(src_ip, pcb->remote_ip, tcp, (uint16_t)tcp_len);

    // 记录发送时间
    pcb->last_send_time = (uint32_t)drivers::Timer::get_uptime_ms();

    // 发送
    int ret = net::Ip::output(dev, buf, pcb->remote_ip, IP_PROTO_TCP);
    net::Netbuf::free(buf);  // 发送只是借用 buf
    return ret;
}

/**
 * @brief 以 snd_nxt 为序列号发送一个新段
 *
 * 占用序列号的段（SYN、FIN 或带数据）先进入重传队列再发送：入队失败时
 * 直接返回错误，snd_nxt 不动；入队之后即使这一次没发出去（设备忙、ARP
 * 还没解析完），重传定时器也会补发，序列空间不会留下空洞。
 *
 * @return 0 成功（已发送或已排入重传队列），-1 失败
 */
static int tcp_send_segment(tcp_pcb_t *pcb, uint8_t flags, uint8_t *data, uint32_t len) {
    if (!net::Netdev::get_default()) {
        return -1;
    }

    uint32_t seq = pcb->snd_nxt;

    // SYN 和 FIN 各占一个序列号
    uint32_t seq_len = len;
    if (flags & TCP_FLAG_SYN) seq_len++;
    if (flags & TCP_FLAG_FIN) seq_len++;

    if (seq_len == 0) {
        // 纯 ACK：不重传，丢了就丢了
        return tcp_xmit(pcb, seq, flags, NULL, 0);
    }

    if (tcp_queue_unacked(pcb, seq, flags, data, len) < 0) {
        return -1;
    }
    pcb->snd_nxt += seq_len;

    tcp_xmit(pcb, seq, flags, data, len);
    return 0;
}

/**
 * @brief 把发送缓冲区里的数据装成段发出去
 *
 * 在 write() 之后以及每次收到新的 ACK/窗口更新时调用。受对端通告窗口、
 * 拥塞窗口和 MSS 限制；窗口为 0 且没有在途数据时发送 1 字节的窗口探测。
 * 调用者已请求关闭（fin_pending）时，缓冲区排空后发送 FIN。
 */
static void tcp_output(tcp_pcb_t *pcb) {
    bool can_send_data = (pcb->state == TCP_ESTABLISHED || pcb->state == TCP_CLOSE_WAIT);

    while (can_send_data && pcb->send_len > 0) {
        uint32_t in_flight = pcb->snd_nxt - pcb->snd_una;
        uint32_t wnd = (pcb->snd_wnd < pcb->cwnd) ? pcb->snd_wnd : pcb->cwnd;

        uint32_t room;
        if (in_flight < wnd) {
            room = wnd - in_flight;
        } else if (in_flight == 0) {
            room = 1;  // 零窗口探测
        } else {
            break;     // 窗口已满，等 ACK
        }

        uint32_t n = pcb->send_len;
        if (n > pcb->mss) n = pcb->mss;
        if (n > room) n = room;

        if (tcp_send_segment(pcb, TCP_FLAG_ACK | TCP_FLAG_PSH, pcb->send_buf, n) < 0) {
            break;  // 没有入队：数据留在发送缓冲区，下次再试
        }

        // 段已进入重传队列，从发送缓冲区移除
        if (n < pcb->send_len) {
            memmove(pcb->send_buf, pcb->send_buf + n, pcb->send_len - n);
        }
        pcb->send_len -= n;
    }

    if (pcb->fin_pending && pcb->send_len == 0 &&
        (can_send_data || pcb->state == TCP_SYN_RECEIVED)) {
        tcp_state_t next = (pcb->state == TCP_CLOSE_WAIT) ? TCP_LAST_ACK : TCP_FIN_WAIT_1;
        if (tcp_send_segment(pcb, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0) == 0) {
            pcb->fin_pending = false;
            pcb->state = next;
        }
    }
}

/**
 * @brief 解析 SYN 段里的 MSS 选项
 */
static void tcp_parse_mss(tcp_pcb_t *pcb, tcp_header_t *tcp, uint8_t hdr_len) {
    uint8_t *opt = (uint8_t *)tcp + TCP_HEADER_MIN_LEN;
    uint8_t *end = (uint8_t *)tcp + hdr_len;

    while (opt < end) {
        uint8_t kind = opt[0];
        if (kind == 0) {        // 选项表结束
            break;
        }
        if (kind == 1) {        // NOP
            opt++;
            continue;
        }
        if (opt + 1 >= end) {
            break;
        }
        uint8_t len = opt[1];
        if (len < 2 || opt + len > end) {
            break;
        }
        if (kind == 2 && len == 4) {
            uint16_t mss = (uint16_t)((opt[2] << 8) | opt[3]);
            if (mss >= 64 && mss < pcb->mss) {
                pcb->mss = mss;
            }
        }
        opt += len;
    }
}

/**
 * 把一个握手未完成的子 PCB 从监听 PCB 的 pending 队列摘下。
 * 调用者必须持有 tcp_lock。
 */
static void tcp_pending_remove_locked(tcp_pcb_t *pcb) {
    tcp_pcb_t *listen = pcb->listen_pcb;
    if (!listen) {
        return;
    }
    tcp_pcb_t **pp = &listen->pending_queue;
    while (*pp && *pp != pcb) {
        pp = &(*pp)->queue_next;
    }
    if (*pp == pcb) {
        *pp = pcb->queue_next;
        listen->pending_count--;
    }
    pcb->queue_next = NULL;
    pcb->listen_pcb = NULL;
}

/**
 * 把 PCB 从活动链表摘下。调用者必须持有 tcp_lock。
 */
static void tcp_pcb_unlink_locked(tcp_pcb_t *pcb) {
    tcp_pcb_t **pp = &tcp_pcbs;
    while (*pp && *pp != pcb) {
        pp = &(*pp)->next;
    }
    if (*pp == pcb) {
        *pp = pcb->next;
    }
    pcb->next = NULL;
}

/**
 * @brief 发送 RST 段
 */
static void tcp_send_rst(uint32_t src_ip, uint32_t dst_ip,
                         uint16_t src_port, uint16_t dst_port,
                         uint32_t seq, uint32_t ack, bool ack_valid) {
    net::Netdev *dev = net::Netdev::get_default();
    if (!dev) {
        return;
    }
    
    // 分配缓冲区
    net::Netbuf *buf = net::Netbuf::alloc(TCP_HEADER_MIN_LEN);
    if (!buf) {
        return;
    }
    
    // 填充 TCP 段
    uint8_t *pkt = net::Netbuf::put(buf, TCP_HEADER_MIN_LEN);
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
    tcp->checksum = net::Tcp::checksum(src_ip, dst_ip, tcp, TCP_HEADER_MIN_LEN);
    
    // 发送
    net::Ip::output(dev, buf, dst_ip, IP_PROTO_TCP);
    net::Netbuf::free(buf);  // 发送只是借用 buf
}

void net::Tcp::init() {
    tcp_lock.init();
    tcp_pcbs = NULL;
    tcp_listen_pcbs = NULL;
    next_ephemeral_port = TCP_EPHEMERAL_PORT_MIN;
    tcp_isn = (uint32_t)drivers::Timer::get_uptime_ms();
    
    LOG_INFO_MSG("tcp: TCP protocol initialized\n");
}

void net::Tcp::input(net::Netdev *dev, net::Netbuf *buf, uint32_t src_ip, uint32_t dst_ip) {
    if (!dev || !buf) {
        return;
    }
    
    // 检查报文长度
    if (buf->len < TCP_HEADER_MIN_LEN) {
        LOG_WARN_MSG("tcp: Packet too short (%u bytes)\n", buf->len);
        net::Netbuf::free(buf);
        return;
    }
    
    tcp_header_t *tcp = (tcp_header_t *)buf->data;
    buf->transport_header = tcp;
    
    // 获取头部长度
    uint8_t hdr_len = net::Tcp::header_len(tcp);
    if (hdr_len < TCP_HEADER_MIN_LEN || hdr_len > buf->len) {
        LOG_WARN_MSG("tcp: Invalid header length %u\n", hdr_len);
        net::Netbuf::free(buf);
        return;
    }
    
    // 验证校验和
    uint16_t orig_checksum = tcp->checksum;
    tcp->checksum = 0;
    uint16_t calc_checksum = net::Tcp::checksum(src_ip, dst_ip, tcp, buf->len);
    
    if (calc_checksum != orig_checksum) {
        LOG_WARN_MSG("tcp: Invalid checksum\n");
        net::Netbuf::free(buf);
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
    
    uint32_t window = ntohs(tcp->window);

    // 查找匹配的 PCB
    bool irq_state;
    tcp_lock.lock_irqsave(irq_state);

    tcp_pcb_t *pcb = tcp_find_pcb(dst_ip, dst_port, src_ip, src_port);

    if (!pcb) {
        tcp_lock.unlock_irqrestore(irq_state);

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

        net::Netbuf::free(buf);
        return;
    }

    // 持有 tcp_lock（自旋锁）期间只更新连接状态；发送会走到驱动（Mutex），
    // 回调也可能发送，所以要做的事先记在下面这些变量里，解锁后再执行。
    bool send_ack = false;           // 回一个纯 ACK
    bool send_rst = false;           // 对这个段回 RST
    bool resend_syn_ack = false;     // 同时打开：以原序列号重发 SYN，并带上 ACK
    bool fast_rexmit = false;        // 快速重传第一个未确认段
    bool run_output = false;         // 窗口可能打开了，继续发送缓冲区里的数据
    bool data_received = false;      // 有新数据进入接收缓冲区
    bool notify_error = false;       // 连接被对端重置
    tcp_pcb_t *syn_ack_pcb = NULL;   // 新建的半连接：发送 SYN+ACK
    tcp_pcb_t *accepted = NULL;      // 刚完成握手、进入 accept 队列的连接
    tcp_pcb_t *accept_listener = NULL;
    tcp_pcb_t *dead_pcb = NULL;      // 已从所有链表摘下、需要释放的半连接

    // 根据状态处理
    switch (pcb->state) {
        case TCP_LISTEN: {
            // 只处理 SYN
            if (flags & TCP_FLAG_RST) {
                break;
            }
            if (flags & TCP_FLAG_ACK) {
                send_rst = true;
                break;
            }
            if (flags & TCP_FLAG_SYN) {
                // backlog 同时约束握手中的连接和等待 accept 的连接
                if (pcb->pending_count + pcb->accept_count >= pcb->backlog) {
                    break;
                }

                // 创建新的 PCB 用于这个连接
                // 这里已经持有 tcp_lock：不能调用会再次加锁的 pcb_new()
                tcp_pcb_t *new_pcb = tcp_pcb_alloc();
                if (!new_pcb) {
                    break;
                }
                tcp_pcb_link_locked(new_pcb);

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
                new_pcb->snd_wnd = window;
                new_pcb->listen_pcb = pcb;
                tcp_parse_mss(new_pcb, tcp, hdr_len);

                // 加入待处理队列
                new_pcb->queue_next = pcb->pending_queue;
                pcb->pending_queue = new_pcb;
                pcb->pending_count++;

                syn_ack_pcb = new_pcb;
            }
            break;
        }

        case TCP_SYN_SENT: {
            // 等待 SYN+ACK
            if ((flags & TCP_FLAG_ACK) && ack != pcb->snd_nxt) {
                // ACK 不正确
                if (!(flags & TCP_FLAG_RST)) {
                    send_rst = true;
                }
                break;
            }
            if (flags & TCP_FLAG_RST) {
                if (flags & TCP_FLAG_ACK) {
                    pcb->state = TCP_CLOSED;
                    tcp_free_unacked(pcb);
                    notify_error = true;
                }
                break;
            }
            if (flags & TCP_FLAG_SYN) {
                pcb->irs = seq;
                pcb->rcv_nxt = seq + 1;
                pcb->snd_wnd = window;
                tcp_parse_mss(pcb, tcp, hdr_len);
                if (flags & TCP_FLAG_ACK) {
                    // 我们的 SYN 已被确认：把它从重传队列里拿掉
                    pcb->snd_una = ack;
                    tcp_ack_received(pcb, ack);
                }

                if (TCP_SEQ_GT(pcb->snd_una, pcb->iss)) {
                    // 连接建立
                    pcb->state = TCP_ESTABLISHED;
                    send_ack = true;
                } else {
                    // 同时打开
                    pcb->state = TCP_SYN_RECEIVED;
                    resend_syn_ack = true;
                }
            }
            break;
        }

        case TCP_SYN_RECEIVED: {
            if (flags & TCP_FLAG_RST) {
                if (pcb->listen_pcb) {
                    // 半连接被对端放弃：从 pending 队列和活动链表摘下并释放
                    tcp_pending_remove_locked(pcb);
                    tcp_pcb_unlink_locked(pcb);
                    dead_pcb = pcb;
                } else {
                    pcb->state = TCP_CLOSED;
                    tcp_free_unacked(pcb);
                    notify_error = true;
                }
                break;
            }
            if ((flags & TCP_FLAG_ACK) && ack == pcb->snd_nxt) {
                // 我们的 SYN(+ACK) 已被确认：把它从重传队列里拿掉
                pcb->snd_una = ack;
                tcp_ack_received(pcb, ack);
                pcb->snd_wnd = window;
                pcb->state = TCP_ESTABLISHED;

                // 如果是被动连接，从 pending 队列移到 accept 队列
                if (pcb->listen_pcb) {
                    tcp_pcb_t *listen = pcb->listen_pcb;
                    tcp_pending_remove_locked(pcb);

                    pcb->queue_next = listen->accept_queue;
                    listen->accept_queue = pcb;
                    listen->accept_count++;

                    accepted = pcb;
                    accept_listener = listen;
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
                notify_error = true;
                break;
            }

            // 处理 ACK
            if (flags & TCP_FLAG_ACK) {
                if (TCP_SEQ_GT(ack, pcb->snd_una) && TCP_SEQ_LEQ(ack, pcb->snd_nxt)) {
                    // 新的 ACK，处理确认
                    pcb->snd_una = ack;
                    tcp_ack_received(pcb, ack);
                    pcb->snd_wnd = window;
                    run_output = true;
                } else if (ack == pcb->snd_una) {
                    // 只有不带数据、不带 SYN/FIN、窗口没变的段才算重复 ACK；
                    // 对端在我们有未确认数据时发来的普通数据段不算
                    bool is_dup = pcb->unacked && data_len == 0 &&
                                  !(flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) &&
                                  window == pcb->snd_wnd;
                    if (is_dup) {
                        fast_rexmit = tcp_dup_ack(pcb);
                    } else if (window != pcb->snd_wnd) {
                        // 窗口更新
                        pcb->snd_wnd = window;
                        run_output = true;
                    }
                }

                if (pcb->state == TCP_FIN_WAIT_1 && ack == pcb->snd_nxt) {
                    pcb->state = TCP_FIN_WAIT_2;
                }
            }

            // 处理数据（支持乱序）。对端发过 FIN 之后不会再有新数据。
            if (data_len > 0) {
                if (pcb->state != TCP_CLOSE_WAIT) {
                    uint32_t old_rcv_nxt = pcb->rcv_nxt;
                    tcp_process_data(pcb, seq, data, data_len);
                    data_received = (pcb->rcv_nxt != old_rcv_nxt);
                }
                // 带数据的段总要回 ACK：按序数据确认新的 rcv_nxt，
                // 乱序、重复或放不下的数据让对端知道我们期望的序列号
                send_ack = true;
            }

            // 处理 FIN
            if (flags & TCP_FLAG_FIN) {
                // FIN 之前的数据全部收下之后，FIN 才生效
                if (pcb->state != TCP_CLOSE_WAIT && seq + data_len == pcb->rcv_nxt) {
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
                            pcb->timer_time_wait = (uint32_t)drivers::Timer::get_uptime_ms() + TCP_TIME_WAIT_TIMEOUT;
                            tcp_free_unacked(pcb);
                            break;
                        default:
                            break;
                    }
                }
                send_ack = true;
            }
            break;
        }

        case TCP_CLOSING: {
            if ((flags & TCP_FLAG_ACK) && ack == pcb->snd_nxt) {
                // 我们的 FIN 已被确认
                pcb->snd_una = ack;
                tcp_ack_received(pcb, ack);
                pcb->state = TCP_TIME_WAIT;
                pcb->timer_time_wait = (uint32_t)drivers::Timer::get_uptime_ms() + TCP_TIME_WAIT_TIMEOUT;
            }
            break;
        }

        case TCP_LAST_ACK: {
            if ((flags & TCP_FLAG_ACK) && ack == pcb->snd_nxt) {
                // 我们的 FIN 已被确认，连接完全关闭
                pcb->snd_una = ack;
                tcp_ack_received(pcb, ack);
                tcp_free_unacked(pcb);
                pcb->state = TCP_CLOSED;
            }
            break;
        }

        case TCP_TIME_WAIT: {
            // 对端重传 FIN：说明我们的 ACK 丢了，再确认一次
            if (flags & TCP_FLAG_FIN) {
                send_ack = true;
            }
            break;
        }

        default:
            break;
    }

    tcp_lock.unlock_irqrestore(irq_state);

    // ---- 以下不持有 tcp_lock ----

    if (dead_pcb) {
        // 半连接已从所有链表摘下，没有别的引用
        tcp_pcb_release(dead_pcb);
        net::Netbuf::free(buf);
        return;
    }

    if (send_rst) {
        tcp_send_rst(dst_ip, src_ip, dst_port, src_port, ack, 0, false);
    }

    if (syn_ack_pcb) {
        if (tcp_send_segment(syn_ack_pcb, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0) < 0) {
            // SYN+ACK 连重传队列都没进：这个半连接永远不会完成也不会超时，直接回收
            tcp_lock.lock_irqsave(irq_state);
            tcp_pending_remove_locked(syn_ack_pcb);
            tcp_pcb_unlink_locked(syn_ack_pcb);
            tcp_lock.unlock_irqrestore(irq_state);
            tcp_pcb_release(syn_ack_pcb);
        }
    }

    if (resend_syn_ack) {
        // SYN 还在重传队列里（序列号 iss），这里只是带上 ACK 再发一次
        tcp_xmit(pcb, pcb->iss, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
    }

    if (fast_rexmit && pcb->unacked) {
        // 重传必须使用段原来的序列号
        tcp_segment_t *seg = pcb->unacked;
        tcp_xmit(pcb, seg->seq, seg->flags | TCP_FLAG_ACK, seg->data, seg->data_len);
    }

    if (run_output) {
        uint32_t before = pcb->snd_nxt;
        tcp_output(pcb);
        if (pcb->snd_nxt != before) {
            send_ack = false;  // 刚发出的段已经带了最新的 ACK
        }
    }

    if (send_ack) {
        tcp_send_segment(pcb, TCP_FLAG_ACK, NULL, 0);
    }

    if (accepted && accept_listener->accept_callback) {
        accept_listener->accept_callback(accepted, accept_listener->callback_arg);
    }

    if (data_received && pcb->recv_callback) {
        pcb->recv_callback(pcb, pcb->callback_arg);
    }

    if (notify_error && pcb->error_callback) {
        pcb->error_callback(pcb, -1, pcb->callback_arg);
    }

    net::Netbuf::free(buf);
}

/**
 * 分配并初始化一个 PCB，不加入任何链表。不获取 tcp_lock。
 */
static tcp_pcb_t *tcp_pcb_alloc(void) {
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
    pcb->rcv_wnd = TCP_RECV_BUF_SIZE;   // 通告窗口 = 接收缓冲区的剩余空间
    pcb->send_buf = (uint8_t *)kmalloc(TCP_SEND_BUF_SIZE);
    pcb->send_buf_size = TCP_SEND_BUF_SIZE;
    
    if (!pcb->recv_buf || !pcb->send_buf) {
        if (pcb->recv_buf) kfree(pcb->recv_buf);
        if (pcb->send_buf) kfree(pcb->send_buf);
        kfree(pcb);
        return NULL;
    }
    
    pcb->lock.init();
    
    return pcb;
}

/**
 * 把 PCB 加入活动链表。调用者必须持有 tcp_lock。
 */
static void tcp_pcb_link_locked(tcp_pcb_t *pcb) {
    pcb->next = tcp_pcbs;
    tcp_pcbs = pcb;
}

tcp_pcb_t *net::Tcp::pcb_new() {
    tcp_pcb_t *pcb = tcp_pcb_alloc();
    if (!pcb) {
        return NULL;
    }
    
    sync::SpinlockIrqGuard guard(tcp_lock);
    tcp_pcb_link_locked(pcb);
    
    return pcb;
}

/**
 * 释放一个已经不在任何链表上的 PCB 及其队列、缓冲区。
 */
static void tcp_pcb_release(tcp_pcb_t *pcb) {
    // 释放未确认队列
    tcp_free_unacked(pcb);

    // 释放乱序队列
    tcp_free_ooseq(pcb);

    // 释放缓冲区
    if (pcb->recv_buf) kfree(pcb->recv_buf);
    if (pcb->send_buf) kfree(pcb->send_buf);

    kfree(pcb);
}

void net::Tcp::pcb_free(tcp_pcb_t *pcb) {
    if (!pcb) {
        return;
    }

    // 监听 PCB 名下的子连接（握手中的和等待 accept 的）：没有 socket 拥有它们，
    // 监听 PCB 一释放就再没人能接走，必须一起回收，否则它们的 listen_pcb 会悬空
    tcp_pcb_t *children = NULL;

    {
        sync::SpinlockIrqGuard guard(tcp_lock);
        // 从活动链表移除
        tcp_pcb_unlink_locked(pcb);

        // 从监听链表移除
        tcp_pcb_t **pp = &tcp_listen_pcbs;
        while (*pp && *pp != pcb) {
            pp = &(*pp)->next;
        }
        if (*pp == pcb) {
            *pp = pcb->next;
        }

        // 自己是别人 pending 队列里的半连接
        tcp_pending_remove_locked(pcb);

        tcp_pcb_t *queues[2] = { pcb->pending_queue, pcb->accept_queue };
        for (int i = 0; i < 2; i++) {
            tcp_pcb_t *child = queues[i];
            while (child) {
                tcp_pcb_t *next = child->queue_next;
                tcp_pcb_unlink_locked(child);
                child->listen_pcb = NULL;
                child->queue_next = children;
                children = child;
                child = next;
            }
        }
        pcb->pending_queue = NULL;
        pcb->accept_queue = NULL;
        pcb->pending_count = 0;
        pcb->accept_count = 0;
    }

    // 锁外：通知对端并释放子连接
    while (children) {
        tcp_pcb_t *child = children;
        children = child->queue_next;
        if (child->state != TCP_CLOSED) {
            tcp_send_rst(child->local_ip, child->remote_ip,
                         child->local_port, child->remote_port,
                         child->snd_nxt, child->rcv_nxt, true);
        }
        tcp_pcb_release(child);
    }

    tcp_pcb_release(pcb);
}

int net::Tcp::bind(tcp_pcb_t *pcb, uint32_t local_ip, uint16_t local_port) {
    if (!pcb || pcb->state != TCP_CLOSED) {
        return -1;
    }
    
    sync::SpinlockIrqGuard guard(tcp_lock);
    
    // 检查端口是否已被使用
    for (tcp_pcb_t *p = tcp_pcbs; p != NULL; p = p->next) {
        if (p != pcb && p->local_port == local_port) {
            if (p->local_ip == 0 || local_ip == 0 || p->local_ip == local_ip) {
                return -1;
            }
        }
    }
    for (tcp_pcb_t *p = tcp_listen_pcbs; p != NULL; p = p->next) {
        if (p != pcb && p->local_port == local_port) {
            if (p->local_ip == 0 || local_ip == 0 || p->local_ip == local_ip) {
                return -1;
            }
        }
    }
    
    pcb->local_ip = local_ip;
    pcb->local_port = local_port;
    
    return 0;
}

int net::Tcp::listen(tcp_pcb_t *pcb, int backlog) {
    if (!pcb || pcb->state != TCP_CLOSED) {
        return -1;
    }
    
    if (pcb->local_port == 0) {
        return -1;  // 必须先绑定
    }
    
    sync::SpinlockIrqGuard guard(tcp_lock);
    
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
    
    return 0;
}

int net::Tcp::connect(tcp_pcb_t *pcb, uint32_t remote_ip, uint16_t remote_port) {
    if (!pcb || pcb->state != TCP_CLOSED) {
        return -1;
    }
    
    // 如果未绑定，分配临时端口
    if (pcb->local_port == 0) {
        pcb->local_port = net::Tcp::alloc_port();
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
    if (tcp_send_segment(pcb, TCP_FLAG_SYN, NULL, 0) < 0) {
        pcb->state = TCP_CLOSED;
        return -1;
    }
    return 0;
}

tcp_pcb_t *net::Tcp::accept(tcp_pcb_t *pcb) {
    if (!pcb || pcb->state != TCP_LISTEN) {
        return NULL;
    }
    
    sync::SpinlockIrqGuard guard(tcp_lock);
    
    tcp_pcb_t *new_pcb = pcb->accept_queue;
    if (new_pcb) {
        pcb->accept_queue = new_pcb->queue_next;
        new_pcb->queue_next = NULL;
        pcb->accept_count--;
    }
    
    return new_pcb;
}

int net::Tcp::write(tcp_pcb_t *pcb, const void *data, uint32_t len) {
    if (!pcb || !data || len == 0) {
        return -1;
    }

    if (pcb->state != TCP_ESTABLISHED && pcb->state != TCP_CLOSE_WAIT) {
        return -1;
    }

    if (pcb->fin_pending) {
        return -1;  // 已经请求关闭发送方向
    }

    // 复制数据到发送缓冲区
    uint32_t copy_len = len;
    if (copy_len > pcb->send_buf_size - pcb->send_len) {
        copy_len = pcb->send_buf_size - pcb->send_len;
    }

    if (copy_len == 0) {
        return 0;  // 缓冲区满
    }

    memcpy(pcb->send_buf + pcb->send_len, data, copy_len);
    pcb->send_len += copy_len;

    // 在窗口允许的范围内尽量发送；剩下的留在发送缓冲区，
    // 收到 ACK 后由 tcp_output() 继续发送
    tcp_output(pcb);

    return copy_len;
}

int net::Tcp::read(tcp_pcb_t *pcb, void *buf, uint32_t len) {
    if (!pcb || !buf || len == 0) {
        return -1;
    }

    if (pcb->recv_len == 0) {
        if (pcb->state == TCP_CLOSE_WAIT || pcb->state == TCP_CLOSED) {
            return 0;  // 连接关闭
        }
        return -1;  // 暂无数据
    }

    uint32_t old_space = tcp_recv_space(pcb);

    // 复制数据
    uint32_t copy_len = len;
    if (copy_len > pcb->recv_len) {
        copy_len = pcb->recv_len;
    }

    memcpy(buf, pcb->recv_buf, copy_len);

    // 把没读完的数据挪到缓冲区开头，读走的空间立刻可以再用来接收
    if (copy_len < pcb->recv_len) {
        memmove(pcb->recv_buf, pcb->recv_buf + copy_len, pcb->recv_len - copy_len);
    }
    pcb->recv_len -= copy_len;
    pcb->recv_read_pos = 0;
    tcp_update_rcv_wnd(pcb);

    // 之前等待的乱序段现在可能放得下了
    uint32_t old_rcv_nxt = pcb->rcv_nxt;
    tcp_ooseq_merge(pcb);
    tcp_update_rcv_wnd(pcb);

    // 窗口从（接近）关闭重新打开，或者合并出了新数据：告诉对端
    bool reopened = (old_space < pcb->mss && pcb->rcv_wnd >= pcb->mss);
    if ((reopened || pcb->rcv_nxt != old_rcv_nxt) &&
        (pcb->state == TCP_ESTABLISHED || pcb->state == TCP_FIN_WAIT_1 ||
         pcb->state == TCP_FIN_WAIT_2)) {
        tcp_send_segment(pcb, TCP_FLAG_ACK, NULL, 0);
    }

    return copy_len;
}

int net::Tcp::close(tcp_pcb_t *pcb) {
    if (!pcb) {
        return -1;
    }

    switch (pcb->state) {
        case TCP_CLOSED:
        case TCP_LISTEN:
        case TCP_SYN_SENT:
            pcb->state = TCP_CLOSED;
            tcp_free_unacked(pcb);  // 不再重传 SYN
            break;

        case TCP_SYN_RECEIVED:
        case TCP_ESTABLISHED:
        case TCP_CLOSE_WAIT:
            // FIN 排在发送缓冲区里的数据之后：数据发完再发 FIN，
            // 状态在 FIN 发出时才转换（FIN_WAIT_1 / LAST_ACK）
            pcb->fin_pending = true;
            tcp_output(pcb);
            break;

        default:
            return -1;
    }

    return 0;
}

void net::Tcp::abort(tcp_pcb_t *pcb) {
    if (!pcb) {
        return;
    }
    
    if (pcb->state != TCP_CLOSED && pcb->state != TCP_LISTEN) {
        net::Netdev *dev = net::Netdev::get_default();
        if (dev) {
            tcp_send_rst(dev->ip_addr, pcb->remote_ip,
                        pcb->local_port, pcb->remote_port,
                        pcb->snd_nxt, 0, false);
        }
    }
    
    pcb->state = TCP_CLOSED;
}

void net::Tcp::accept_callback(tcp_pcb_t *pcb,
                         void (*callback)(tcp_pcb_t *new_pcb, void *arg),
                         void *arg) {
    if (pcb) {
        pcb->accept_callback = callback;
        pcb->callback_arg = arg;
    }
}

void net::Tcp::recv_callback(tcp_pcb_t *pcb,
                       void (*callback)(tcp_pcb_t *pcb, void *arg),
                       void *arg) {
    if (pcb) {
        pcb->recv_callback = callback;
        pcb->callback_arg = arg;
    }
}

uint16_t net::Tcp::checksum(uint32_t src_ip, uint32_t dst_ip, tcp_header_t *tcp, uint16_t len) {
    uint32_t sum = 0;
    
    // 计算伪首部校验和
    tcp_pseudo_header_t pseudo;
    pseudo.src_addr = src_ip;
    pseudo.dst_addr = dst_ip;
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTO_TCP;
    pseudo.tcp_length = htons(len);
    
    sum = net::Checksum::partial(sum, &pseudo, sizeof(pseudo));
    
    // 计算 TCP 头部和数据校验和
    sum = net::Checksum::partial(sum, tcp, len);
    
    return net::Checksum::finish(sum);
}

const char *net::Tcp::state_name(tcp_state_t state) {
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

uint16_t net::Tcp::alloc_port() {
    sync::SpinlockIrqGuard guard(tcp_lock);
    
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
            return port;
        }
        
    } while (next_ephemeral_port != start_port);
    
    return 0;
}

int net::Tcp::pcb_list_dump(char *buf, size_t size) {
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
    tcp_lock.lock_irqsave(irq_state);
    
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
            net::Ip::to_str(pcb->local_ip, local_ip_str);
        }
        
        OUTPUT("tcp    %s:%-5u          0.0.0.0:*              %s\n",
               local_ip_str, pcb->local_port, net::Tcp::state_name(pcb->state));
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
            net::Ip::to_str(pcb->local_ip, local_ip_str);
        }
        if (pcb->remote_ip == 0) {
            strcpy(remote_ip_str, "0.0.0.0");
        } else {
            net::Ip::to_str(pcb->remote_ip, remote_ip_str);
        }
        
        OUTPUT("tcp    %s:%-5u  %s:%-5u  %s\n",
               local_ip_str, pcb->local_port,
               remote_ip_str, pcb->remote_port,
               net::Tcp::state_name(pcb->state));
    }
    
    tcp_lock.unlock_irqrestore(irq_state);
    
    #undef OUTPUT
    return len;
}

/**
 * @brief TCP 定时器处理（需要定期调用，任务上下文）
 *
 * 处理：
 * - 重传定时器：超时重传未确认的段
 * - 半连接超时：SYN+ACK 重传用尽后回收半连接
 * - TIME_WAIT 定时器：等待 2MSL 后关闭连接
 */
void net::Tcp::timer() {
    uint32_t now = (uint32_t)drivers::Timer::get_uptime_ms();

    bool irq_state;
    tcp_lock.lock_irqsave(irq_state);

    // 遍历所有活动 PCB
    tcp_pcb_t *pcb = tcp_pcbs;
    while (pcb != NULL) {
        tcp_pcb_t *next = pcb->next;  // 保存下一个，因为当前可能被删除

        // 已关闭的连接不再重传
        if (pcb->state == TCP_CLOSED) {
            if (pcb->unacked) {
                tcp_free_unacked(pcb);
            }
            pcb = next;
            continue;
        }

        // 处理重传定时器
        if (pcb->timer_retransmit != 0 && (int32_t)(now - pcb->timer_retransmit) >= 0) {
            tcp_segment_t *seg = pcb->unacked;
            if (!seg) {
                pcb->timer_retransmit = 0;
            } else if (seg->retries >= TCP_MAX_RETRIES) {
                if (pcb->state == TCP_SYN_RECEIVED && pcb->listen_pcb) {
                    // 半连接一直没有完成握手：归还 backlog 名额并释放
                    LOG_DEBUG_MSG("tcp: Half-open connection on port %u timed out\n",
                                  pcb->local_port);
                    tcp_pending_remove_locked(pcb);
                    tcp_pcb_unlink_locked(pcb);
                    tcp_lock.unlock_irqrestore(irq_state);
                    tcp_pcb_release(pcb);
                    tcp_lock.lock_irqsave(irq_state);
                    pcb = next;
                    continue;
                }

                // 重传次数过多，中止连接
                LOG_WARN_MSG("tcp: Max retries exceeded for port %u, aborting connection\n",
                             pcb->local_port);
                pcb->state = TCP_CLOSED;
                tcp_free_unacked(pcb);
                tcp_free_ooseq(pcb);

                if (pcb->error_callback) {
                    tcp_lock.unlock_irqrestore(irq_state);
                    pcb->error_callback(pcb, -1, pcb->callback_arg);
                    tcp_lock.lock_irqsave(irq_state);
                }
            } else {
                // 重传
                LOG_DEBUG_MSG("tcp: Retransmit seq=%u, retry=%d, rto=%u\n",
                              seg->seq, seg->retries + 1, pcb->rto);

                // 更新重传信息
                seg->retries++;

                // 指数退避
                uint32_t backoff_rto = pcb->rto * (1u << seg->retries);
                if (backoff_rto > TCP_RTO_MAX) backoff_rto = TCP_RTO_MAX;

                seg->retransmit_time = now + backoff_rto;
                pcb->timer_retransmit = seg->retransmit_time;

                // 拥塞控制：超时重传时减小窗口
                pcb->ssthresh = pcb->cwnd / 2;
                if (pcb->ssthresh < 2u * pcb->mss) {
                    pcb->ssthresh = 2u * pcb->mss;
                }
                pcb->cwnd = pcb->mss;  // 重新慢启动

                // 主动打开时重传的是不带 ACK 的 SYN；其余的段都带 ACK
                uint8_t flags = seg->flags;
                if (pcb->state != TCP_SYN_SENT) {
                    flags |= TCP_FLAG_ACK;
                }

                // 解锁后发送（发送会走到驱动）；用段原来的序列号，不进重传队列
                tcp_lock.unlock_irqrestore(irq_state);
                tcp_xmit(pcb, seg->seq, flags, seg->data, seg->data_len);
                tcp_lock.lock_irqsave(irq_state);
            }
        }

        // 处理 TIME_WAIT 定时器
        if (pcb->state == TCP_TIME_WAIT &&
            pcb->timer_time_wait != 0 && (int32_t)(now - pcb->timer_time_wait) >= 0) {
            LOG_DEBUG_MSG("tcp: TIME_WAIT timeout for port %u, closing connection\n",
                          pcb->local_port);
            pcb->state = TCP_CLOSED;
            pcb->timer_time_wait = 0;
        }

        pcb = next;
    }

    tcp_lock.unlock_irqrestore(irq_state);
}
