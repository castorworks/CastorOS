// net - 网络服务
//
// 特权的用户态服务，以 "net" 登记，实现 net.h 里的协议。它把两样东西放在同一个
// 进程里：virtio-net 网卡驱动，和一个很小的协议栈（以太网、ARP、IPv4、ICMP 回显、UDP）。
// 放在一起是因为收包是由中断驱动的：一个主循环同时等中断、定时器和客户请求，
// 帧到了直接交给协议栈，不需要驱动和协议栈之间再来回传一次。
//
// 地址在启动时用 DHCP 获取；等不到应答就退回 QEMU 用户网络（-netdev user）的固定配置
// 10.0.2.15/24，网关 10.0.2.2。没有 TCP、分片重组，也不续租。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <names.h>
#include <net.h>
#include <virtio.h>
#include <clients.h>

// ============================================================================
// 配置
// ============================================================================

// 地址配置：DHCP 完成（或放弃）之前 my_ip 是 0
static uint32_t my_ip = 0;
static uint32_t netmask = 0;
static uint32_t gateway = 0;
static uint32_t dns_server = 0;
static bool from_dhcp = false;

#define IP_BROADCAST 0xFFFFFFFFu

static uint8_t my_mac[6];

// 网络字节序是大端，三个架构都是小端
static uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static uint32_t swap32(uint32_t v) {
    return (v << 24) | ((v << 8) & 0x00FF0000) | ((v >> 8) & 0x0000FF00) | (v >> 24);
}

// ============================================================================
// 网卡：virtio-net
// ============================================================================

#define PAGE_SIZE           4096
#define VIRTIO_NET_F_MAC    (1u << 5)       // 配置空间里有 MAC 地址

// 每个帧前面都有这个头（legacy，没有协商 MRG_RXBUF 时是 10 字节）
struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed));

#define NIC_BUF_SIZE    2048                // 一个缓冲区放一个帧（含 virtio 头）
#define RX_BUFS         32
#define TX_BUFS         16
#define FRAME_MAX       1514                // 以太网帧（不含 FCS）

static struct virtio_dev nic;
static struct virtq rxq, txq;               // 队列 0 收，队列 1 发
static char *rx_mem, *tx_mem;
static uint64_t rx_phys, tx_phys;
static uint16_t tx_free[TX_BUFS];           // 空闲的发送缓冲区
static int tx_free_count;

static void eth_input(uint8_t *frame, size_t len);

static bool nic_init(void) {
    if (!virtio_find(&nic, VIRTIO_ID_NET)) {
        return false;
    }
    if (!(virtio_get_features(&nic) & VIRTIO_NET_F_MAC)) {
        printf("net: device does not provide a MAC address\n");
        return false;
    }
    virtio_set_features(&nic, VIRTIO_NET_F_MAC);
    for (int i = 0; i < 6; i++) {
        my_mac[i] = virtio_config_read8(&nic, (uint32_t)i);
    }

    rx_mem = (char *)dma_alloc(RX_BUFS * NIC_BUF_SIZE, &rx_phys);
    tx_mem = (char *)dma_alloc(TX_BUFS * NIC_BUF_SIZE, &tx_phys);
    if (rx_mem == MAP_FAILED || tx_mem == MAP_FAILED ||
        !virtq_setup(&nic, &rxq, 0, 64) || !virtq_setup(&nic, &txq, 1, 64) ||
        rxq.num < RX_BUFS || txq.num < TX_BUFS) {
        printf("net: cannot set up the queues\n");
        return false;
    }

    // 把所有接收缓冲区交给设备：描述符 i 固定对应缓冲区 i
    for (uint16_t i = 0; i < RX_BUFS; i++) {
        rxq.desc[i].addr = rx_phys + (uint64_t)i * NIC_BUF_SIZE;
        rxq.desc[i].len = NIC_BUF_SIZE;
        rxq.desc[i].flags = VRING_DESC_F_WRITE;
        rxq.desc[i].next = 0;
        virtq_submit(&rxq, i);
    }
    // 发送完成不需要中断：下次发送时顺手回收
    txq.avail->flags = VRING_AVAIL_F_NO_INTERRUPT;
    for (uint16_t i = 0; i < TX_BUFS; i++) {
        tx_free[tx_free_count++] = i;
    }

    if (irq_claim(nic.irq) != 0) {
        printf("net: cannot claim IRQ %d\n", nic.irq);
        return false;
    }
    virtio_driver_ok(&nic);
    virtio_notify(&nic, &rxq);
    return true;
}

