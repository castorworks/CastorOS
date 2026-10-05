// tcp.cpp - TCP：主动连接（connect）、被动连接（listen / accept）、收发、关闭
//
// 一个刻意保持简单的实现：
//   - 发送：每条连接一个环形缓冲区，放着“还没被确认的数据”。能发多少由对方的窗口决定；
//     超时没被确认就从头重发（回退 N），超时时间每次翻倍，重试够次数就放弃连接
//   - 接收：只接受按序到达的数据，放进环形缓冲区，通告窗口就是缓冲区的剩余空间；
//     乱序的段直接丢掉并重复确认，靠对方重传
//   - 监听：收到 SYN 就建一条半开的连接，三次握手完成后排队等 accept
//   - 没有拥塞控制、选择确认、窗口缩放、延迟确认
//
// 阻塞的请求（connect、accept、recv、缓冲区满时的 send）不立刻应答：相应的事件发生时、
// 或者定时器发现它超时了，再用 reply_client 应答。

#include <syscall.h>
#include <string.h>
#include <clients.h>
#include "net_internal.h"

#define TCP_HDR         20
#define TCP_MSS         1460
#define TCP_FIN         0x01
#define TCP_SYN         0x02
#define TCP_RST         0x04
#define TCP_PSH         0x08
#define TCP_ACK         0x10

#define MAX_CONNS       8
#define SND_BUF         8192
#define RCV_BUF         8192

#define RTO_INITIAL_MS  300
#define RTO_MAX_MS      4000
#define MAX_RETRIES     8

#define EPHEMERAL_BASE  49152
#define BACKLOG         4           // 每个监听最多有这么多条还没被 accept 的连接

uint32_t tcp_retransmits = 0;

struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t data_offset;            // 高 4 位：头部长度，单位 4 字节
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

enum tcp_state {
    TCP_FREE = 0,
    TCP_LISTEN,                     // 不是连接，是一个监听：只用到 owner、local_port 和 accept_*
    TCP_SYN_SENT,
    TCP_SYN_RCVD,                   // 被动打开：收到 SYN、回了 SYN+ACK，等对方的确认
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,                 // 我们先关：FIN 已排队或已发，还没被确认
    TCP_FIN_WAIT_2,                 // 我们的 FIN 已被确认，等对方的 FIN
    TCP_CLOSING,                    // 双方同时关：收到了对方的 FIN，自己的还没被确认
    TCP_CLOSE_WAIT,                 // 对方先关：我们还可以继续发
    TCP_LAST_ACK,                   // 对方先关，我们也关了，等自己的 FIN 被确认
    TCP_DONE,                       // 双方都关完了，等应用来 close
    TCP_DEAD,                       // 被复位或重试耗尽，等应用来 close
};

static struct tcp_conn {
    enum tcp_state state;
    int owner;                      // 拥有这条连接的客户
    bool app_closed;                // 应用已经 close：连接结束后直接释放
    bool accepted;                  // 应用已经拿到这条连接（主动连接一开始就是）
    int listener;                   // 被动打开的连接：它所属的监听在 conns[] 里的下标；否则 -1

    uint16_t local_port, remote_port;
    uint32_t remote_ip;
    uint16_t mss;                   // 对方能收的最大段

    // 发送方向。序号空间：snd_una 是最早的未确认字节，snd_nxt 是下一个要发的，
    // snd_max 是发到过的最远处（重传会把 snd_nxt 拉回 snd_una）
    uint32_t snd_una, snd_nxt, snd_max;
    uint32_t snd_wnd;               // 对方通告的窗口
    uint8_t snd_buf[SND_BUF];       // 从 snd_una 开始的数据
    uint32_t snd_head, snd_len;
    bool fin_queued;                // 数据发完之后要发 FIN
    bool fin_sent, fin_acked;
    uint32_t fin_seq;

    // 接收方向
    uint32_t rcv_nxt;               // 期望的下一个序号
    uint8_t rcv_buf[RCV_BUF];
    uint32_t rcv_head, rcv_len;
    bool peer_fin;                  // 对方已经发完了

    // 重传定时器
    bool rto_armed;
    uint64_t rto_deadline;
    uint32_t rto_ms;
    int retries;

    // 阻塞在这条连接上的请求（都来自 owner，同一时刻最多一个）
    bool connect_waiting;
    uint64_t connect_deadline;
    bool recv_waiting;
    uint32_t recv_max;
    uint64_t recv_deadline;
    bool send_waiting;
    uint32_t send_len;
    bool accept_waiting;            // 只用于监听
    uint64_t accept_deadline;
} conns[MAX_CONNS];

