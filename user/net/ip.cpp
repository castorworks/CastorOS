// ip.cpp - 协议栈的下半部分：以太网、ARP、IPv4、ICMP 回显
//
// 帧从网卡驱动进来（eth_input），按类型交给 ARP 或 IP；IP 包再按协议交给 ICMP（这里）、
// UDP（udp.cpp）或 TCP（tcp.cpp）。往外发的方向反过来：上层调 ip_send，这里选下一跳、
// 用 ARP 解析出它的 MAC，再交给驱动的 nic_send。发给自己地址的包不出网卡，走回环队列。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <net.h>
#include "net_internal.h"

// ============================================================================
// 以太网和 ARP
// ============================================================================

#define ETH_HDR         14
#define ETHERTYPE_IP    0x0800
#define ETHERTYPE_ARP   0x0806

struct arp_packet {
    uint16_t htype, ptype;
    uint8_t hlen, plen;
    uint16_t oper;                  // 1 请求，2 应答
    uint8_t sha[6];
    uint32_t spa;
    uint8_t tha[6];
    uint32_t tpa;
} __attribute__((packed));

static const uint8_t BROADCAST_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void eth_send(const uint8_t *dst_mac, uint16_t ethertype, const uint8_t *payload, size_t len) {
    static uint8_t frame[FRAME_MAX];
    if (len > FRAME_MAX - ETH_HDR) {
        return;
    }
    memcpy(frame, dst_mac, 6);
    memcpy(frame + 6, my_mac, 6);
    frame[12] = (uint8_t)(ethertype >> 8);
    frame[13] = (uint8_t)ethertype;
    memcpy(frame + ETH_HDR, payload, len);
    size_t total = ETH_HDR + len;
    if (total < 60) {               // 以太网帧的最小长度
        memset(frame + total, 0, 60 - total);
        total = 60;
    }
    nic_send(frame, total);
}

// ARP 表：每一项可以挂一个等地址解析的 IP 包
#define ARP_ENTRIES     8
#define ARP_RETRY_MS    500
#define ARP_TRIES       3

static struct arp_entry {
    uint32_t ip;                    // 0 表示空闲
    bool resolved;
    uint8_t mac[6];
    uint64_t asked_at;              // 上次发请求的时间
    int tries;
    size_t pending_len;             // 等解析结果的 IP 包（0 表示没有）
    uint8_t pending[FRAME_MAX - ETH_HDR];
} arp_table[ARP_ENTRIES];

static struct arp_entry *arp_find(uint32_t ip) {
    for (int i = 0; i < ARP_ENTRIES; i++) {
        if (arp_table[i].ip == ip) {
            return &arp_table[i];
        }
    }
    return NULL;
}

static void arp_request(struct arp_entry *e) {
    struct arp_packet p = {};
    p.htype = swap16(1);
    p.ptype = swap16(ETHERTYPE_IP);
    p.hlen = 6;
    p.plen = 4;
    p.oper = swap16(1);
    memcpy(p.sha, my_mac, 6);
    p.spa = swap32(my_ip);
    p.tpa = swap32(e->ip);
    e->asked_at = uptime_ms();
    e->tries++;
    eth_send(BROADCAST_MAC, ETHERTYPE_ARP, (const uint8_t *)&p, sizeof(p));
}

/** 把 IP 包发给下一跳；MAC 还不知道时先挂起并发 ARP 请求 */
static void arp_send_ip(uint32_t next_hop, const uint8_t *packet, size_t len) {
    struct arp_entry *e = arp_find(next_hop);
    if (e && e->resolved) {
        eth_send(e->mac, ETHERTYPE_IP, packet, len);
        return;
    }
    if (!e) {
        // 找空位；没有就顶掉一个已解析的
        e = arp_find(0);
        for (int i = 0; !e && i < ARP_ENTRIES; i++) {
            if (arp_table[i].resolved) {
                e = &arp_table[i];
            }
        }
        if (!e) {
            return;
        }
        memset(e, 0, sizeof(*e));
        e->ip = next_hop;
    }
    memcpy(e->pending, packet, len);
    e->pending_len = len;
    if (e->tries == 0) {
        arp_request(e);
    }
}

static void arp_input(const uint8_t *data, size_t len) {
    if (len < sizeof(struct arp_packet)) {
        return;
    }
    struct arp_packet p;
    memcpy(&p, data, sizeof(p));
    if (swap16(p.htype) != 1 || swap16(p.ptype) != ETHERTYPE_IP || p.hlen != 6 || p.plen != 4) {
        return;
    }
    uint32_t sender = swap32(p.spa);

    // 记下发送者的地址（只更新我们关心的表项），并发出挂起的包
    struct arp_entry *e = arp_find(sender);
    if (e && sender != 0) {
        memcpy(e->mac, p.sha, 6);
        e->resolved = true;
        e->tries = 0;
        if (e->pending_len > 0) {
            eth_send(e->mac, ETHERTYPE_IP, e->pending, e->pending_len);
            e->pending_len = 0;
        }
    }

    // 有人问我们的地址：回答
    if (swap16(p.oper) == 1 && swap32(p.tpa) == my_ip) {
        struct arp_packet r = p;
        r.oper = swap16(2);
        memcpy(r.sha, my_mac, 6);
        r.spa = swap32(my_ip);
        memcpy(r.tha, p.sha, 6);
        r.tpa = p.spa;
        eth_send(p.sha, ETHERTYPE_ARP, (const uint8_t *)&r, sizeof(r));
    }
}