/** 发一个以太网帧。发送缓冲区用完时丢弃 */
static void nic_send(const uint8_t *frame, size_t len) {
    uint32_t done;
    while (virtq_pop_used(&txq, &done, NULL)) {
        tx_free[tx_free_count++] = (uint16_t)done;
    }
    if (tx_free_count == 0 || len > FRAME_MAX) {
        return;
    }
    uint16_t i = tx_free[--tx_free_count];
    char *buf = tx_mem + (size_t)i * NIC_BUF_SIZE;
    memset(buf, 0, sizeof(struct virtio_net_hdr));
    memcpy(buf + sizeof(struct virtio_net_hdr), frame, len);

    txq.desc[i].addr = tx_phys + (uint64_t)i * NIC_BUF_SIZE;
    txq.desc[i].len = (uint32_t)(sizeof(struct virtio_net_hdr) + len);
    txq.desc[i].flags = 0;
    txq.desc[i].next = 0;
    virtq_submit(&txq, i);
    virtio_notify(&nic, &txq);
}

/** 处理设备交回来的接收缓冲区，再把它们还给设备 */
static void nic_poll(void) {
    uint32_t i, len;
    bool any = false;
    while (virtq_pop_used(&rxq, &i, &len)) {
        if (i < RX_BUFS && len > sizeof(struct virtio_net_hdr) && len <= NIC_BUF_SIZE) {
            eth_input((uint8_t *)rx_mem + (size_t)i * NIC_BUF_SIZE + sizeof(struct virtio_net_hdr),
                      len - sizeof(struct virtio_net_hdr));
        }
        virtq_submit(&rxq, (uint16_t)i);
        any = true;
    }
    if (any) {
        virtio_notify(&nic, &rxq);
    }
}

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
static bool arp_tick(uint64_t now) {
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
#define IP_PROTO_UDP    17
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

/** 互联网校验和：16 位反码求和。start 用来接着前一段（伪首部）的和算 */
static uint16_t checksum(const void *data, size_t len, uint32_t start) {
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

static void ip_send(uint32_t dst, uint8_t protocol, const uint8_t *payload, size_t len) {
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
        // 发给自己：不经过网卡，直接当作收到的包处理（先拷一份，packet 会被重用）
        static uint8_t loop[FRAME_MAX - ETH_HDR];
        memcpy(loop, packet, IP_HDR + len);
        ip_input(loop, IP_HDR + len);
        return;
    }
    // 同一个子网直接发，否则交给网关
    uint32_t next_hop = (dst & netmask) == (my_ip & netmask) ? dst : gateway;
    arp_send_ip(next_hop, packet, IP_HDR + len);
}

static void icmp_input(uint32_t src, const uint8_t *data, size_t len);
static void udp_input(uint32_t src, uint32_t dst, const uint8_t *data, size_t len);

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
    }
}

static void eth_input(uint8_t *frame, size_t len) {
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

/** 应答一个阻塞在请求里的客户 */
static void reply_client(int pid, uint32_t label, int64_t result, uint64_t d1, uint64_t d2) {
    struct ipc_msg m = {};
    m.label = label;
    m.data[0] = (uint64_t)result;
    m.data[1] = d1;
    m.data[2] = d2;
    ipc_reply(pid, &m);
}

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

static void ping_start(int pid, uint32_t ip, uint32_t timeout_ms) {
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

// ============================================================================
// UDP
// ============================================================================

#define UDP_HDR         8
#define DHCP_CLIENT_PORT 68
#define MAX_SOCKETS     8
#define SOCKET_QUEUE    4           // 每个套接字最多积压这么多个数据报
#define EPHEMERAL_BASE  49152

struct udp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

struct datagram {
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t len;
    uint8_t data[NET_UDP_MAX];
};

static struct udp_socket {
    int owner;                      // 0 表示空闲
    uint16_t port;
    int waiting;                    // 阻塞在 recv 里的客户（就是 owner）或 0
    uint64_t deadline;
    int head, count;                // 积压的数据报（环形）
    struct datagram queue[SOCKET_QUEUE];
} sockets[MAX_SOCKETS];

static struct udp_socket *socket_of(int pid, uint64_t id) {
    if (id >= MAX_SOCKETS || sockets[id].owner != pid) {
        return NULL;
    }
    return &sockets[id];
}

static bool port_in_use(uint16_t port) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i].owner != 0 && sockets[i].port == port) {
            return true;
        }
    }
    return false;
}