// 序号比较要考虑回绕
static bool seq_lt(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }
static bool seq_le(uint32_t a, uint32_t b) { return (int32_t)(a - b) <= 0; }

static uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

// ============================================================================
// 发送
// ============================================================================

/** 发一个不属于任何连接的段（复位用）或连接上的段的公共部分 */
static void send_raw(uint32_t dst, uint16_t src_port, uint16_t dst_port, uint32_t seq, uint32_t ack,
                     uint8_t flags, uint16_t window, const uint8_t *data, size_t len, bool mss_option) {
    static uint8_t seg[IP_PAYLOAD_MAX];
    size_t hdr = TCP_HDR + (mss_option ? 4 : 0);
    if (hdr + len > sizeof(seg)) {
        return;
    }
    struct tcp_header h = {};
    h.src_port = swap16(src_port);
    h.dst_port = swap16(dst_port);
    h.seq = swap32(seq);
    h.ack = swap32(ack);
    h.data_offset = (uint8_t)((hdr / 4) << 4);
    h.flags = flags;
    h.window = swap16(window);
    memcpy(seg, &h, TCP_HDR);
    if (mss_option) {
        seg[TCP_HDR] = 2;           // 选项：最大段长度
        seg[TCP_HDR + 1] = 4;
        seg[TCP_HDR + 2] = (uint8_t)(TCP_MSS >> 8);
        seg[TCP_HDR + 3] = (uint8_t)TCP_MSS;
    }
    if (len) {
        memcpy(seg + hdr, data, len);
    }
    uint16_t sum = swap16(checksum(seg, hdr + len, pseudo_sum(my_ip, dst, IP_PROTO_TCP, hdr + len)));
    memcpy(seg + 16, &sum, 2);
    ip_send(dst, IP_PROTO_TCP, seg, hdr + len);
}

static void send_segment(struct tcp_conn *c, uint8_t flags, uint32_t seq, const uint8_t *data, size_t len) {
    uint32_t window = RCV_BUF - c->rcv_len;
    send_raw(c->remote_ip, c->local_port, c->remote_port, seq, c->rcv_nxt, flags,
             (uint16_t)min_u32(window, 0xFFFF), data, len, (flags & TCP_SYN) != 0);
}

static void send_ack(struct tcp_conn *c) {
    send_segment(c, TCP_ACK, c->snd_nxt, NULL, 0);
}

static void arm_rto(struct tcp_conn *c) {
    c->rto_armed = true;
    c->rto_deadline = uptime_ms() + c->rto_ms;
}

/** 已经发出去、还没被确认的数据字节数（不算 FIN） */
static uint32_t data_in_flight(struct tcp_conn *c) {
    return (c->fin_sent ? c->fin_seq : c->snd_nxt) - c->snd_una;
}

/** 在窗口允许的范围内，把缓冲区里还没发的数据发出去；数据发完了就发排队的 FIN */
static void tcp_output(struct tcp_conn *c) {
    if (c->state != TCP_ESTABLISHED && c->state != TCP_CLOSE_WAIT && c->state != TCP_FIN_WAIT_1 &&
        c->state != TCP_LAST_ACK && c->state != TCP_CLOSING) {
        return;
    }

    static uint8_t chunk[TCP_MSS];
    while (!c->fin_sent) {
        uint32_t in_flight = data_in_flight(c);
        uint32_t unsent = c->snd_len - in_flight;
        // 对方窗口为 0 而我们又没有在途的数据时，发 1 个字节去探一探，否则双方会一直等下去
        uint32_t window = (c->snd_wnd == 0 && in_flight == 0) ? 1 : c->snd_wnd;
        uint32_t can = min_u32(unsent, window > in_flight ? window - in_flight : 0);
        can = min_u32(can, c->mss);
        if (can == 0) {
            break;
        }
        for (uint32_t i = 0; i < can; i++) {
            chunk[i] = c->snd_buf[(c->snd_head + in_flight + i) % SND_BUF];
        }
        send_segment(c, TCP_ACK | TCP_PSH, c->snd_nxt, chunk, can);
        c->snd_nxt += can;
    }

    if (c->fin_queued && !c->fin_sent && data_in_flight(c) == c->snd_len) {
        c->fin_seq = c->snd_nxt;
        send_segment(c, TCP_FIN | TCP_ACK, c->snd_nxt, NULL, 0);
        c->snd_nxt++;
        c->fin_sent = true;
    }

    if (seq_lt(c->snd_max, c->snd_nxt)) {
        c->snd_max = c->snd_nxt;
    }
    if (c->snd_nxt != c->snd_una && !c->rto_armed) {
        arm_rto(c);
    }
}

