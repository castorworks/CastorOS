# 阶段 17: 网络子系统增强

## 概述

本阶段将完善 CastorOS 的网络子系统，解决当前实现中的关键缺陷，提升网络通信的可靠性和功能完整性。

**📝 设计理念**：

当前网络栈已实现基本的 TCP/IP 通信功能，但在可靠性保证方面存在重大缺陷。本阶段将按照优先级分步实施，确保每个子任务都能独立测试验证。

**🎯 核心目标**：

1. **可靠性增强** - TCP 重传机制、乱序报文处理
2. **功能完善** - IP 分片重组、Socket 非阻塞 I/O
3. **用户体验** - select/poll 多路复用、诊断工具
4. **协议扩展** - DHCP 客户端、DNS 解析

---

## 目标清单

### 第一阶段：TCP 可靠性增强（必须完成）
- [x] 实现 TCP 重传定时器机制
- [x] 实现 TIME_WAIT 状态定时器
- [x] 实现乱序报文处理（接收缓冲区重组）
- [x] 实现发送缓冲区的 ACK 确认机制

### 第二阶段：IP 层增强
- [x] 实现 IP 分片接收和重组
- [x] 实现简单路由表

### 第三阶段：Socket API 完善
- [x] 实现非阻塞 I/O 模式
- [x] 实现 UDP recvfrom 源地址返回
- [ ] 实现接收/发送超时（部分实现，需要阻塞等待机制）
- [x] 实现 select() 系统调用

### 第四阶段：高级功能
- [x] 实现 TCP 拥塞控制（慢启动、拥塞避免）
- [ ] 实现 DHCP 客户端
- [ ] 实现 DNS 解析器
- [x] 添加 netstat 命令
- [x] 添加 route 命令（额外实现）

---

## 第一阶段：TCP 可靠性增强

### 1.1 TCP 重传定时器

**问题分析**：

当前 `tcp_timer()` 函数为空，TCP 发送数据后不会进行超时重传，这意味着：
- 数据包丢失后无法恢复
- 连接可能无限期卡住
- 不符合 RFC 793 规范

**设计方案**：

```
┌─────────────────────────────────────────────────────────────┐
│                    TCP 重传机制                              │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   发送数据 ──→ 加入未确认队列 ──→ 启动重传定时器            │
│       │                              │                      │
│       │                              ↓                      │
│       │           ┌──────────────────────────────┐          │
│       │           │  定时器触发（RTO 超时）       │          │
│       │           │  检查 snd_una < snd_nxt？    │          │
│       │           └──────────────────────────────┘          │
│       │                    │                                │
│       │         Yes ───────┴─────── No                      │
│       │           ↓                  ↓                      │
│       │      重传数据           无需重传                    │
│       │      RTO *= 2           停止定时器                  │
│       │      retries++                                      │
│       │           │                                         │
│       │           ↓                                         │
│       │    retries > MAX_RETRIES？                          │
│       │           │                                         │
│       │      Yes──┴──No                                     │
│       │       ↓      ↓                                      │
│       │    中止连接  继续等待                               │
│       ↓                                                     │
│   收到 ACK ──→ 更新 snd_una ──→ 从队列移除已确认数据        │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**数据结构修改**：

```c
// src/include/net/tcp.h

// 重传段结构
typedef struct tcp_segment {
    uint32_t seq;               // 段序列号
    uint32_t len;               // 段长度（包括 SYN/FIN 标志）
    uint8_t *data;              // 数据副本
    uint32_t data_len;          // 实际数据长度
    uint8_t flags;              // TCP 标志
    uint32_t send_time;         // 发送时间（用于 RTT 计算）
    uint32_t retransmit_time;   // 下次重传时间
    uint8_t retries;            // 重传次数
    struct tcp_segment *next;
} tcp_segment_t;