static void close_sockets_of(int pid) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i].owner == pid) {
            sockets[i].owner = 0;
        }
    }
}

static int64_t udp_open(int pid, uint16_t port, uint64_t *bound_port) {
    // 已经退出的进程留下的套接字先收回来
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i].owner != 0 && kill(sockets[i].owner, 0) != 0) {
            sockets[i].owner = 0;
        }
    }
    if (port == 0) {
        static uint16_t next = EPHEMERAL_BASE;
        for (int tries = 0; tries < 1000 && port == 0; tries++) {
            uint16_t candidate = next;
            next = next == 65535 ? EPHEMERAL_BASE : (uint16_t)(next + 1);
            if (!port_in_use(candidate)) {
                port = candidate;
            }
        }
    }
    if (port == 0 || port == DHCP_CLIENT_PORT || port_in_use(port)) {
        return -1;
    }
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i].owner == 0) {
            sockets[i].owner = pid;
            sockets[i].port = port;
            sockets[i].waiting = 0;
            sockets[i].head = sockets[i].count = 0;
            *bound_port = port;
            return i;
        }
    }
    return -1;
}

/** 把队首的数据报交给客户（它的共享缓冲区），应答它的 recv */
static void udp_deliver(struct udp_socket *s, int pid) {
    struct datagram *d = &s->queue[s->head];
    char *buf = clients_buf(pid);
    if (!buf) {
        reply_client(pid, NET_UDP_RECV, -1, 0, 0);
        return;
    }
    memcpy(buf, d->data, d->len);
    s->head = (s->head + 1) % SOCKET_QUEUE;
    s->count--;
    reply_client(pid, NET_UDP_RECV, d->len, d->src_ip, d->src_port);
}

/** 伪首部（源、目的地址，协议，长度）的校验和部分 */
static uint32_t udp_pseudo_sum(uint32_t src, uint32_t dst, size_t len) {
    return (src >> 16) + (src & 0xFFFF) + (dst >> 16) + (dst & 0xFFFF) + IP_PROTO_UDP + (uint32_t)len;
}

static void dhcp_input(const uint8_t *data, size_t len);

/** 不经过套接字直接发一个数据报（DHCP 用：那时还没有地址） */
static void udp_send_raw(uint16_t src_port, uint32_t dst, uint16_t dst_port, const uint8_t *data, size_t len) {
    static uint8_t msg[UDP_HDR + NET_UDP_MAX];
    if (len > NET_UDP_MAX) {
        return;
    }
    struct udp_header h = {};
    h.src_port = swap16(src_port);
    h.dst_port = swap16(dst_port);
    h.length = swap16((uint16_t)(UDP_HDR + len));
    memcpy(msg, &h, UDP_HDR);
    memcpy(msg + UDP_HDR, data, len);
    uint16_t sum = checksum(msg, UDP_HDR + len, udp_pseudo_sum(my_ip, dst, UDP_HDR + len));
    sum = swap16(sum == 0 ? 0xFFFF : sum);      // 算出来是 0 时按规定发全 1
    memcpy(msg + 6, &sum, 2);
    ip_send(dst, IP_PROTO_UDP, msg, UDP_HDR + len);
}