// ============================================================================
// 连接的结束
// ============================================================================

static void wake_waiters(struct tcp_conn *c, int64_t result) {
    if (c->connect_waiting) {
        c->connect_waiting = false;
        reply_client(c->owner, NET_TCP_CONNECT, result, 0, 0);
    }
    if (c->recv_waiting) {
        c->recv_waiting = false;
        reply_client(c->owner, NET_TCP_RECV, result, 0, 0);
    }
    if (c->send_waiting) {
        c->send_waiting = false;
        reply_client(c->owner, NET_TCP_SEND, result, 0, 0);
    }
}

/** 双方都正常关完了 */
static void finish(struct tcp_conn *c) {
    c->rto_armed = false;
    // 还没被 accept 的连接留着：应用之后 accept 它，仍然能读到对方发过的数据
    if (c->app_closed) {
        c->state = TCP_FREE;
        return;
    }
    c->state = TCP_DONE;
    if (c->recv_waiting && c->rcv_len == 0) {
        c->recv_waiting = false;
        reply_client(c->owner, NET_TCP_RECV, 0, 0, 0);     // 没有更多数据了
    }
    if (c->send_waiting) {
        c->send_waiting = false;
        reply_client(c->owner, NET_TCP_SEND, -1, 0, 0);
    }
}

/** 连接异常结束（被复位、重试耗尽、应用放弃）：让等着的请求都失败 */
static void abort_conn(struct tcp_conn *c, bool send_rst) {
    if (send_rst && c->state != TCP_SYN_SENT) {
        send_segment(c, TCP_RST | TCP_ACK, c->snd_nxt, NULL, 0);
    }
    c->rto_armed = false;
    wake_waiters(c, -1);
    // 应用还不知道有这条连接（没 accept 过）时没有人会来 close 它：直接释放
    c->state = (c->app_closed || !c->accepted) ? TCP_FREE : TCP_DEAD;
}

// ============================================================================
// 接收
// ============================================================================

/** 把接收缓冲区里的数据交给正在等的 recv 请求 */
static void deliver(struct tcp_conn *c) {
    if (!c->recv_waiting || c->rcv_len == 0) {
        return;
    }
    char *buf = clients_buf(c->owner);
    c->recv_waiting = false;
    if (!buf) {
        reply_client(c->owner, NET_TCP_RECV, -1, 0, 0);
        return;
    }
    uint32_t n = min_u32(min_u32(c->rcv_len, c->recv_max), NET_BUF_SIZE);
    uint32_t free_before = RCV_BUF - c->rcv_len;
    for (uint32_t i = 0; i < n; i++) {
        buf[i] = (char)c->rcv_buf[(c->rcv_head + i) % RCV_BUF];
    }
    c->rcv_head = (c->rcv_head + n) % RCV_BUF;
    c->rcv_len -= n;
    reply_client(c->owner, NET_TCP_RECV, n, 0, 0);

    // 窗口之前小到放不下一个段、现在又放得下了：主动告诉对方，否则它可能一直等
    if (free_before < c->mss && RCV_BUF - c->rcv_len >= c->mss &&
        (c->state == TCP_ESTABLISHED || c->state == TCP_FIN_WAIT_1 || c->state == TCP_FIN_WAIT_2)) {
        send_ack(c);
    }
}

/** 发送缓冲区有空位了：把等着的 send 请求的数据收进来 */
static void accept_send(struct tcp_conn *c) {
    if (!c->send_waiting || c->snd_len == SND_BUF) {
        return;
    }
    char *buf = clients_buf(c->owner);
    c->send_waiting = false;
    if (!buf) {
        reply_client(c->owner, NET_TCP_SEND, -1, 0, 0);
        return;
    }
    uint32_t n = min_u32(c->send_len, SND_BUF - c->snd_len);
    for (uint32_t i = 0; i < n; i++) {
        c->snd_buf[(c->snd_head + c->snd_len + i) % SND_BUF] = (uint8_t)buf[i];
    }
    c->snd_len += n;
    reply_client(c->owner, NET_TCP_SEND, n, 0, 0);
}