// TCP PCB 新增字段
typedef struct tcp_pcb {
    // ... 现有字段 ...
    
    // 重传队列
    tcp_segment_t *unacked;     // 未确认段队列
    
    // RTT 估算（Jacobson 算法）
    uint32_t srtt;              // 平滑 RTT（微秒，定点数 × 8）
    uint32_t rttvar;            // RTT 方差（微秒，定点数 × 4）
    bool rtt_measuring;         // 是否正在测量 RTT
    uint32_t rtt_seq;           // 测量 RTT 的段序列号
    
    // 定时器
    uint32_t timer_retransmit;  // 重传定时器到期时间
    uint32_t timer_time_wait;   // TIME_WAIT 定时器到期时间
} tcp_pcb_t;
```

**实现代码**：

```c
// src/net/tcp.c

// RTT 估算常量
#define TCP_RTO_MIN         200     // 最小 RTO（毫秒）
#define TCP_RTO_MAX         120000  // 最大 RTO（120秒）
#define TCP_SRTT_ALPHA      8       // SRTT 平滑因子分母
#define TCP_RTTVAR_BETA     4       // RTTVAR 平滑因子分母

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
        int32_t delta = measured_rtt - (pcb->srtt / 8);
        pcb->srtt += delta;
        if (pcb->srtt <= 0) pcb->srtt = 1;
        
        if (delta < 0) delta = -delta;
        pcb->rttvar += (delta - pcb->rttvar / 4);
        if (pcb->rttvar <= 0) pcb->rttvar = 1;
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
    
    // 开始 RTT 测量
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
}

/**
 * @brief 重传段
 */
static void tcp_retransmit(tcp_pcb_t *pcb, tcp_segment_t *seg) {
    // 重新发送段
    tcp_send_segment(pcb, seg->flags | TCP_FLAG_ACK, seg->data, seg->data_len);
    
    // 更新重传信息
    seg->retries++;
    uint32_t now = (uint32_t)timer_get_uptime_ms();
    
    // 指数退避
    uint32_t backoff_rto = pcb->rto * (1 << seg->retries);
    if (backoff_rto > TCP_RTO_MAX) backoff_rto = TCP_RTO_MAX;
    
    seg->retransmit_time = now + backoff_rto;
    
    LOG_DEBUG_MSG("tcp: Retransmit seq=%u, retry=%d, rto=%u\n",
                  seg->seq, seg->retries, backoff_rto);
}

/**
 * @brief TCP 定时器处理（需要定期调用）
 */
void tcp_timer(void) {
    uint32_t now = (uint32_t)timer_get_uptime_ms();
    
    bool irq_state;
    spinlock_lock_irqsave(&tcp_lock, &irq_state);
    
    // 遍历所有活动 PCB
    for (tcp_pcb_t *pcb = tcp_pcbs; pcb != NULL; pcb = pcb->next) {
        // 处理重传定时器
        if (pcb->timer_retransmit != 0 && now >= pcb->timer_retransmit) {
            tcp_segment_t *seg = pcb->unacked;
            if (seg) {
                if (seg->retries >= TCP_MAX_RETRIES) {
                    // 重传次数过多，中止连接
                    LOG_WARN_MSG("tcp: Max retries exceeded, aborting connection\n");
                    pcb->state = TCP_CLOSED;
                    if (pcb->error_callback) {
                        spinlock_unlock_irqrestore(&tcp_lock, irq_state);
                        pcb->error_callback(pcb, -1, pcb->callback_arg);
                        spinlock_lock_irqsave(&tcp_lock, &irq_state);
                    }
                } else {
                    // 重传
                    tcp_retransmit(pcb, seg);
                    pcb->timer_retransmit = seg->retransmit_time;
                }
            }
        }
        
        // 处理 TIME_WAIT 定时器
        if (pcb->state == TCP_TIME_WAIT && 
            pcb->timer_time_wait != 0 && now >= pcb->timer_time_wait) {
            pcb->state = TCP_CLOSED;
            LOG_DEBUG_MSG("tcp: TIME_WAIT timeout, closing connection\n");
        }
    }
    
    spinlock_unlock_irqrestore(&tcp_lock, irq_state);
}
```

**集成到系统定时器**：

```c
// src/kernel/kernel.c 或 src/drivers/timer.c

// 在定时器中断处理中添加（每 100ms 调用一次）
static uint32_t tcp_timer_counter = 0;