/** 定时器里调用：重发没有回音的 ARP 请求，试够次数就放弃。@return 还有没有在等的 */
bool arp_tick(uint64_t now) {
    bool waiting = false;
    for (int i = 0; i < ARP_ENTRIES; i++) {
        struct arp_entry *e = &arp_table[i];
        if (e->ip == 0 || e->resolved) {
            continue;
        }
        if (now - e->asked_at >= ARP_RETRY_MS) {
            if (e->tries >= ARP_TRIES) {
                e->ip = 0;          // 不可达：丢掉挂起的包
                continue;
            }
            arp_request(e);
        }
        waiting = true;
    }
    return waiting;
}

// ============================================================================
// IPv4
// ============================================================================

#define IP_PROTO_ICMP   1
#define IP_HDR          20

struct ip_header {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_length;
    uint16_t id;
    uint16_t frag_offset;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src;
    uint32_t dst;
} __attribute__((packed));

uint16_t checksum(const void *data, size_t len, uint32_t start) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = start;
    while (len > 1) {
        sum += (uint32_t)((p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }
    if (len) {
        sum += (uint32_t)(p[0] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static void ip_input(const uint8_t *packet, size_t len);

// 回环队列：发给本机地址的 IP 包。满了就丢（和真实的链路一样，上层自己重传）
#define LOOPBACK_SLOTS  16

static struct {
    size_t len;
    uint8_t data[FRAME_MAX - ETH_HDR];
} loopback[LOOPBACK_SLOTS];
static int loopback_head = 0, loopback_count = 0;

static void loopback_push(const uint8_t *packet, size_t len) {
    if (loopback_count == LOOPBACK_SLOTS || len > sizeof(loopback[0].data)) {
        return;
    }
    int slot = (loopback_head + loopback_count) % LOOPBACK_SLOTS;
    memcpy(loopback[slot].data, packet, len);
    loopback[slot].len = len;
    loopback_count++;
}

/** 处理排队的回环包（处理过程中可能又排进新的） */
void loopback_drain(void) {
    while (loopback_count > 0) {
        int slot = loopback_head;
        loopback_head = (loopback_head + 1) % LOOPBACK_SLOTS;
        loopback_count--;
        ip_input(loopback[slot].data, loopback[slot].len);
    }
}

void ip_send(uint32_t dst, uint8_t protocol, const uint8_t *payload, size_t len) {
    static uint8_t packet[FRAME_MAX - ETH_HDR];
    static uint16_t next_id = 1;
    if (len > sizeof(packet) - IP_HDR) {
        return;
    }
    struct ip_header h = {};
    h.version_ihl = 0x45;
    h.total_length = swap16((uint16_t)(IP_HDR + len));
    h.id = swap16(next_id++);
    h.ttl = 64;
    h.protocol = protocol;
    h.src = swap32(my_ip);
    h.dst = swap32(dst);
    h.checksum = swap16(checksum(&h, IP_HDR, 0));
    memcpy(packet, &h, IP_HDR);
    memcpy(packet + IP_HDR, payload, len);

    if (dst == IP_BROADCAST || (my_ip != 0 && dst == (my_ip | ~netmask))) {
        // 广播：不需要地址解析
        eth_send(BROADCAST_MAC, ETHERTYPE_IP, packet, IP_HDR + len);
        return;
    }
    if (my_ip == 0) {
        return;     // 还没有地址：只能发广播
    }
    if (dst == my_ip) {
        // 发给自己：不经过网卡。不在这里直接处理，而是排队等回到主循环再当作收到的包处理：
        // 发送方往往正处在某个操作的中途，立刻递归进去会重入它还没更新完的状态
        loopback_push(packet, IP_HDR + len);
        return;
    }
    // 同一个子网直接发，否则交给网关
    uint32_t next_hop = (dst & netmask) == (my_ip & netmask) ? dst : gateway;
    arp_send_ip(next_hop, packet, IP_HDR + len);
}

static void icmp_input(uint32_t src, const uint8_t *data, size_t len);

static void ip_input(const uint8_t *packet, size_t len) {
    if (len < IP_HDR) {
        return;
    }
    struct ip_header h;
    memcpy(&h, packet, IP_HDR);
    size_t hdr_len = (size_t)(h.version_ihl & 0x0F) * 4;
    size_t total = swap16(h.total_length);
    if ((h.version_ihl >> 4) != 4 || hdr_len < IP_HDR || total < hdr_len || total > len ||
        checksum(packet, hdr_len, 0) != 0) {
        return;
    }
    // 收给自己的和广播的；还没有地址时来者不拒（DHCP 的应答可能直接发到将要分给我们的地址）
    uint32_t dst = swap32(h.dst);
    if (my_ip != 0 && dst != my_ip && dst != IP_BROADCAST && dst != (my_ip | ~netmask)) {
        return;
    }
    if (swap16(h.frag_offset) & 0x3FFF) {
        return;     // 分片：不支持重组
    }
    const uint8_t *payload = packet + hdr_len;
    size_t payload_len = total - hdr_len;
    if (h.protocol == IP_PROTO_ICMP) {
        icmp_input(swap32(h.src), payload, payload_len);
    } else if (h.protocol == IP_PROTO_UDP) {
        udp_input(swap32(h.src), dst, payload, payload_len);
    } else if (h.protocol == IP_PROTO_TCP) {
        tcp_input(swap32(h.src), dst, payload, payload_len);
    }
}

void eth_input(uint8_t *frame, size_t len) {
    if (len < ETH_HDR) {
        return;
    }
    uint16_t ethertype = (uint16_t)((frame[12] << 8) | frame[13]);
    if (ethertype == ETHERTYPE_ARP) {
        arp_input(frame + ETH_HDR, len - ETH_HDR);
    } else if (ethertype == ETHERTYPE_IP) {
        ip_input(frame + ETH_HDR, len - ETH_HDR);
    }
}

// ============================================================================
// ICMP 回显（ping）
// ============================================================================

#define ICMP_ECHO_REPLY     0
#define ICMP_ECHO_REQUEST   8
#define ICMP_HDR            8
#define PING_PAYLOAD        32
#define MAX_PINGS           8

struct icmp_header {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed));

// 等回显应答的客户
static struct ping {
    int pid;                        // 0 表示空闲
    uint32_t ip;
    uint16_t seq;
    uint64_t sent_at;
    uint64_t deadline;
} pings[MAX_PINGS];

static uint16_t ping_seq = 1;


static void icmp_send_echo(uint32_t dst, uint8_t type, uint16_t id, uint16_t seq,
                           const uint8_t *payload, size_t len) {
    static uint8_t msg[FRAME_MAX - ETH_HDR - IP_HDR];
    if (len > sizeof(msg) - ICMP_HDR) {
        return;
    }
    struct icmp_header h = {};
    h.type = type;
    h.id = id;                      // id 和 seq 原样带回，不需要转字节序
    h.seq = seq;
    memcpy(msg, &h, ICMP_HDR);
    memcpy(msg + ICMP_HDR, payload, len);
    uint16_t sum = swap16(checksum(msg, ICMP_HDR + len, 0));
    memcpy(msg + 2, &sum, 2);
    ip_send(dst, IP_PROTO_ICMP, msg, ICMP_HDR + len);
}

static void icmp_input(uint32_t src, const uint8_t *data, size_t len) {
    if (len < ICMP_HDR || checksum(data, len, 0) != 0) {
        return;
    }
    struct icmp_header h;
    memcpy(&h, data, ICMP_HDR);

    if (h.type == ICMP_ECHO_REQUEST && my_ip != 0) {
        icmp_send_echo(src, ICMP_ECHO_REPLY, h.id, h.seq, data + ICMP_HDR, len - ICMP_HDR);
    } else if (h.type == ICMP_ECHO_REPLY) {
        for (int i = 0; i < MAX_PINGS; i++) {
            struct ping *p = &pings[i];
            if (p->pid != 0 && p->ip == src && h.id == (uint16_t)p->pid && h.seq == p->seq) {
                int pid = p->pid;
                p->pid = 0;
                reply_client(pid, NET_PING, 0, uptime_ms() - p->sent_at, 0);
                return;
            }
        }
    }
}

void ping_start(int pid, uint32_t ip, uint32_t timeout_ms) {
    for (int i = 0; i < MAX_PINGS; i++) {
        struct ping *p = &pings[i];
        if (p->pid != 0) {
            continue;
        }
        // 先登记再发：发给自己时应答在 icmp_send_echo 返回之前就到了
        p->pid = pid;
        p->ip = ip;
        p->seq = ping_seq++;
        p->sent_at = uptime_ms();
        p->deadline = p->sent_at + (timeout_ms ? timeout_ms : 1);
        uint8_t payload[PING_PAYLOAD];
        for (int j = 0; j < PING_PAYLOAD; j++) {
            payload[j] = (uint8_t)('a' + j % 26);
        }
        icmp_send_echo(ip, ICMP_ECHO_REQUEST, (uint16_t)pid, p->seq, payload, sizeof(payload));
        return;
    }
    reply_client(pid, NET_PING, -1, 0, 0);
}

bool ping_tick(uint64_t now) {
    bool waiting = false;
    for (int i = 0; i < MAX_PINGS; i++) {
        struct ping *p = &pings[i];
        if (p->pid == 0) {
            continue;
        }
        if (now >= p->deadline) {
            int pid = p->pid;
            p->pid = 0;
            reply_client(pid, NET_PING, -1, 0, 0);
        } else {
            waiting = true;
        }
    }
    return waiting;
}