static struct tcp_conn *find_conn(uint32_t remote_ip, uint16_t remote_port, uint16_t local_port) {
    for (int i = 0; i < MAX_CONNS; i++) {
        struct tcp_conn *c = &conns[i];
        if (c->state != TCP_FREE && c->state != TCP_LISTEN && c->local_port == local_port &&
            c->remote_port == remote_port && c->remote_ip == remote_ip) {
            return c;
        }
    }
    return NULL;
}

/** 初始序号：不要求不可预测，只要每条连接不一样 */
static uint32_t new_iss(void) {
    static uint32_t counter = 0;
    return (uint32_t)uptime_ms() * 250000u + (counter += 64021);
}

/** 对方 SYN 的选项里可能带着它的 MSS */
static void parse_mss(struct tcp_conn *c, const uint8_t *data, size_t hdr) {
    for (size_t i = TCP_HDR; i + 1 < hdr; ) {
        uint8_t kind = data[i];
        if (kind == 0) {
            break;
        }
        if (kind == 1) {
            i++;
            continue;
        }
        uint8_t olen = data[i + 1];
        if (olen < 2 || i + olen > hdr) {
            break;
        }
        if (kind == 2 && olen == 4) {
            uint16_t mss = (uint16_t)((data[i + 2] << 8) | data[i + 3]);
            if (mss >= 64 && mss < c->mss) {
                c->mss = mss;
            }
        }
        i += olen;
    }
}

/** 监听 l 上有没有握手已经完成、还没交给应用的连接；有就交给正在等的 accept */
static void try_accept(struct tcp_conn *l) {
    if (!l->accept_waiting) {
        return;
    }
    int li = (int)(l - conns);
    for (int i = 0; i < MAX_CONNS; i++) {
        struct tcp_conn *c = &conns[i];
        if (c->state == TCP_FREE || c->state == TCP_LISTEN || c->state == TCP_SYN_RCVD ||
            c->listener != li || c->accepted) {
            continue;
        }
        c->accepted = true;
        l->accept_waiting = false;
        reply_client(l->owner, NET_TCP_ACCEPT, i, c->remote_ip, c->remote_port);
        return;
    }
}

/** 监听端口上来了一个 SYN：建一条半开的连接，回 SYN+ACK */
static void passive_open(struct tcp_conn *l, uint32_t src, uint16_t src_port, uint32_t seq,
                         uint16_t window, const uint8_t *data, size_t hdr) {
    int li = (int)(l - conns);
    int pending = 0;
    struct tcp_conn *c = NULL;
    for (int i = 0; i < MAX_CONNS; i++) {
        if (conns[i].state == TCP_FREE) {
            if (!c) {
                c = &conns[i];
            }
        } else if (conns[i].state != TCP_LISTEN && conns[i].listener == li && !conns[i].accepted) {
            pending++;
        }
    }
    if (!c || pending >= BACKLOG) {
        return;     // 不回应：对方会重发 SYN，到时候也许有位置了
    }

    memset(c, 0, sizeof(*c));
    c->owner = l->owner;
    c->listener = li;
    c->remote_ip = src;
    c->remote_port = src_port;
    c->local_port = l->local_port;
    c->mss = TCP_MSS;
    parse_mss(c, data, hdr);
    c->rcv_nxt = seq + 1;
    c->snd_wnd = window;

    uint32_t iss = new_iss();
    c->snd_una = iss;
    c->snd_nxt = c->snd_max = iss + 1;      // SYN 占一个序号
    c->rto_ms = RTO_INITIAL_MS;
    c->state = TCP_SYN_RCVD;
    arm_rto(c);
    send_segment(c, TCP_SYN | TCP_ACK, iss, NULL, 0);
}