void timer_tick_handler(void) {
    // ... 其他处理 ...
    
    tcp_timer_counter++;
    if (tcp_timer_counter >= 10) {  // 假设 10ms 一次中断
        tcp_timer_counter = 0;
        tcp_timer();
    }
}
```

---

### 1.2 TIME_WAIT 定时器

**问题分析**：

TCP 连接关闭后需要在 TIME_WAIT 状态等待 2MSL（通常 60 秒），以确保：
1. 最后的 ACK 能被对端收到（如果丢失可以重传）
2. 防止旧连接的延迟包影响新连接

**设计方案**：

```c
// 在进入 TIME_WAIT 状态时启动定时器
case TCP_FIN_WAIT_2:
    pcb->state = TCP_TIME_WAIT;
    pcb->timer_time_wait = (uint32_t)timer_get_uptime_ms() + TCP_TIME_WAIT_TIMEOUT;
    break;

// 在 tcp_timer() 中处理超时
if (pcb->state == TCP_TIME_WAIT && 
    pcb->timer_time_wait != 0 && now >= pcb->timer_time_wait) {
    pcb->state = TCP_CLOSED;
}
```

---

### 1.3 乱序报文处理

**问题分析**：

当前实现只处理按序到达的数据：

```c
if (data_len > 0 && seq == pcb->rcv_nxt) {
    // 只接受期望序列号的数据
}
```

这意味着乱序到达的数据被直接丢弃，需要对端重传。

**设计方案**：

```
接收缓冲区布局（环形缓冲区 + 乱序队列）：

已读数据    待读数据    乱序数据空洞
   ↓           ↓           ↓
┌──────┬───────────┬───┬───────────┬───┬──────────┐
│XXXXX │ DATA DATA │   │ DATA DATA │   │          │
└──────┴───────────┴───┴───────────┴───┴──────────┘
        ↑                                         ↑
    rcv_nxt                                   窗口边界

乱序队列（链表）：
┌───────────────┐   ┌───────────────┐   ┌───────────────┐
│ seq: 1000     │──→│ seq: 1500     │──→│ seq: 2000     │
│ len: 100      │   │ len: 200      │   │ len: 150      │
│ data: [...]   │   │ data: [...]   │   │ data: [...]   │
└───────────────┘   └───────────────┘   └───────────────┘
```

**数据结构**：

```c
// 乱序段结构
typedef struct tcp_ooseq {
    uint32_t seq;               // 段起始序列号
    uint32_t len;               // 数据长度
    uint8_t *data;              // 数据
    struct tcp_ooseq *next;     // 下一个段
} tcp_ooseq_t;

// TCP PCB 新增字段
typedef struct tcp_pcb {
    // ... 现有字段 ...
    
    // 乱序队列
    tcp_ooseq_t *ooseq;         // 乱序段链表（按序列号排序）
    uint32_t ooseq_count;       // 乱序段数量（限制内存使用）
} tcp_pcb_t;

#define TCP_MAX_OOSEQ   8       // 最大乱序段数量
```

**实现代码**：

```c
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
            if (pcb->recv_len + copy_len <= pcb->recv_buf_size) {
                memcpy(pcb->recv_buf + pcb->recv_len, seg->data, copy_len);
                pcb->recv_len += copy_len;
            }
            pcb->rcv_nxt += seg->len;
            
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
 * @brief 处理接收数据（修改 tcp_input 中的数据处理部分）
 */