static void udp_input(uint32_t src, uint32_t dst, const uint8_t *data, size_t len) {
    if (len < UDP_HDR) {
        return;
    }
    struct udp_header h;
    memcpy(&h, data, UDP_HDR);
    size_t total = swap16(h.length);
    if (total < UDP_HDR || total > len) {
        return;
    }
    // 校验和为 0 表示发送方没算
    if (h.checksum != 0 && checksum(data, total, udp_pseudo_sum(src, dst, total)) != 0) {
        return;
    }
    size_t payload = total - UDP_HDR;
    uint16_t port = swap16(h.dst_port);

    if (port == DHCP_CLIENT_PORT) {
        dhcp_input(data + UDP_HDR, payload);
        return;
    }

    for (int i = 0; i < MAX_SOCKETS; i++) {
        struct udp_socket *s = &sockets[i];
        if (s->owner == 0 || s->port != port) {
            continue;
        }
        if (s->count == SOCKET_QUEUE || payload > NET_UDP_MAX) {
            return;     // 积压满了：丢弃
        }
        struct datagram *d = &s->queue[(s->head + s->count) % SOCKET_QUEUE];
        d->src_ip = src;
        d->src_port = swap16(h.src_port);
        d->len = (uint16_t)payload;
        memcpy(d->data, data + UDP_HDR, payload);
        s->count++;
        if (s->waiting) {
            int pid = s->waiting;
            s->waiting = 0;
            udp_deliver(s, pid);
        }
        return;
    }
}

static int64_t udp_send(struct udp_socket *s, uint32_t dst, uint16_t dst_port, const char *data, size_t len) {
    if (len > NET_UDP_MAX || my_ip == 0) {
        return -1;
    }
    udp_send_raw(s->port, dst, dst_port, (const uint8_t *)data, len);
    return 0;
}

// ============================================================================
// DHCP 客户端
// ============================================================================
//
// 启动时走一遍 DISCOVER -> OFFER -> REQUEST -> ACK。请求里带广播标志，
// 让服务器把应答发到广播地址（我们这时还没有地址）。不续租。

#define DHCP_SERVER_PORT    67
#define DHCP_MAGIC          0x63825363u

#define DHCP_DISCOVER       1
#define DHCP_OFFER          2
#define DHCP_REQUEST        3
#define DHCP_ACK            5
#define DHCP_NAK            6

#define DHCP_RETRY_MS       500
#define DHCP_TRIES          6           // 3 秒没有结果就放弃

struct dhcp_packet {
    uint8_t op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint32_t ciaddr, yiaddr, siaddr, giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint32_t magic;
    uint8_t options[64];
} __attribute__((packed));

static enum { DHCP_IDLE, DHCP_DISCOVERING, DHCP_REQUESTING, DHCP_DONE } dhcp_state = DHCP_IDLE;
static uint32_t dhcp_xid;
static uint32_t dhcp_offered, dhcp_server;      // 网络字节序，原样带回
static uint64_t dhcp_sent_at;
static int dhcp_tries;

static void dhcp_send(uint8_t type) {
    static struct dhcp_packet p;
    memset(&p, 0, sizeof(p));
    p.op = 1;                           // 请求
    p.htype = 1;
    p.hlen = 6;
    p.xid = dhcp_xid;
    p.flags = swap16(0x8000);           // 请把应答广播给我
    memcpy(p.chaddr, my_mac, 6);
    p.magic = swap32(DHCP_MAGIC);

    uint8_t *o = p.options;
    *o++ = 53; *o++ = 1; *o++ = type;                       // 消息类型
    if (type == DHCP_REQUEST) {
        *o++ = 50; *o++ = 4; memcpy(o, &dhcp_offered, 4); o += 4;   // 要的地址
        *o++ = 54; *o++ = 4; memcpy(o, &dhcp_server, 4); o += 4;    // 选的服务器
    }
    *o++ = 55; *o++ = 3; *o++ = 1; *o++ = 3; *o++ = 6;      // 想要：掩码、网关、DNS
    *o++ = 255;

    dhcp_sent_at = uptime_ms();
    dhcp_tries++;
    udp_send_raw(DHCP_CLIENT_PORT, IP_BROADCAST, DHCP_SERVER_PORT, (const uint8_t *)&p, sizeof(p));
}

static void print_config(const char *how, uint32_t lease) {
    char ip[16], gw[16], dns[16];
    net_format_ip(my_ip, ip);
    net_format_ip(gateway, gw);
    net_format_ip(dns_server, dns);
    if (lease) {
        printf("net: %s: %s, gateway %s, dns %s, lease %us\n", how, ip, gw, dns, lease);
    } else {
        printf("net: %s: %s, gateway %s, dns %s\n", how, ip, gw, dns);
    }
}