void tcp_input(uint32_t src, uint32_t dst, const uint8_t *data, size_t len) {
    if (len < TCP_HDR || dst != my_ip ||
        checksum(data, len, pseudo_sum(src, dst, IP_PROTO_TCP, len)) != 0) {
        return;
    }
    struct tcp_header h;
    memcpy(&h, data, TCP_HDR);
    size_t hdr = (size_t)(h.data_offset >> 4) * 4;
    if (hdr < TCP_HDR || hdr > len) {
        return;
    }
    uint32_t seq = swap32(h.seq);
    uint32_t ack = swap32(h.ack);
    uint8_t flags = h.flags;
    const uint8_t *payload = data + hdr;
    uint32_t payload_len = (uint32_t)(len - hdr);

    struct tcp_conn *c = find_conn(src, swap16(h.src_port), swap16(h.dst_port));
    if (!c && (flags & (TCP_SYN | TCP_ACK | TCP_RST)) == TCP_SYN) {
        // 新连接的请求：有人在这个端口上监听吗
        for (int i = 0; i < MAX_CONNS; i++) {
            if (conns[i].state == TCP_LISTEN && conns[i].local_port == swap16(h.dst_port)) {
                passive_open(&conns[i], src, swap16(h.src_port), seq, swap16(h.window), data, hdr);
                return;
            }
        }
    }
    if (!c) {
        // 没有这条连接：用复位告诉对方（对复位本身不再回应）
        if (!(flags & TCP_RST)) {
            if (flags & TCP_ACK) {
                send_raw(src, swap16(h.dst_port), swap16(h.src_port), ack, 0, TCP_RST, 0, NULL, 0, false);
            } else {
                uint32_t next = seq + payload_len + ((flags & TCP_SYN) ? 1 : 0) + ((flags & TCP_FIN) ? 1 : 0);
                send_raw(src, swap16(h.dst_port), swap16(h.src_port), 0, next, TCP_RST | TCP_ACK, 0,
                         NULL, 0, false);
            }
        }
        return;
    }

    // ---- 正在建立连接 ----
    if (c->state == TCP_SYN_SENT) {
        if ((flags & TCP_ACK) && ack != c->snd_nxt) {
            return;     // 不是对我们这个 SYN 的回应
        }
        if (flags & TCP_RST) {
            if (flags & TCP_ACK) {
                abort_conn(c, false);       // 对方拒绝了连接
            }
            return;
        }
        if ((flags & (TCP_SYN | TCP_ACK)) != (TCP_SYN | TCP_ACK)) {
            return;
        }
        parse_mss(c, data, hdr);
        c->rcv_nxt = seq + 1;
        c->snd_una = ack;
        c->snd_wnd = swap16(h.window);
        c->state = TCP_ESTABLISHED;
        c->rto_armed = false;
        c->retries = 0;
        c->rto_ms = RTO_INITIAL_MS;
        int id = (int)(c - conns);
        if (c->connect_waiting) {
            c->connect_waiting = false;
            reply_client(c->owner, NET_TCP_CONNECT, id, 0, 0);
        }
        send_ack(c);
        return;
    }

    if (c->state == TCP_DONE || c->state == TCP_DEAD) {
        return;
    }

    // ---- 被动打开：等对方确认我们的 SYN+ACK ----
    if (c->state == TCP_SYN_RCVD) {
        if (flags & TCP_RST) {
            c->state = TCP_FREE;
            return;
        }
        if (flags & TCP_SYN) {
            // 对方重发了 SYN：我们的 SYN+ACK 丢了，再发一次
            send_segment(c, TCP_SYN | TCP_ACK, c->snd_una, NULL, 0);
            return;
        }
        if (!(flags & TCP_ACK) || ack != c->snd_max) {
            return;
        }
        // 握手完成。这个段里可能已经带着数据，接着按已建立的连接处理
        c->state = TCP_ESTABLISHED;
        if (c->listener >= 0 && conns[c->listener].state == TCP_LISTEN) {
            // 下面的通用处理会确认 SYN、清掉重传定时器；先把它交给可能在等的 accept
            try_accept(&conns[c->listener]);
        }
    }

    // ---- 已建立的连接 ----
    if (flags & TCP_RST) {
        // 只认落在接收窗口里的复位，免得被随便一个伪造的段打断
        if (seq_le(c->rcv_nxt, seq) && seq_lt(seq, c->rcv_nxt + RCV_BUF)) {
            abort_conn(c, false);
        }
        return;
    }
    if (flags & TCP_SYN) {
        send_ack(c);    // 对方重发了 SYN+ACK：我们的 ACK 丢了，再确认一次
        return;
    }
    if (!(flags & TCP_ACK)) {
        return;
    }

    // 确认：推进 snd_una，把已确认的数据从发送缓冲区里去掉
    if (seq_lt(c->snd_una, ack) && seq_le(ack, c->snd_max)) {
        uint32_t acked = ack - c->snd_una;
        uint32_t data_acked = min_u32(acked, c->snd_len);
        c->snd_head = (c->snd_head + data_acked) % SND_BUF;
        c->snd_len -= data_acked;
        c->snd_una = ack;
        if (seq_lt(c->snd_nxt, c->snd_una)) {
            c->snd_nxt = c->snd_una;
        }
        if (c->fin_sent && ack == c->fin_seq + 1) {
            c->fin_acked = true;
        }
        // 有进展：重传计时重新开始
        c->retries = 0;
        c->rto_ms = RTO_INITIAL_MS;
        c->rto_armed = false;
        if (c->snd_una != c->snd_max) {
            arm_rto(c);
        }
    }
    c->snd_wnd = swap16(h.window);

    // 数据：只接受正好接在 rcv_nxt 后面的部分
    bool need_ack = false;
    bool fin = (flags & TCP_FIN) != 0;
    if (payload_len > 0 || fin) {
        if (seq_lt(seq, c->rcv_nxt)) {
            // 开头是重复的（对方重传了我们已经收下的数据）：去掉重复的部分
            uint32_t dup = c->rcv_nxt - seq;
            if (dup >= payload_len) {
                // 整段数据都是重复的；FIN 正好落在 rcv_nxt 上才算数
                fin = fin && dup == payload_len;
                payload_len = 0;
            } else {
                payload += dup;
                payload_len -= dup;
            }
            seq = c->rcv_nxt;
            need_ack = true;
        }
        if (seq != c->rcv_nxt) {
            // 乱序：丢掉，重复确认我们期望的位置
            send_ack(c);
            return;
        }
        bool can_receive = c->state == TCP_ESTABLISHED || c->state == TCP_FIN_WAIT_1 ||
                           c->state == TCP_FIN_WAIT_2;
        if (payload_len > 0 && can_receive) {
            uint32_t n = min_u32(payload_len, RCV_BUF - c->rcv_len);
            for (uint32_t i = 0; i < n; i++) {
                c->rcv_buf[(c->rcv_head + c->rcv_len + i) % RCV_BUF] = payload[i];
            }
            c->rcv_len += n;
            c->rcv_nxt += n;
            if (n < payload_len) {
                fin = false;        // 没收完，后面的 FIN 也就还没轮到
            }
            need_ack = true;
        }
        if (fin && can_receive) {
            c->rcv_nxt++;
            c->peer_fin = true;
            need_ack = true;
        }
    }

    if (need_ack) {
        send_ack(c);
    }

    // 状态迁移：我们的 FIN 被确认了、对方的 FIN 到了
    if (c->fin_acked) {
        if (c->state == TCP_FIN_WAIT_1) {
            c->state = TCP_FIN_WAIT_2;
        } else if (c->state == TCP_CLOSING || c->state == TCP_LAST_ACK) {
            finish(c);
            return;
        }
    }
    if (c->peer_fin) {
        if (c->state == TCP_ESTABLISHED) {
            c->state = TCP_CLOSE_WAIT;
        } else if (c->state == TCP_FIN_WAIT_1) {
            c->state = TCP_CLOSING;
        } else if (c->state == TCP_FIN_WAIT_2) {
            deliver(c);
            finish(c);
            return;
        }
    }

    deliver(c);
    if (c->recv_waiting && c->peer_fin && c->rcv_len == 0) {
        c->recv_waiting = false;
        reply_client(c->owner, NET_TCP_RECV, 0, 0, 0);     // 对方关了，数据也读完了
    }
    accept_send(c);
    tcp_output(c);
}