static void tcp_process_data(tcp_pcb_t *pcb, uint32_t seq, uint8_t *data, uint32_t data_len) {
    if (data_len == 0) return;
    
    if (seq == pcb->rcv_nxt) {
        // 按序到达，直接复制到接收缓冲区
        uint32_t copy_len = data_len;
        if (pcb->recv_len + copy_len <= pcb->recv_buf_size) {
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
```

---

### 1.4 发送缓冲区 ACK 确认机制

**问题分析**：

当前实现中，数据发送后立即从发送缓冲区移除，不等待 ACK：

```c
tcp_send_segment(pcb, TCP_FLAG_ACK | TCP_FLAG_PSH, pcb->send_buf, send_len);

// 移除已发送的数据 ← 问题：应该等 ACK 后再移除
if (send_len < pcb->send_len) {
    memmove(pcb->send_buf, pcb->send_buf + send_len, pcb->send_len - send_len);
}
pcb->send_len -= send_len;
```

**解决方案**：

修改 `tcp_write()` 函数，将数据加入未确认队列而不是立即移除：

```c
int tcp_write(tcp_pcb_t *pcb, const void *data, uint32_t len) {
    if (!pcb || !data || len == 0) return -1;
    if (pcb->state != TCP_ESTABLISHED && pcb->state != TCP_CLOSE_WAIT) return -1;
    
    // 检查发送窗口
    uint32_t send_window = pcb->snd_wnd;
    uint32_t in_flight = pcb->snd_nxt - pcb->snd_una;
    uint32_t available = (send_window > in_flight) ? (send_window - in_flight) : 0;
    
    if (available == 0) return 0;  // 窗口满
    
    uint32_t send_len = (len < available) ? len : available;
    if (send_len > pcb->mss) send_len = pcb->mss;
    
    // 发送数据
    int ret = tcp_send_segment(pcb, TCP_FLAG_ACK | TCP_FLAG_PSH, 
                               (uint8_t *)data, send_len);
    if (ret < 0) return ret;
    
    // 加入未确认队列（等待 ACK）
    tcp_queue_unacked(pcb, pcb->snd_nxt - send_len, TCP_FLAG_PSH, 
                      (uint8_t *)data, send_len);
    
    return send_len;
}
```

---

## 第二阶段：IP 层增强

### 2.1 IP 分片重组

**问题分析**：

当前实现直接丢弃分片包：

```c
if ((flags_frag & IP_FLAG_MF) || (flags_frag & IP_FRAG_OFFSET_MASK)) {
    LOG_WARN_MSG("ip: Fragmented packets not supported\n");
    netbuf_free(buf);
    return;
}
```

这导致 MTU 较小的网络无法正常通信。

**设计方案**：

```
IP 分片重组数据结构：

                     ┌─────────────────────────────────────┐
                     │           ip_reassembly_t           │
                     │  ┌─────────────────────────────┐    │
                     │  │ key: src_ip + dst_ip + id   │    │
                     │  │ total_len: 待定/已知        │    │
                     │  │ received: 位图              │    │
                     │  │ timeout: 超时时间           │    │
                     │  │ fragments: 分片链表         │    │
                     │  └─────────────────────────────┘    │
                     └─────────────────────────────────────┘

分片到达处理流程：

收到 IP 分片
     │
     ↓
按 (src_ip, dst_ip, id, proto) 查找重组条目
     │
     ├─ 未找到 ──→ 创建新条目，添加分片
     │
     └─ 找到 ──→ 添加分片到条目
                  │
                  ↓
            检查是否完整
                  │
            ├─ 不完整 ──→ 继续等待
            │
            └─ 完整 ──→ 重组数据包
                         │
                         ↓
                    传递给上层协议
```

**数据结构**：

```c
// src/include/net/ip.h

#define IP_REASS_MAX_ENTRIES    8       // 最大同时重组条目数
#define IP_REASS_TIMEOUT        30000   // 重组超时（30秒）
#define IP_REASS_MAX_SIZE       65535   // 最大 IP 数据包大小

// 分片结构
typedef struct ip_fragment {
    uint16_t offset;            // 分片偏移（字节）
    uint16_t len;               // 分片长度
    uint8_t *data;              // 分片数据
    struct ip_fragment *next;   // 下一个分片
} ip_fragment_t;

// 重组条目
typedef struct ip_reassembly {
    uint32_t src_ip;            // 源 IP
    uint32_t dst_ip;            // 目的 IP
    uint16_t id;                // 标识
    uint8_t protocol;           // 协议
    
    uint16_t total_len;         // 总长度（0 表示未知）
    uint16_t received_len;      // 已接收长度
    
    ip_fragment_t *fragments;   // 分片链表（按偏移排序）
    
    uint32_t timeout;           // 超时时间
    bool valid;                 // 条目是否有效
} ip_reassembly_t;
```

**实现代码**：

```c
// src/net/ip.c

static ip_reassembly_t reass_table[IP_REASS_MAX_ENTRIES];

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
    
    // 替换最旧的条目
    // TODO: 实现 LRU 替换策略
    return NULL;
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
    frag->next = *pp;
    *pp = frag;
    
    r->received_len += len;
    
    return 0;
}

/**
 * @brief 检查并重组完整数据包
 */
static netbuf_t *ip_reass_complete(ip_reassembly_t *r) {
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
    
    // 清理条目
    ip_reass_free(r);
    
    return buf;
}

/**
 * @brief 处理 IP 分片
 */
netbuf_t *ip_reassemble(netdev_t *dev, netbuf_t *buf, ip_header_t *ip) {
    uint16_t flags_frag = ntohs(ip->flags_fragment);
    uint16_t offset = (flags_frag & IP_FRAG_OFFSET_MASK) * 8;
    bool more_frags = (flags_frag & IP_FLAG_MF) != 0;
    
    // 查找或创建重组条目
    ip_reassembly_t *r = ip_reass_find(ip->src_addr, ip->dst_addr,
                                        ntohs(ip->identification), ip->protocol);
    if (!r) {
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
    return ip_reass_complete(r);
}
```

---

### 2.2 简单路由表

**设计方案**：

```c
// src/include/net/ip.h

#define IP_ROUTE_MAX    16

typedef struct ip_route {
    uint32_t dest;          // 目的网络
    uint32_t netmask;       // 子网掩码
    uint32_t gateway;       // 网关（0 表示直连）
    netdev_t *dev;          // 出接口
    uint32_t metric;        // 度量值（跳数）
} ip_route_t;

// 路由查找
netdev_t *ip_route_lookup(uint32_t dst_ip, uint32_t *next_hop);

// 添加路由
int ip_route_add(uint32_t dest, uint32_t netmask, uint32_t gateway, 
                 netdev_t *dev, uint32_t metric);

// 删除路由
int ip_route_del(uint32_t dest, uint32_t netmask);

// 显示路由表
void ip_route_show(void);
```

---

## 第三阶段：Socket API 完善

### 3.1 非阻塞 I/O 模式

**设计方案**：

```c
// 新增 Socket 标志
typedef struct socket {
    // ... 现有字段 ...
    int flags;              // O_NONBLOCK 等
} socket_t;

// fcntl 系统调用
int sys_fcntl(int fd, int cmd, int arg);

// 修改接收函数以支持非阻塞
ssize_t sys_recv(int sockfd, void *buf, size_t len, int flags) {
    socket_t *sock = socket_get(sockfd);
    if (!sock || !buf) return -1;
    
    bool nonblock = (sock->flags & O_NONBLOCK) || (flags & MSG_DONTWAIT);
    
    if (sock->type == SOCK_STREAM) {
        tcp_pcb_t *pcb = sock->pcb.tcp;
        
        if (pcb->recv_len == 0) {
            if (nonblock) {
                return -EAGAIN;  // 非阻塞模式，无数据立即返回
            }
            // 阻塞模式：等待数据...（需要实现等待机制）
        }
        
        return tcp_read(pcb, buf, len);
    }
    // ...
}
```

### 3.2 UDP recvfrom 源地址

**设计方案**：

修改 netbuf 结构，保存接收时的源地址信息：

```c
// src/include/net/netbuf.h
typedef struct netbuf {
    // ... 现有字段 ...
    
    // 接收信息
    uint32_t src_ip;        // 源 IP 地址
    uint16_t src_port;      // 源端口
} netbuf_t;

// src/net/udp.c
void udp_input(netdev_t *dev, netbuf_t *buf, uint32_t src_ip, uint32_t dst_ip) {
    // ...
    
    // 保存源地址到缓冲区
    buf->src_ip = src_ip;
    buf->src_port = src_port;
    
    // 加入接收队列
    // ...
}

// src/net/socket.c
ssize_t sys_recvfrom(int sockfd, void *buf, size_t len, int flags,
                     struct sockaddr *src_addr, socklen_t *addrlen) {
    // ...
    
    // 从缓冲区获取源地址
    if (src_addr && addrlen && *addrlen >= sizeof(struct sockaddr_in)) {
        struct sockaddr_in *sin = (struct sockaddr_in *)src_addr;
        sin->sin_family = AF_INET;
        sin->sin_port = htons(nbuf->src_port);
        sin->sin_addr = nbuf->src_ip;
        *addrlen = sizeof(struct sockaddr_in);
    }
    
    // ...
}
```

### 3.3 select() 系统调用

**设计方案**：

```c
// src/include/net/socket.h

typedef struct fd_set {
    uint32_t bits[2];       // 支持 64 个文件描述符
} fd_set;

#define FD_SETSIZE  64
#define FD_SET(fd, set)     ((set)->bits[(fd)/32] |= (1 << ((fd) % 32)))
#define FD_CLR(fd, set)     ((set)->bits[(fd)/32] &= ~(1 << ((fd) % 32)))
#define FD_ISSET(fd, set)   ((set)->bits[(fd)/32] & (1 << ((fd) % 32)))
#define FD_ZERO(set)        memset((set), 0, sizeof(fd_set))

struct timeval {
    long tv_sec;
    long tv_usec;
};

/**
 * @brief select 系统调用
 * @param nfds 最大文件描述符 + 1
 * @param readfds 读集合（输入/输出）
 * @param writefds 写集合（输入/输出）
 * @param exceptfds 异常集合（输入/输出）
 * @param timeout 超时时间（NULL 表示无限等待）
 * @return 就绪的描述符数量，0 超时，-1 错误
 */
int sys_select(int nfds, fd_set *readfds, fd_set *writefds,
               fd_set *exceptfds, struct timeval *timeout);
```

**实现要点**：

```c
int sys_select(int nfds, fd_set *readfds, fd_set *writefds,
               fd_set *exceptfds, struct timeval *timeout) {
    fd_set read_result, write_result, except_result;
    FD_ZERO(&read_result);
    FD_ZERO(&write_result);
    FD_ZERO(&except_result);
    
    uint32_t timeout_ms = timeout ? 
        (timeout->tv_sec * 1000 + timeout->tv_usec / 1000) : 0xFFFFFFFF;
    uint32_t start = (uint32_t)timer_get_uptime_ms();
    
    int ready_count = 0;
    
    while (1) {
        ready_count = 0;
        
        for (int fd = 0; fd < nfds; fd++) {
            socket_t *sock = socket_get(fd);
            if (!sock) continue;
            
            // 检查可读
            if (readfds && FD_ISSET(fd, readfds)) {
                bool readable = false;
                if (sock->type == SOCK_STREAM) {
                    tcp_pcb_t *pcb = sock->pcb.tcp;
                    readable = (pcb->recv_len > 0) ||
                              (pcb->state == TCP_CLOSE_WAIT) ||
                              (pcb->state == TCP_CLOSED);
                    // 监听 socket：有待接受的连接
                    if (sock->listening) {
                        readable = (pcb->accept_queue != NULL);
                    }
                } else {
                    udp_pcb_t *pcb = sock->pcb.udp;
                    readable = (pcb->recv_queue != NULL);
                }
                
                if (readable) {
                    FD_SET(fd, &read_result);
                    ready_count++;
                }
            }
            
            // 检查可写
            if (writefds && FD_ISSET(fd, writefds)) {
                bool writable = false;
                if (sock->type == SOCK_STREAM) {
                    tcp_pcb_t *pcb = sock->pcb.tcp;
                    // 发送窗口有空间
                    uint32_t in_flight = pcb->snd_nxt - pcb->snd_una;
                    writable = (pcb->state == TCP_ESTABLISHED) &&
                              (in_flight < pcb->snd_wnd);
                } else {
                    writable = true;  // UDP 总是可写
                }
                
                if (writable) {
                    FD_SET(fd, &write_result);
                    ready_count++;
                }
            }
            
            // 检查异常
            if (exceptfds && FD_ISSET(fd, exceptfds)) {
                bool has_error = (sock->error != 0);
                if (has_error) {
                    FD_SET(fd, &except_result);
                    ready_count++;
                }
            }
        }
        
        if (ready_count > 0) break;
        
        // 检查超时
        uint32_t elapsed = (uint32_t)timer_get_uptime_ms() - start;
        if (elapsed >= timeout_ms) break;
        
        // 让出 CPU（简单实现）
        // task_yield();
    }
    
    // 复制结果
    if (readfds) memcpy(readfds, &read_result, sizeof(fd_set));
    if (writefds) memcpy(writefds, &write_result, sizeof(fd_set));
    if (exceptfds) memcpy(exceptfds, &except_result, sizeof(fd_set));
    
    return ready_count;
}
```

---

## 第四阶段：高级功能

### 4.1 TCP 拥塞控制

**设计方案**：

实现基本的 Reno 拥塞控制算法：

```c
// TCP PCB 新增字段
typedef struct tcp_pcb {
    // ... 现有字段 ...
    
    // 拥塞控制
    uint32_t cwnd;          // 拥塞窗口
    uint32_t ssthresh;      // 慢启动阈值
    uint32_t dup_ack_count; // 重复 ACK 计数（快速重传）
} tcp_pcb_t;

// 拥塞控制算法
void tcp_congestion_on_ack(tcp_pcb_t *pcb) {
    if (pcb->cwnd < pcb->ssthresh) {
        // 慢启动：指数增长
        pcb->cwnd += pcb->mss;
    } else {
        // 拥塞避免：线性增长
        pcb->cwnd += pcb->mss * pcb->mss / pcb->cwnd;
    }
}

void tcp_congestion_on_loss(tcp_pcb_t *pcb) {
    // 乘法减小
    pcb->ssthresh = pcb->cwnd / 2;
    if (pcb->ssthresh < 2 * pcb->mss) {
        pcb->ssthresh = 2 * pcb->mss;
    }
    pcb->cwnd = pcb->mss;  // 重新慢启动
}

void tcp_congestion_on_fast_retransmit(tcp_pcb_t *pcb) {
    // 快速重传：收到 3 个重复 ACK
    pcb->ssthresh = pcb->cwnd / 2;
    pcb->cwnd = pcb->ssthresh + 3 * pcb->mss;
    // 重传丢失的段
}
```

### 4.2 DHCP 客户端

**设计方案**：

```c
// src/include/net/dhcp.h

#define DHCP_SERVER_PORT    67
#define DHCP_CLIENT_PORT    68

// DHCP 消息类型
#define DHCP_DISCOVER   1
#define DHCP_OFFER      2
#define DHCP_REQUEST    3
#define DHCP_ACK        5
#define DHCP_NAK        6

// DHCP 选项
#define DHCP_OPT_SUBNET_MASK    1
#define DHCP_OPT_ROUTER         3
#define DHCP_OPT_DNS            6
#define DHCP_OPT_HOSTNAME       12
#define DHCP_OPT_REQ_IP         50
#define DHCP_OPT_LEASE_TIME     51
#define DHCP_OPT_MSG_TYPE       53
#define DHCP_OPT_SERVER_ID      54
#define DHCP_OPT_END            255

/**
 * @brief 启动 DHCP 客户端
 * @param dev 网络设备
 * @return 0 成功，-1 失败
 */
int dhcp_start(netdev_t *dev);

/**
 * @brief 停止 DHCP 客户端
 */
void dhcp_stop(netdev_t *dev);

/**
 * @brief 获取 DHCP 状态
 */
int dhcp_get_status(netdev_t *dev, dhcp_info_t *info);
```

### 4.3 DNS 解析器

**设计方案**：

```c
// src/include/net/dns.h

#define DNS_PORT    53

/**
 * @brief 解析域名
 * @param hostname 域名
 * @param ip 输出 IP 地址
 * @return 0 成功，-1 失败
 */
int dns_resolve(const char *hostname, uint32_t *ip);

/**
 * @brief 设置 DNS 服务器
 */
void dns_set_server(uint32_t primary, uint32_t secondary);

/**
 * @brief 清除 DNS 缓存
 */
void dns_cache_clear(void);
```

### 4.4 netstat 命令

**设计方案**：

```c
// Shell 命令实现

void cmd_netstat(int argc, char *argv[]) {
    bool show_tcp = true, show_udp = true, show_listen = true;
    
    // 解析参数
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0) show_udp = false;
        if (strcmp(argv[i], "-u") == 0) show_tcp = false;
        if (strcmp(argv[i], "-l") == 0) show_listen = true;
    }
    
    kprintf("Active Internet connections\n");
    kprintf("Proto  Local Address          Foreign Address        State\n");
    
    if (show_tcp) {
        // 遍历 TCP PCB 链表
        tcp_pcb_list_dump();
    }
    
    if (show_udp) {
        // 遍历 UDP PCB 链表
        udp_pcb_list_dump();
    }
}

// 输出示例：
// Proto  Local Address          Foreign Address        State
// tcp    0.0.0.0:80             0.0.0.0:*              LISTEN
// tcp    10.0.2.15:80           10.0.2.2:54321         ESTABLISHED
// udp    0.0.0.0:68             0.0.0.0:*
```

---

## 测试方案

### 单元测试

```c
// tests/tcp_test.c

void test_tcp_retransmit(void) {
    // 1. 创建 TCP 连接
    // 2. 发送数据但不回复 ACK
    // 3. 等待 RTO 超时
    // 4. 验证数据被重传
    // 5. 发送 ACK
    // 6. 验证数据从未确认队列移除
}

void test_tcp_ooseq(void) {
    // 1. 创建已建立的 TCP 连接
    // 2. 发送乱序的数据段
    // 3. 验证数据被正确缓存
    // 4. 发送缺失的段
    // 5. 验证数据被正确重组
}

void test_ip_reassembly(void) {
    // 1. 发送分片的 ICMP 数据包
    // 2. 验证分片被正确重组
    // 3. 验证重组后的数据完整性
}
```

### 集成测试

```bash
# 在 QEMU 中测试

# 1. 配置网络
ifconfig eth0 10.0.2.15 netmask 255.255.255.0 gateway 10.0.2.2

# 2. 测试 ping（验证 ICMP）
ping 10.0.2.2

# 3. 测试 TCP 连接
# 在主机上启动 nc -l 8080
# 在 CastorOS 中连接并发送数据

# 4. 测试 UDP
# 使用 nc -u 测试 UDP 收发

# 5. 测试 DHCP
dhcpc eth0
```

---

## 实施计划

### 时间线

| 阶段 | 任务 | 优先级 |
|------|------|----------|
| 1.1 | TCP 重传定时器 | 必须 |
| 1.2 | TIME_WAIT 定时器 | 必须 |
| 1.3 | 乱序报文处理 | 必须 |
| 1.4 | 发送 ACK 确认 | 必须 |
| 2.1 | IP 分片重组 | 重要 |
| 2.2 | 简单路由表 | 一般 |
| 3.1 | 非阻塞 I/O | 重要 |
| 3.2 | UDP 源地址 | 重要 |
| 3.3 | select() | 重要 |
| 4.1 | 拥塞控制 | 一般 |
| 4.2 | DHCP 客户端 | 一般 |
| 4.3 | DNS 解析 | 一般 |
| 4.4 | netstat 命令 | 一般 |

### 依赖关系

```
第一阶段（TCP 可靠性）
     │
     ↓
第二阶段（IP 层增强）──→ 第三阶段（Socket API）
                              │
                              ↓
                        第四阶段（高级功能）
```

---

## 参考资料

1. **RFC 文档**
   - RFC 793: TCP 规范
   - RFC 1122: TCP/IP 主机要求
   - RFC 2581: TCP 拥塞控制
   - RFC 6298: TCP 重传定时器
   - RFC 2131: DHCP
   - RFC 1035: DNS

2. **开源实现**
   - lwIP (Lightweight IP)
   - Linux kernel networking
   - picoTCP

3. **书籍**
   - 《TCP/IP 详解 卷1：协议》- W. Richard Stevens
   - 《Unix 网络编程 卷1》- W. Richard Stevens