static void dhcp_start(void) {
    dhcp_xid = 0x43000000u | ((uint32_t)uptime_ms() & 0xFFFF) | ((uint32_t)my_mac[5] << 16);
    dhcp_state = DHCP_DISCOVERING;
    dhcp_tries = 0;
    dhcp_send(DHCP_DISCOVER);
}

/** 等不到 DHCP：用 QEMU 用户网络的固定地址 */
static void dhcp_give_up(void) {
    dhcp_state = DHCP_DONE;
    my_ip = NET_IP(10, 0, 2, 15);
    netmask = NET_IP(255, 255, 255, 0);
    gateway = NET_IP(10, 0, 2, 2);
    dns_server = NET_IP(10, 0, 2, 3);
    from_dhcp = false;
    print_config("no DHCP answer, using static address", 0);
}

static void dhcp_input(const uint8_t *data, size_t len) {
    static struct dhcp_packet p;
    if (dhcp_state != DHCP_DISCOVERING && dhcp_state != DHCP_REQUESTING) {
        return;
    }
    size_t fixed = sizeof(p) - sizeof(p.options);
    if (len < fixed + 4) {
        return;
    }
    memcpy(&p, data, fixed);
    if (p.op != 2 || p.xid != dhcp_xid || memcmp(p.chaddr, my_mac, 6) != 0 ||
        swap32(p.magic) != DHCP_MAGIC) {
        return;
    }

    // 选项：类型、长度、值，一个接一个
    uint8_t type = 0;
    uint32_t mask = 0, router = 0, dns = 0, server = 0, lease = 0;
    const uint8_t *o = data + fixed;
    const uint8_t *end = data + len;
    while (o < end && *o != 255) {
        if (*o == 0) {          // 填充
            o++;
            continue;
        }
        if (o + 2 > end || o + 2 + o[1] > end) {
            break;
        }
        uint8_t code = o[0], olen = o[1];
        const uint8_t *v = o + 2;
        if (code == 53 && olen >= 1) type = v[0];
        if (code == 1 && olen >= 4) memcpy(&mask, v, 4);
        if (code == 3 && olen >= 4) memcpy(&router, v, 4);
        if (code == 6 && olen >= 4) memcpy(&dns, v, 4);
        if (code == 54 && olen >= 4) memcpy(&server, v, 4);
        if (code == 51 && olen >= 4) memcpy(&lease, v, 4);
        o += 2 + olen;
    }

    if (dhcp_state == DHCP_DISCOVERING && type == DHCP_OFFER && p.yiaddr != 0) {
        dhcp_offered = p.yiaddr;
        dhcp_server = server;
        dhcp_state = DHCP_REQUESTING;
        dhcp_tries = 0;
        dhcp_send(DHCP_REQUEST);
    } else if (dhcp_state == DHCP_REQUESTING && type == DHCP_ACK && p.yiaddr != 0) {
        dhcp_state = DHCP_DONE;
        my_ip = swap32(p.yiaddr);
        netmask = mask ? swap32(mask) : NET_IP(255, 255, 255, 0);
        gateway = swap32(router);
        dns_server = swap32(dns);
        from_dhcp = true;
        print_config("configured by DHCP", swap32(lease));
    } else if (dhcp_state == DHCP_REQUESTING && type == DHCP_NAK) {
        dhcp_start();           // 被拒绝：从头再来
    }
}

/** 定时器里调用：重发没有回音的请求，试够次数就放弃。@return 是否还在进行 */
static bool dhcp_tick(uint64_t now) {
    if (dhcp_state != DHCP_DISCOVERING && dhcp_state != DHCP_REQUESTING) {
        return false;
    }
    if (now - dhcp_sent_at >= DHCP_RETRY_MS) {
        if (dhcp_tries >= DHCP_TRIES) {
            dhcp_give_up();
            return false;
        }
        dhcp_send(dhcp_state == DHCP_DISCOVERING ? DHCP_DISCOVER : DHCP_REQUEST);
    }
    return true;
}

// ============================================================================
// 超时
// ============================================================================

#define TICK_MS 50