// ============================================================================
// 定时器
// ============================================================================

bool tcp_tick(uint64_t now) {
    bool active = false;
    for (int i = 0; i < MAX_CONNS; i++) {
        struct tcp_conn *c = &conns[i];
        if (c->state == TCP_FREE) {
            continue;
        }
        if (c->state == TCP_LISTEN) {
            if (c->accept_waiting) {
                if (now >= c->accept_deadline) {
                    c->accept_waiting = false;
                    reply_client(c->owner, NET_TCP_ACCEPT, -1, 0, 0);
                } else {
                    active = true;
                }
            }
            continue;
        }

        if (c->connect_waiting && now >= c->connect_deadline) {
            abort_conn(c, false);
            continue;
        }
        if (c->recv_waiting && now >= c->recv_deadline) {
            c->recv_waiting = false;
            reply_client(c->owner, NET_TCP_RECV, -1, 0, 0);
        }

        if (c->rto_armed && now >= c->rto_deadline) {
            if (++c->retries > MAX_RETRIES) {
                abort_conn(c, true);
                continue;
            }
            c->rto_ms = min_u32(c->rto_ms * 2, RTO_MAX_MS);
            tcp_retransmits++;
            if (c->state == TCP_SYN_SENT) {
                send_segment(c, TCP_SYN, c->snd_una, NULL, 0);
                arm_rto(c);
            } else if (c->state == TCP_SYN_RCVD) {
                send_segment(c, TCP_SYN | TCP_ACK, c->snd_una, NULL, 0);
                arm_rto(c);
            } else {
                // 回退 N：从最早未确认的地方重新发
                c->snd_nxt = c->snd_una;
                if (c->fin_sent && !c->fin_acked) {
                    c->fin_sent = false;
                }
                c->rto_armed = false;
                tcp_output(c);
            }
        }

        if (c->rto_armed || c->connect_waiting || c->recv_waiting || c->send_waiting) {
            active = true;
        }
    }
    return active;
}

// ============================================================================
// 客户请求
// ============================================================================

/** pid 可以收发的连接（不含监听和还没 accept 的） */
static struct tcp_conn *conn_of(int pid, uint64_t id) {
    if (id >= MAX_CONNS || conns[id].state == TCP_FREE || conns[id].state == TCP_LISTEN ||
        conns[id].owner != pid || conns[id].app_closed || !conns[id].accepted) {
        return NULL;
    }
    return &conns[id];
}

static struct tcp_conn *listener_of(int pid, uint64_t id) {
    if (id >= MAX_CONNS || conns[id].state != TCP_LISTEN || conns[id].owner != pid) {
        return NULL;
    }
    return &conns[id];
}

/** 关掉一个监听：还没被 accept 的连接一并复位 */
static void close_listener(struct tcp_conn *l) {
    int li = (int)(l - conns);
    for (int i = 0; i < MAX_CONNS; i++) {
        struct tcp_conn *c = &conns[i];
        if (c->state != TCP_FREE && c->state != TCP_LISTEN && c->listener == li && !c->accepted) {
            if (c->state != TCP_DONE && c->state != TCP_DEAD) {
                abort_conn(c, true);
            }
            c->state = TCP_FREE;
        }
    }
    if (l->accept_waiting) {
        l->accept_waiting = false;
        reply_client(l->owner, NET_TCP_ACCEPT, -1, 0, 0);
    }
    l->state = TCP_FREE;
}

void tcp_drop_owner(int pid) {
    for (int i = 0; i < MAX_CONNS; i++) {
        struct tcp_conn *c = &conns[i];
        if (c->state != TCP_FREE && c->owner == pid) {
            // 没人会来读写了：直接复位，不走正常的关闭
            c->connect_waiting = c->recv_waiting = c->send_waiting = c->accept_waiting = false;
            c->app_closed = true;
            bool live = c->state != TCP_DONE && c->state != TCP_DEAD && c->state != TCP_LISTEN;
            if (live) {
                abort_conn(c, true);
            }
            c->state = TCP_FREE;
        }
    }
}

static void do_connect(int pid, uint32_t ip, uint16_t port, uint32_t timeout_ms) {
    static uint16_t next_port = EPHEMERAL_BASE;

    // 已经退出的进程留下的连接先收回来
    for (int i = 0; i < MAX_CONNS; i++) {
        if (conns[i].state != TCP_FREE && kill(conns[i].owner, 0) != 0) {
            tcp_drop_owner(conns[i].owner);
        }
    }

    struct tcp_conn *c = NULL;
    for (int i = 0; i < MAX_CONNS && !c; i++) {
        if (conns[i].state == TCP_FREE) {
            c = &conns[i];
        }
    }
    if (!c || my_ip == 0 || ip == 0 || port == 0) {
        reply_client(pid, NET_TCP_CONNECT, -1, 0, 0);
        return;
    }

    memset(c, 0, sizeof(*c));
    c->owner = pid;
    c->accepted = true;
    c->listener = -1;
    c->remote_ip = ip;
    c->remote_port = port;
    c->local_port = next_port;
    next_port = next_port == 65535 ? EPHEMERAL_BASE : (uint16_t)(next_port + 1);
    c->mss = TCP_MSS;

    uint32_t iss = new_iss();
    c->snd_una = iss;
    c->snd_nxt = c->snd_max = iss + 1;      // SYN 占一个序号
    c->rto_ms = RTO_INITIAL_MS;
    c->state = TCP_SYN_SENT;
    c->connect_waiting = true;
    c->connect_deadline = uptime_ms() + (timeout_ms ? timeout_ms : 1);

    arm_rto(c);
    send_segment(c, TCP_SYN, iss, NULL, 0);
}