/** 处理到期的等待，返回是否还有东西在等（还需要定时器） */
static bool expire_waiters(void) {
    uint64_t now = uptime_ms();
    bool waiting = arp_tick(now);
    waiting = dhcp_tick(now) || waiting;

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
    for (int i = 0; i < MAX_SOCKETS; i++) {
        struct udp_socket *s = &sockets[i];
        if (s->owner == 0 || s->waiting == 0) {
            continue;
        }
        if (now >= s->deadline) {
            int pid = s->waiting;
            s->waiting = 0;
            reply_client(pid, NET_UDP_RECV, -1, 0, 0);
        } else {
            waiting = true;
        }
    }
    return waiting;
}

// ============================================================================
// 主循环
// ============================================================================

static void handle_request(struct ipc_msg *m) {
    int pid = (int)m->sender;
    struct ipc_msg reply = {};
    reply.label = m->label;
    int64_t result = -1;

    switch (m->label) {
        case NET_INFO:
            result = 0;
            for (int i = 0; i < 6; i++) {
                reply.data[1] |= (uint64_t)my_mac[i] << (8 * i);
            }
            reply.data[1] |= (uint64_t)from_dhcp << 48;
            reply.data[2] = my_ip;
            reply.data[3] = netmask;
            reply.data[4] = gateway;
            reply.data[5] = dns_server;
            break;

        case NET_PING:
            if (my_ip == 0) {
                break;      // 还没有地址
            }
            // 应答在收到回显、或者超时的时候发
            ping_start(pid, (uint32_t)m->data[0], (uint32_t)m->data[1]);
            return;

        case NET_UDP_OPEN:
            result = udp_open(pid, (uint16_t)m->data[0], &reply.data[1]);
            break;

        case NET_UDP_CLOSE: {
            struct udp_socket *s = socket_of(pid, m->data[0]);
            if (s) {
                s->owner = 0;
                result = 0;
            }
            break;
        }

        case NET_UDP_SEND: {
            struct udp_socket *s = socket_of(pid, m->data[0]);
            char *buf = clients_buf(pid);
            if (s && buf && my_ip != 0 && m->data[3] <= NET_UDP_MAX && m->data[2] != 0 && m->data[2] <= 0xFFFF) {
                // 先应答再发：发给自己的数据报会立刻进到某个套接字，那里可能又要应答别的客户
                reply_client(pid, NET_UDP_SEND, 0, 0, 0);
                udp_send(s, (uint32_t)m->data[1], (uint16_t)m->data[2], buf, (size_t)m->data[3]);
                return;
            }
            break;
        }

        case NET_UDP_RECV: {
            struct udp_socket *s = socket_of(pid, m->data[0]);
            if (!s) {
                break;
            }
            if (s->count > 0) {
                udp_deliver(s, pid);
                return;
            }
            if (m->data[1] == 0) {
                break;      // 不等待
            }
            s->waiting = pid;
            s->deadline = uptime_ms() + m->data[1];
            return;
        }
    }

    reply.data[0] = (uint64_t)result;
    ipc_reply(pid, &reply);
}

int main() {
    if (!nic_init()) {
        printf("net: no usable virtio-net device\n");
        return 1;
    }
    clients_init(NET_BUF_SIZE, close_sockets_of);
    if (name_register(NET_SERVICE_NAME) != 0) {
        printf("net: cannot register name\n");
        return 1;
    }
    printf("net: ready (pid %d, irq %d), %02x:%02x:%02x:%02x:%02x:%02x\n", getpid(), nic.irq,
           my_mac[0], my_mac[1], my_mac[2], my_mac[3], my_mac[4], my_mac[5]);
    dhcp_start();
    timer_set(TICK_MS);

    struct ipc_msg m;
    for (;;) {
        if (ipc_recv(IPC_ANY, &m) != 0) {
            continue;
        }

        if (m.sender == IPC_KERNEL) {
            if (m.label == IPC_LABEL_IRQ) {
                virtio_irq_ack(&nic);
                nic_poll();
                irq_ack(nic.irq);
            }
            // IPC_LABEL_TIMER：下面统一处理
        } else if (m.label == IPC_LABEL_GRANT) {
            clients_attach(&m);
        } else {
            handle_request(&m);
        }

        // 只要还有事在等（ping、recv、ARP、DHCP），就保持一个周期性的定时器来处理超时
        if (expire_waiters()) {
            timer_set(TICK_MS);
        }
    }
}