void tcp_request(const struct ipc_msg *m) {
    int pid = (int)m->sender;

    if (m->label == NET_TCP_CONNECT) {
        do_connect(pid, (uint32_t)m->data[0], (uint16_t)m->data[1], (uint32_t)m->data[2]);
        return;
    }

    if (m->label == NET_TCP_LISTEN) {
        uint16_t port = (uint16_t)m->data[0];
        struct tcp_conn *l = NULL;
        for (int i = 0; i < MAX_CONNS; i++) {
            if (conns[i].state == TCP_LISTEN && conns[i].local_port == port) {
                port = 0;       // 已经有人在听
            } else if (conns[i].state == TCP_FREE && !l) {
                l = &conns[i];
            }
        }
        if (!l || port == 0 || m->data[0] > 0xFFFF) {
            reply_client(pid, NET_TCP_LISTEN, -1, 0, 0);
            return;
        }
        memset(l, 0, sizeof(*l));
        l->state = TCP_LISTEN;
        l->owner = pid;
        l->local_port = port;
        l->listener = -1;
        reply_client(pid, NET_TCP_LISTEN, (int)(l - conns), 0, 0);
        return;
    }

    if (m->label == NET_TCP_ACCEPT) {
        struct tcp_conn *l = listener_of(pid, m->data[0]);
        if (!l) {
            reply_client(pid, NET_TCP_ACCEPT, -1, 0, 0);
            return;
        }
        l->accept_waiting = true;
        l->accept_deadline = uptime_ms() + m->data[1];
        try_accept(l);
        if (l->accept_waiting && m->data[1] == 0) {
            l->accept_waiting = false;      // 不等待
            reply_client(pid, NET_TCP_ACCEPT, -1, 0, 0);
        }
        return;
    }

    if (m->label == NET_TCP_CLOSE && listener_of(pid, m->data[0])) {
        reply_client(pid, NET_TCP_CLOSE, 0, 0, 0);
        close_listener(&conns[m->data[0]]);
        return;
    }

    struct tcp_conn *c = conn_of(pid, m->data[0]);
    if (!c) {
        reply_client(pid, m->label, -1, 0, 0);
        return;
    }

    switch (m->label) {
        case NET_TCP_SEND: {
            bool can_send = (c->state == TCP_ESTABLISHED || c->state == TCP_CLOSE_WAIT) && !c->fin_queued;
            if (!can_send || m->data[1] == 0 || m->data[1] > NET_BUF_SIZE || !clients_buf(pid)) {
                reply_client(pid, NET_TCP_SEND, -1, 0, 0);
                return;
            }
            c->send_waiting = true;
            c->send_len = (uint32_t)m->data[1];
            accept_send(c);         // 有空位就立刻收下并应答，否则等确认腾出空位
            tcp_output(c);
            return;
        }

        case NET_TCP_RECV:
            if (c->rcv_len > 0) {
                c->recv_waiting = true;
                c->recv_max = (uint32_t)m->data[1];
                deliver(c);
                return;
            }
            if (c->peer_fin || c->state == TCP_DONE) {
                reply_client(pid, NET_TCP_RECV, 0, 0, 0);       // 对方关了，没有更多数据
                return;
            }
            if (c->state == TCP_DEAD || m->data[2] == 0 || m->data[1] == 0) {
                reply_client(pid, NET_TCP_RECV, -1, 0, 0);
                return;
            }
            c->recv_waiting = true;
            c->recv_max = (uint32_t)m->data[1];
            c->recv_deadline = uptime_ms() + m->data[2];
            return;

        case NET_TCP_CLOSE:
            c->app_closed = true;
            reply_client(pid, NET_TCP_CLOSE, 0, 0, 0);
            if (c->state == TCP_ESTABLISHED || c->state == TCP_CLOSE_WAIT) {
                // 正常关闭：数据发完后发 FIN，之后的事在后台完成
                c->fin_queued = true;
                c->state = c->state == TCP_ESTABLISHED ? TCP_FIN_WAIT_1 : TCP_LAST_ACK;
                tcp_output(c);
            } else if (c->state == TCP_SYN_SENT || c->state == TCP_DONE || c->state == TCP_DEAD) {
                c->state = TCP_FREE;
            }
            // 其余状态（已经在关闭途中）：结束时 finish/abort 会释放
            return;
    }
    reply_client(pid, m->label, -1, 0, 0);
}
