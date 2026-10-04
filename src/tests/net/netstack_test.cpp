// ============================================================================
// netstack_test.cpp - 协议栈行为测试（ARP / IP / UDP / DHCP / TCP）
// ============================================================================
//
// 这些测试注册一块假的网卡：发送的帧被记录下来供检查，接收的帧由测试
// 直接构造后经 net::Netdev::receive() 注入（任务上下文下同步处理）。
// 这样可以在没有真实对端的情况下驱动完整的收发路径和状态机。
//
// 测试期间假网卡是默认设备；结束时恢复原来的默认设备并清空 ARP 缓存。
// ============================================================================

#include <tests/ktest.h>
#include <net/net.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/kprintf.h>

void run_netstack_tests(void);

// ============================================================================
// 假网卡
// ============================================================================

#define T_LOCAL     IP_ADDR(10, 99, 0, 2)
#define T_PEER      IP_ADDR(10, 99, 0, 1)
#define T_MASK      IP_ADDR(255, 255, 255, 0)
#define T_OFFERED   IP_ADDR(10, 99, 0, 77)

static const uint8_t T_MAC[6]      = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
static const uint8_t T_PEER_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

#define CAP_MAX         32
#define CAP_FRAME_MAX   1600

struct CapturedFrame {
    uint32_t len;
    uint8_t data[CAP_FRAME_MAX];
};

static CapturedFrame cap[CAP_MAX];
static int cap_count = 0;       // 发送过的帧数（超过 CAP_MAX 的只计数不保存）
static bool tx_fail = false;    // 让发送失败，模拟驱动出错

class TestNetdevOps final : public net::NetdevOps {
public:
    int transmit(net::Netdev *, net::Netbuf *buf) const override {
        if (tx_fail) {
            return -1;
        }
        if (cap_count < CAP_MAX && buf->len <= CAP_FRAME_MAX) {
            cap[cap_count].len = buf->len;
            memcpy(cap[cap_count].data, buf->data, buf->len);
        }
        cap_count++;
        return 0;
    }
};

static const TestNetdevOps test_ops{};
static net::Netdev test_dev;
static net::Netdev *saved_default = NULL;

static void cap_reset(void) {
    cap_count = 0;
}

static void test_net_setup(void) {
    saved_default = net::Netdev::get_default();

    memset(&test_dev, 0, sizeof(test_dev));
    strcpy(test_dev.name, "tst0");
    memcpy(test_dev.mac, T_MAC, 6);
    test_dev.mtu = 1500;
    test_dev.ops = &test_ops;
    test_dev.lock.init();

    net::Netdev::register_device(&test_dev);
    net::Netdev::set_default(&test_dev);
    net::Netdev::up(&test_dev);

    test_dev.ip_addr = T_LOCAL;
    test_dev.netmask = T_MASK;
    test_dev.gateway = 0;

    net::Arp::cache_clear();
    tx_fail = false;
    cap_reset();
}

static void test_net_teardown(void) {
    net::Arp::cache_clear();
    net::Ip::route_del(0, 0);
    net::Netdev::down(&test_dev);
    net::Netdev::unregister_device(&test_dev);
    net::Netdev::set_default(saved_default);
}

// ============================================================================
// 构造并注入帧
// ============================================================================

/**
 * 构造 以太网 + IPv4 + 负载 的帧并注入。pad_to 模拟以太网对短帧的填充。
 */
static void inject_ip(uint8_t proto, uint32_t src_ip, uint32_t dst_ip,
                      const uint8_t *l4, uint32_t l4_len, uint32_t pad_to,
                      bool eth_broadcast) {
    uint32_t total = ETH_HEADER_LEN + IP_HEADER_MIN_LEN + l4_len;
    uint32_t frame_len = (total < pad_to) ? pad_to : total;

    net::Netbuf *buf = net::Netbuf::alloc(frame_len);
    if (!buf) {
        return;
    }
    uint8_t *p = net::Netbuf::put(buf, frame_len);  // alloc 已清零，填充字节为 0

    eth_header_t *eth = (eth_header_t *)p;
    memcpy(eth->dst, eth_broadcast ? ETH_BROADCAST_ADDR : T_MAC, 6);
    memcpy(eth->src, T_PEER_MAC, 6);
    eth->type = htons(ETH_TYPE_IP);

    ip_header_t *ip = (ip_header_t *)(p + ETH_HEADER_LEN);
    ip->version_ihl = (IP_VERSION_4 << 4) | (IP_HEADER_MIN_LEN / 4);
    ip->total_length = htons((uint16_t)(IP_HEADER_MIN_LEN + l4_len));
    ip->identification = htons(1);
    ip->flags_fragment = htons(IP_FLAG_DF);
    ip->ttl = 64;
    ip->protocol = proto;
    ip->src_addr = src_ip;
    ip->dst_addr = dst_ip;
    ip->checksum = 0;
    ip->checksum = net::Ip::checksum(ip, IP_HEADER_MIN_LEN);

    memcpy(p + ETH_HEADER_LEN + IP_HEADER_MIN_LEN, l4, l4_len);

    net::Netdev::receive(&test_dev, buf);  // 任务上下文：同步交给协议栈
}

static void inject_udp(uint16_t sport, uint16_t dport, uint32_t dst_ip,
                       const uint8_t *data, uint32_t len, bool broadcast) {
    static uint8_t dgram[UDP_HEADER_LEN + 600];
    if (len > 600) {
        return;
    }
    udp_header_t *udp = (udp_header_t *)dgram;
    udp->src_port = htons(sport);
    udp->dst_port = htons(dport);
    udp->length = htons((uint16_t)(UDP_HEADER_LEN + len));
    udp->checksum = 0;  // 0 表示不校验
    memcpy(dgram + UDP_HEADER_LEN, data, len);
    inject_ip(IP_PROTO_UDP, T_PEER, dst_ip, dgram, UDP_HEADER_LEN + len, 60, broadcast);
}

static void inject_tcp(uint16_t sport, uint16_t dport, uint32_t seq, uint32_t ack,
                       uint8_t flags, uint16_t window,
                       const uint8_t *data, uint32_t len) {
    static uint8_t seg[TCP_HEADER_MIN_LEN + 1460];
    if (len > 1460) {
        return;
    }
    memset(seg, 0, TCP_HEADER_MIN_LEN);
    tcp_header_t *tcp = (tcp_header_t *)seg;
    tcp->src_port = htons(sport);
    tcp->dst_port = htons(dport);
    tcp->seq_num = htonl(seq);
    tcp->ack_num = htonl(ack);
    tcp->data_offset = (TCP_HEADER_MIN_LEN / 4) << 4;
    tcp->flags = flags;
    tcp->window = htons(window);
    if (data && len > 0) {
        memcpy(seg + TCP_HEADER_MIN_LEN, data, len);
    }
    tcp->checksum = 0;
    tcp->checksum = net::Tcp::checksum(T_PEER, T_LOCAL, tcp,
                                       (uint16_t)(TCP_HEADER_MIN_LEN + len));
    // 短段被填充到以太网最小帧长（60 字节），和真实网卡收到的一样
    inject_ip(IP_PROTO_TCP, T_PEER, T_LOCAL, seg, TCP_HEADER_MIN_LEN + len, 60, false);
}

static void inject_arp_reply(void) {
    uint32_t frame_len = 60;
    net::Netbuf *buf = net::Netbuf::alloc(frame_len);
    if (!buf) {
        return;
    }
    uint8_t *p = net::Netbuf::put(buf, frame_len);

    eth_header_t *eth = (eth_header_t *)p;
    memcpy(eth->dst, T_MAC, 6);
    memcpy(eth->src, T_PEER_MAC, 6);
    eth->type = htons(ETH_TYPE_ARP);

    arp_header_t *arp = (arp_header_t *)(p + ETH_HEADER_LEN);
    arp->hardware_type = htons(ARP_HARDWARE_ETHERNET);
    arp->protocol_type = htons(ARP_PROTOCOL_IP);
    arp->hardware_len = 6;
    arp->protocol_len = 4;
    arp->operation = htons(ARP_OP_REPLY);
    memcpy(arp->sender_mac, T_PEER_MAC, 6);
    arp->sender_ip = T_PEER;
    memcpy(arp->target_mac, T_MAC, 6);
    arp->target_ip = T_LOCAL;

    net::Netdev::receive(&test_dev, buf);
}

// ============================================================================
// 解析记录下来的帧
// ============================================================================

static bool cap_valid(int i) {
    return i >= 0 && i < cap_count && i < CAP_MAX;
}

static uint16_t cap_ethertype(int i) {
    return ntohs(((eth_header_t *)cap[i].data)->type);
}

static ip_header_t *cap_ip(int i) {
    return (ip_header_t *)(cap[i].data + ETH_HEADER_LEN);
}

static bool cap_is_ip_proto(int i, uint8_t proto) {
    return cap_valid(i) && cap_ethertype(i) == ETH_TYPE_IP && cap_ip(i)->protocol == proto;
}

static uint8_t *cap_udp_payload(int i) {
    return cap[i].data + ETH_HEADER_LEN + IP_HEADER_MIN_LEN + UDP_HEADER_LEN;
}

static tcp_header_t *cap_tcp(int i) {
    return (tcp_header_t *)(cap[i].data + ETH_HEADER_LEN + IP_HEADER_MIN_LEN);
}

static uint32_t cap_tcp_data_len(int i) {
    return ntohs(cap_ip(i)->total_length) - IP_HEADER_MIN_LEN -
           net::Tcp::header_len(cap_tcp(i));
}

static uint8_t *cap_tcp_data(int i) {
    return (uint8_t *)cap_tcp(i) + net::Tcp::header_len(cap_tcp(i));
}

/** 最后一个 TCP 帧的下标；dport 非 0 时只看发往该端口的帧。没有返回 -1 */
static int cap_last_tcp(uint16_t dport) {
    int n = (cap_count < CAP_MAX) ? cap_count : CAP_MAX;
    for (int i = n - 1; i >= 0; i--) {
        if (cap_is_ip_proto(i, IP_PROTO_TCP) &&
            (dport == 0 || ntohs(cap_tcp(i)->dst_port) == dport)) {
            return i;
        }
    }
    return -1;
}

static net::Netbuf *make_payload(const uint8_t *data, uint32_t len) {
    net::Netbuf *buf = net::Netbuf::alloc(len);
    if (!buf) {
        return NULL;
    }
    memcpy(net::Netbuf::put(buf, len), data, len);
    return buf;
}

// ============================================================================
// ARP：等待队列的所有权、上限
// ============================================================================

/**
 * 目的地址还没解析时，数据包排入 ARP 等待队列；调用者随后释放自己的缓冲区，
 * ARP 应答到达后发出的仍然是完整的数据包（队列持有的是副本）。
 */
TEST_CASE(test_arp_pending_packet_survives_caller_free) {
    test_net_setup();

    udp_pcb_t *pcb = net::Udp::pcb_new();
    ASSERT_NOT_NULL(pcb);
    if (!pcb) { test_net_teardown(); return; }
    net::Udp::bind(pcb, 0, 4000);

    const uint8_t payload[4] = {'a', 'b', 'c', 'd'};
    net::Netbuf *buf = make_payload(payload, 4);
    ASSERT_NOT_NULL(buf);
    if (!buf) { net::Udp::pcb_free(pcb); test_net_teardown(); return; }

    ASSERT_EQ(0, net::Udp::sendto(pcb, buf, T_PEER, 5000));
    net::Netbuf::free(buf);  // 发送只是借用，调用者总是释放

    // 只发出了一个广播 ARP 请求
    ASSERT_EQ(1, cap_count);
    ASSERT_EQ_U(ETH_TYPE_ARP, cap_ethertype(0));
    ASSERT_TRUE(mac_addr_is_broadcast(cap[0].data));

    // 在这之间多做几次分配/释放：如果队列里是悬空指针，内容会被覆盖
    for (int i = 0; i < 4; i++) {
        net::Netbuf *junk = net::Netbuf::alloc(64);
        if (junk) {
            memset(net::Netbuf::put(junk, 64), 0xEE, 64);
            net::Netbuf::free(junk);
        }
    }

    inject_arp_reply();

    // ARP 解析完成：排队的数据报被发出，内容完好
    ASSERT_EQ(2, cap_count);
    ASSERT_TRUE(cap_is_ip_proto(1, IP_PROTO_UDP));
    if (cap_is_ip_proto(1, IP_PROTO_UDP)) {
        ASSERT_EQ(0, memcmp(cap[1].data, T_PEER_MAC, 6));
        ASSERT_EQ_U(ETH_HEADER_LEN + IP_HEADER_MIN_LEN + UDP_HEADER_LEN + 4, cap[1].len);
        ASSERT_EQ(0, memcmp(cap_udp_payload(1), payload, 4));
    }

    net::Udp::pcb_free(pcb);
    test_net_teardown();
}

/**
 * 等待队列有上限：对一个不应答的地址连续发包，只保留最近的 ARP_PENDING_MAX 个。
 */
TEST_CASE(test_arp_pending_queue_bounded) {
    test_net_setup();

    udp_pcb_t *pcb = net::Udp::pcb_new();
    ASSERT_NOT_NULL(pcb);
    if (!pcb) { test_net_teardown(); return; }
    net::Udp::bind(pcb, 0, 4000);

    for (uint8_t i = 0; i < 10; i++) {
        net::Netbuf *buf = make_payload(&i, 1);
        if (buf) {
            ASSERT_EQ(0, net::Udp::sendto(pcb, buf, T_PEER, 5000));
            net::Netbuf::free(buf);
        }
    }

    // 重试间隔之内不会重复发 ARP 请求
    ASSERT_EQ(1, cap_count);

    cap_reset();
    inject_arp_reply();

    ASSERT_EQ(ARP_PENDING_MAX, cap_count);
    if (cap_count == ARP_PENDING_MAX) {
        // 保留的是最新的几个，按原顺序发出
        for (int i = 0; i < ARP_PENDING_MAX; i++) {
            ASSERT_TRUE(cap_is_ip_proto(i, IP_PROTO_UDP));
            ASSERT_EQ(10 - ARP_PENDING_MAX + i, cap_udp_payload(i)[0]);
        }
    }

    net::Udp::pcb_free(pcb);
    test_net_teardown();
}

// ============================================================================
// IP：广播、未配置地址、填充、环回
// ============================================================================

/**
 * 受限广播不做 ARP，直接用广播 MAC；接口没有地址时也能发（源地址 0.0.0.0）。
 * 没有地址时的单播仍然失败。
 */
TEST_CASE(test_ip_broadcast_from_unconfigured_interface) {
    test_net_setup();
    test_dev.ip_addr = 0;

    udp_pcb_t *pcb = net::Udp::pcb_new();
    ASSERT_NOT_NULL(pcb);
    if (!pcb) { test_net_teardown(); return; }
    net::Udp::bind(pcb, 0, 68);

    const uint8_t payload[2] = {1, 2};
    net::Netbuf *buf = make_payload(payload, 2);
    if (buf) {
        ASSERT_EQ(0, net::Udp::sendto(pcb, buf, 0xFFFFFFFF, 67));
        net::Netbuf::free(buf);
    }

    ASSERT_EQ(1, cap_count);
    ASSERT_TRUE(cap_is_ip_proto(0, IP_PROTO_UDP));
    if (cap_is_ip_proto(0, IP_PROTO_UDP)) {
        ASSERT_TRUE(mac_addr_is_broadcast(cap[0].data));
        ASSERT_EQ_U(0, cap_ip(0)->src_addr);
        ASSERT_EQ_U(0xFFFFFFFF, cap_ip(0)->dst_addr);
    }

    // 单播需要源地址
    cap_reset();
    buf = make_payload(payload, 2);
    if (buf) {
        ASSERT_EQ(-1, net::Udp::sendto(pcb, buf, T_PEER, 67));
        net::Netbuf::free(buf);
    }
    ASSERT_EQ(0, cap_count);

    // 配置了地址之后，子网定向广播同样不做 ARP
    test_dev.ip_addr = T_LOCAL;
    buf = make_payload(payload, 2);
    if (buf) {
        ASSERT_EQ(0, net::Udp::sendto(pcb, buf, IP_ADDR(10, 99, 0, 255), 67));
        net::Netbuf::free(buf);
    }
    ASSERT_EQ(1, cap_count);
    ASSERT_TRUE(cap_is_ip_proto(0, IP_PROTO_UDP));
    ASSERT_TRUE(mac_addr_is_broadcast(cap[0].data));

    net::Udp::pcb_free(pcb);
    test_net_teardown();
}

/**
 * 短帧的以太网填充不算负载：3 字节的 UDP 数据报被填充到 60 字节的帧里，
 * 应用收到的仍然是 3 字节。
 */
TEST_CASE(test_ip_input_drops_ethernet_padding) {
    test_net_setup();

    udp_pcb_t *pcb = net::Udp::pcb_new();
    ASSERT_NOT_NULL(pcb);
    if (!pcb) { test_net_teardown(); return; }
    net::Udp::bind(pcb, 0, 4001);

    const uint8_t payload[3] = {7, 8, 9};
    inject_udp(5000, 4001, T_LOCAL, payload, 3, false);

    net::Netbuf *got = net::Udp::recv_poll(pcb);
    ASSERT_NOT_NULL(got);
    if (got) {
        ASSERT_EQ_U(3, got->len);
        ASSERT_EQ(0, memcmp(got->data, payload, 3));
        net::Netbuf::free(got);
    }

    net::Udp::pcb_free(pcb);
    test_net_teardown();
}

/**
 * 发给本机地址的数据包不经过网卡，经接收队列回到协议栈。
 */
TEST_CASE(test_ip_loopback_to_own_address) {
    test_net_setup();

    udp_pcb_t *pcb = net::Udp::pcb_new();
    ASSERT_NOT_NULL(pcb);
    if (!pcb) { test_net_teardown(); return; }
    net::Udp::bind(pcb, 0, 4002);

    const uint8_t payload[5] = {'l', 'o', 'o', 'p', '!'};
    net::Netbuf *buf = make_payload(payload, 5);
    if (buf) {
        ASSERT_EQ(0, net::Udp::sendto(pcb, buf, T_LOCAL, 4002));
        net::Netbuf::free(buf);
    }

    ASSERT_EQ(0, cap_count);            // 没有上线
    ASSERT_FALSE(net::Udp::has_data(pcb));  // 还在接收队列里，没有在发送路径里重入
    net::Netdev::poll();

    net::Netbuf *got = net::Udp::recv_poll(pcb);
    ASSERT_NOT_NULL(got);
    if (got) {
        ASSERT_EQ_U(5, got->len);
        ASSERT_EQ(0, memcmp(got->data, payload, 5));
        ASSERT_EQ_U(T_LOCAL, got->src_ip);
        net::Netbuf::free(got);
    }

    net::Udp::pcb_free(pcb);
    test_net_teardown();
}

// ============================================================================
// UDP：接收队列上限
// ============================================================================

TEST_CASE(test_udp_recv_queue_bounded) {
    test_net_setup();

    udp_pcb_t *pcb = net::Udp::pcb_new();
    ASSERT_NOT_NULL(pcb);
    if (!pcb) { test_net_teardown(); return; }
    net::Udp::bind(pcb, 0, 4003);

    for (uint32_t i = 0; i < UDP_RECV_QUEUE_MAX + 10; i++) {
        uint8_t b = (uint8_t)i;
        inject_udp(5000, 4003, T_LOCAL, &b, 1, false);
    }
    ASSERT_EQ_U(UDP_RECV_QUEUE_MAX, pcb->recv_queue_len);

    // 队列里是最先到的那些，顺序不变；取空后还能继续接收
    uint32_t n = 0;
    net::Netbuf *got;
    while ((got = net::Udp::recv_poll(pcb)) != NULL) {
        ASSERT_EQ((uint8_t)n, got->data[0]);
        net::Netbuf::free(got);
        n++;
    }
    ASSERT_EQ_U(UDP_RECV_QUEUE_MAX, n);
    ASSERT_EQ_U(0, pcb->recv_queue_len);

    uint8_t b = 0x5A;
    inject_udp(5000, 4003, T_LOCAL, &b, 1, false);
    got = net::Udp::recv_poll(pcb);
    ASSERT_NOT_NULL(got);
    if (got) {
        ASSERT_EQ(0x5A, got->data[0]);
        net::Netbuf::free(got);
    }

    net::Udp::pcb_free(pcb);
    test_net_teardown();
}

// ============================================================================
// DHCP
// ============================================================================

static uint8_t *dhcp_opt(uint8_t *opt, uint8_t code, uint8_t len, const void *data) {
    *opt++ = code;
    *opt++ = len;
    memcpy(opt, data, len);
    return opt + len;
}

/** 构造服务器应答（OFFER/ACK）并以广播注入 */
static void inject_dhcp_reply(uint32_t xid_net, uint8_t msg_type) {
    static dhcp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.op = DHCP_OP_REPLY;
    pkt.htype = DHCP_HTYPE_ETH;
    pkt.hlen = 6;
    pkt.xid = xid_net;
    pkt.yiaddr = T_OFFERED;
    memcpy(pkt.chaddr, T_MAC, 6);
    pkt.magic = htonl(DHCP_MAGIC_COOKIE);

    uint32_t server = T_PEER;
    uint32_t mask = T_MASK;
    uint32_t lease = htonl(3600);
    uint8_t *opt = pkt.options;
    opt = dhcp_opt(opt, DHCP_OPT_MSG_TYPE, 1, &msg_type);
    opt = dhcp_opt(opt, DHCP_OPT_SERVER_ID, 4, &server);
    opt = dhcp_opt(opt, DHCP_OPT_SUBNET_MASK, 4, &mask);
    opt = dhcp_opt(opt, DHCP_OPT_ROUTER, 4, &server);
    opt = dhcp_opt(opt, DHCP_OPT_LEASE_TIME, 4, &lease);
    *opt++ = DHCP_OPT_END;

    uint32_t len = sizeof(dhcp_packet_t) - sizeof(pkt.options) + (uint32_t)(opt - pkt.options);
    inject_udp(DHCP_SERVER_PORT, DHCP_CLIENT_PORT, 0xFFFFFFFF, (uint8_t *)&pkt, len, true);
}

/** 记录下来的第 i 帧里 DHCP 报文的消息类型（选项 53 紧跟在 magic 之后） */
static uint8_t cap_dhcp_msg_type(int i) {
    dhcp_packet_t *pkt = (dhcp_packet_t *)cap_udp_payload(i);
    return (pkt->options[0] == DHCP_OPT_MSG_TYPE) ? pkt->options[2] : 0;
}

/**
 * 完整的 DISCOVER -> OFFER -> REQUEST -> ACK 流程。
 */
TEST_CASE(test_dhcp_acquires_lease) {
    test_net_setup();

    ASSERT_EQ(0, net::Dhcp::start(&test_dev));
    ASSERT_EQ(DHCP_STATE_SELECTING, net::Dhcp::get_status(&test_dev, NULL));

    // DISCOVER：广播，源地址 0.0.0.0，目的端口 67
    ASSERT_EQ(1, cap_count);
    ASSERT_TRUE(cap_is_ip_proto(0, IP_PROTO_UDP));
    if (!cap_is_ip_proto(0, IP_PROTO_UDP)) {
        net::Dhcp::stop(&test_dev);
        test_net_teardown();
        return;
    }
    ASSERT_TRUE(mac_addr_is_broadcast(cap[0].data));
    ASSERT_EQ_U(0, cap_ip(0)->src_addr);
    ASSERT_EQ_U(0xFFFFFFFF, cap_ip(0)->dst_addr);
    ASSERT_EQ(DHCP_DISCOVER, cap_dhcp_msg_type(0));
    uint32_t xid_net = ((dhcp_packet_t *)cap_udp_payload(0))->xid;

    // OFFER 到达 68 端口后必须进入状态机，并触发 REQUEST
    cap_reset();
    inject_dhcp_reply(xid_net, DHCP_OFFER);
    ASSERT_EQ(DHCP_STATE_REQUESTING, net::Dhcp::get_status(&test_dev, NULL));
    ASSERT_EQ(1, cap_count);
    if (cap_is_ip_proto(0, IP_PROTO_UDP)) {
        ASSERT_EQ(DHCP_REQUEST, cap_dhcp_msg_type(0));
        ASSERT_TRUE(mac_addr_is_broadcast(cap[0].data));
    }

    // ACK：接口配置为租到的地址
    inject_dhcp_reply(xid_net, DHCP_ACK);
    dhcp_info_t info;
    ASSERT_EQ(DHCP_STATE_BOUND, net::Dhcp::get_status(&test_dev, &info));
    ASSERT_EQ_U(T_OFFERED, test_dev.ip_addr);
    ASSERT_EQ_U(T_MASK, test_dev.netmask);
    ASSERT_EQ_U(T_PEER, test_dev.gateway);
    ASSERT_EQ_U(3600, info.lease_time);

    net::Dhcp::stop(&test_dev);
    test_net_teardown();
}

/**
 * DISCOVER 发不出去时，start 失败但不留下后遗症：原地址还在，
 * 客户端槽位已释放（可以再次 start）。还没拿到租约就 stop 也恢复原地址。
 */
TEST_CASE(test_dhcp_start_failure_rolls_back) {
    test_net_setup();

    tx_fail = true;
    ASSERT_EQ(-1, net::Dhcp::start(&test_dev));
    ASSERT_EQ_U(T_LOCAL, test_dev.ip_addr);
    ASSERT_EQ(DHCP_STATE_ERROR, net::Dhcp::get_status(&test_dev, NULL));  // 没有客户端

    tx_fail = false;
    ASSERT_EQ(0, net::Dhcp::start(&test_dev));
    ASSERT_EQ(-1, net::Dhcp::start(&test_dev));  // 已经在运行
    ASSERT_EQ_U(0, test_dev.ip_addr);

    net::Dhcp::stop(&test_dev);
    ASSERT_EQ_U(T_LOCAL, test_dev.ip_addr);

    test_net_teardown();
}

// ============================================================================
// TCP：主动连接
// ============================================================================

#define T_PEER_PORT     80
#define T_PEER_ISS      1000u

static uint8_t tcp_pattern[9000];
static uint8_t tcp_readback[9000];

/**
 * 握手、重复 ACK 判定、快速重传、分段发送、接收窗口、关闭。
 * 对端的段都按以太网最小帧长填充。
 */
TEST_CASE(test_tcp_client_connection) {
    test_net_setup();
    net::Arp::cache_update(T_PEER, T_PEER_MAC);
    for (uint32_t i = 0; i < sizeof(tcp_pattern); i++) {
        tcp_pattern[i] = (uint8_t)(i * 7 + 3);
    }

    tcp_pcb_t *pcb = net::Tcp::pcb_new();
    ASSERT_NOT_NULL(pcb);
    if (!pcb) { test_net_teardown(); return; }

    // ---- 握手 ----
    ASSERT_EQ(0, net::Tcp::connect(pcb, T_PEER, T_PEER_PORT));
    uint16_t lport = pcb->local_port;
    uint32_t iss = pcb->iss;

    ASSERT_EQ(1, cap_count);
    ASSERT_TRUE(cap_is_ip_proto(0, IP_PROTO_TCP));
    if (!cap_is_ip_proto(0, IP_PROTO_TCP)) {
        net::Tcp::pcb_free(pcb);
        test_net_teardown();
        return;
    }
    ASSERT_EQ(TCP_FLAG_SYN, cap_tcp(0)->flags);
    ASSERT_EQ_U(iss, ntohl(cap_tcp(0)->seq_num));

    // SYN+ACK 只有 40 字节的 IP 报文，被填充到 60 字节：校验和不能把填充算进去
    cap_reset();
    inject_tcp(T_PEER_PORT, lport, T_PEER_ISS, iss + 1, TCP_FLAG_SYN | TCP_FLAG_ACK, 65535, NULL, 0);
    ASSERT_EQ(TCP_ESTABLISHED, pcb->state);
    ASSERT_EQ_U(T_PEER_ISS + 1, pcb->rcv_nxt);
    // 握手完成后 SYN 不能留在重传队列里
    ASSERT_NULL(pcb->unacked);
    ASSERT_EQ_U(0, pcb->timer_retransmit);
    ASSERT_EQ(1, cap_count);
    if (cap_is_ip_proto(0, IP_PROTO_TCP)) {
        ASSERT_EQ(TCP_FLAG_ACK, cap_tcp(0)->flags);
        ASSERT_EQ_U(T_PEER_ISS + 1, ntohl(cap_tcp(0)->ack_num));
    }

    // ---- 重复 ACK 与快速重传 ----
    cap_reset();
    ASSERT_EQ(100, net::Tcp::write(pcb, tcp_pattern, 100));
    ASSERT_EQ(1, cap_count);
    ASSERT_EQ_U(iss + 101, pcb->snd_nxt);
    ASSERT_NOT_NULL(pcb->unacked);

    // 对端的普通数据段（ack 没前进）不是重复 ACK
    uint32_t peer_seq = T_PEER_ISS + 1;
    for (int i = 0; i < 3; i++) {
        inject_tcp(T_PEER_PORT, lport, peer_seq, iss + 1, TCP_FLAG_ACK, 65535,
                   tcp_pattern + (peer_seq - (T_PEER_ISS + 1)), 10);
        peer_seq += 10;
    }
    ASSERT_EQ_U(0, pcb->dup_ack_count);
    ASSERT_EQ_U(iss + 101, pcb->snd_nxt);
    ASSERT_EQ_U(peer_seq, pcb->rcv_nxt);
    ASSERT_EQ_U(30, pcb->recv_len);

    // 三个真正的重复 ACK：以原序列号重传，snd_nxt 不动，队列里不多出一段
    cap_reset();
    for (int i = 0; i < 3; i++) {
        inject_tcp(T_PEER_PORT, lport, peer_seq, iss + 1, TCP_FLAG_ACK, 65535, NULL, 0);
    }
    ASSERT_EQ(1, cap_count);
    if (cap_is_ip_proto(0, IP_PROTO_TCP)) {
        ASSERT_EQ_U(iss + 1, ntohl(cap_tcp(0)->seq_num));
        ASSERT_EQ_U(100, cap_tcp_data_len(0));
        ASSERT_EQ(0, memcmp(cap_tcp_data(0), tcp_pattern, 100));
    }
    ASSERT_EQ_U(iss + 101, pcb->snd_nxt);
    ASSERT_NOT_NULL(pcb->unacked);
    if (pcb->unacked) {
        ASSERT_NULL(pcb->unacked->next);
    }

    // 确认这 100 字节，同时把对端窗口缩到 1000
    inject_tcp(T_PEER_PORT, lport, peer_seq, iss + 101, TCP_FLAG_ACK, 1000, NULL, 0);
    ASSERT_NULL(pcb->unacked);
    ASSERT_EQ_U(1000, pcb->snd_wnd);

    // ---- 大于一个 MSS 的写入 ----
    cap_reset();
    uint32_t base = iss + 101;
    ASSERT_EQ(4000, net::Tcp::write(pcb, tcp_pattern, 4000));
    // 窗口只有 1000：先发 1000 字节，其余留在发送缓冲区
    ASSERT_EQ(1, cap_count);
    ASSERT_EQ_U(base + 1000, pcb->snd_nxt);
    ASSERT_EQ_U(3000, pcb->send_len);

    // 对端确认并打开窗口：剩余数据继续发出
    inject_tcp(T_PEER_PORT, lport, peer_seq, base + 1000, TCP_FLAG_ACK, 65535, NULL, 0);
    for (int round = 0; round < 8 && pcb->snd_una != pcb->snd_nxt; round++) {
        inject_tcp(T_PEER_PORT, lport, peer_seq, pcb->snd_nxt, TCP_FLAG_ACK, 65535, NULL, 0);
    }
    ASSERT_EQ_U(0, pcb->send_len);
    ASSERT_EQ_U(base + 4000, pcb->snd_nxt);
    ASSERT_NULL(pcb->unacked);

    // 线上的字节流连续、不重不漏，内容正确，每段不超过 MSS
    memset(tcp_readback, 0, 4000);
    uint32_t sent_bytes = 0;
    bool stream_ok = true;
    for (int i = 0; i < cap_count && i < CAP_MAX; i++) {
        if (!cap_is_ip_proto(i, IP_PROTO_TCP)) continue;
        uint32_t dlen = cap_tcp_data_len(i);
        if (dlen == 0) continue;
        uint32_t off = ntohl(cap_tcp(i)->seq_num) - base;
        if (off != sent_bytes || dlen > TCP_DEFAULT_MSS || off + dlen > 4000) {
            stream_ok = false;
            break;
        }
        memcpy(tcp_readback + off, cap_tcp_data(i), dlen);
        sent_bytes += dlen;
    }
    ASSERT_TRUE(stream_ok);
    ASSERT_EQ_U(4000, sent_bytes);
    ASSERT_EQ(0, memcmp(tcp_readback, tcp_pattern, 4000));

    // ---- 接收缓冲区满 ----
    ASSERT_EQ(30, net::Tcp::read(pcb, tcp_readback, 64));
    ASSERT_EQ(0, memcmp(tcp_readback, tcp_pattern, 30));
    ASSERT_EQ_U(0, pcb->recv_len);

    uint32_t rbase = peer_seq;            // 接下来对端数据的起始序列号
    uint32_t bufsz = pcb->recv_buf_size;  // 8192
    cap_reset();
    for (uint32_t off = 0; off < 9000; off += 1000) {
        inject_tcp(T_PEER_PORT, lport, rbase + off, pcb->snd_nxt, TCP_FLAG_ACK, 65535,
                   tcp_pattern + off, 1000);
    }
    // 放不下的部分没有被确认：rcv_nxt 只前进了存下来的字节数
    ASSERT_EQ_U(bufsz, pcb->recv_len);
    ASSERT_EQ_U(rbase + bufsz, pcb->rcv_nxt);
    int last = cap_last_tcp(T_PEER_PORT);
    ASSERT_TRUE(last >= 0);
    if (last >= 0) {
        ASSERT_EQ_U(rbase + bufsz, ntohl(cap_tcp(last)->ack_num));
        ASSERT_EQ_U(0, ntohs(cap_tcp(last)->window));
    }

    // 读走一半：窗口重新打开并通告给对端
    cap_reset();
    ASSERT_EQ(4096, net::Tcp::read(pcb, tcp_readback, 4096));
    ASSERT_EQ(1, cap_count);
    if (cap_is_ip_proto(0, IP_PROTO_TCP)) {
        ASSERT_EQ_U(4096, ntohs(cap_tcp(0)->window));
    }

    // 对端重传没被确认的那一段（和已收到的部分重叠）
    inject_tcp(T_PEER_PORT, lport, rbase + 8000, pcb->snd_nxt, TCP_FLAG_ACK, 65535,
               tcp_pattern + 8000, 1000);
    ASSERT_EQ_U(rbase + 9000, pcb->rcv_nxt);

    // 应用读到的 9000 字节完整、有序
    uint32_t got = 4096;
    while (got < 9000) {
        int n = net::Tcp::read(pcb, tcp_readback + got, 9000 - got);
        if (n <= 0) break;
        got += (uint32_t)n;
    }
    ASSERT_EQ_U(9000, got);
    ASSERT_EQ(0, memcmp(tcp_readback, tcp_pattern, 9000));

    // ---- 关闭 ----
    cap_reset();
    ASSERT_EQ(0, net::Tcp::close(pcb));
    ASSERT_EQ(TCP_FIN_WAIT_1, pcb->state);
    last = cap_last_tcp(T_PEER_PORT);
    ASSERT_TRUE(last >= 0);
    if (last >= 0) {
        ASSERT_TRUE((cap_tcp(last)->flags & TCP_FLAG_FIN) != 0);
    }

    // FIN 被确认后不再留在重传队列里
    inject_tcp(T_PEER_PORT, lport, rbase + 9000, pcb->snd_nxt, TCP_FLAG_ACK, 65535, NULL, 0);
    ASSERT_EQ(TCP_FIN_WAIT_2, pcb->state);
    ASSERT_NULL(pcb->unacked);

    inject_tcp(T_PEER_PORT, lport, rbase + 9000, pcb->snd_nxt,
               TCP_FLAG_FIN | TCP_FLAG_ACK, 65535, NULL, 0);
    ASSERT_EQ(TCP_TIME_WAIT, pcb->state);
    ASSERT_EQ_U(rbase + 9001, pcb->rcv_nxt);

    net::Tcp::pcb_free(pcb);
    test_net_teardown();
}

/**
 * 发送失败（驱动出错）不能在序列空间里留下空洞：段留在重传队列里，
 * 由重传定时器补发。
 */
TEST_CASE(test_tcp_send_failure_is_retransmitted) {
    test_net_setup();
    net::Arp::cache_update(T_PEER, T_PEER_MAC);

    tcp_pcb_t *pcb = net::Tcp::pcb_new();
    ASSERT_NOT_NULL(pcb);
    if (!pcb) { test_net_teardown(); return; }

    tx_fail = true;
    ASSERT_EQ(0, net::Tcp::connect(pcb, T_PEER, T_PEER_PORT));
    tx_fail = false;

    // SYN 没发出去，但在重传队列里，序列号是 iss
    ASSERT_EQ(TCP_SYN_SENT, pcb->state);
    ASSERT_EQ_U(pcb->iss + 1, pcb->snd_nxt);
    ASSERT_NOT_NULL(pcb->unacked);
    if (pcb->unacked) {
        ASSERT_EQ_U(pcb->iss, pcb->unacked->seq);
        ASSERT_TRUE(pcb->timer_retransmit != 0);

        // 让重传定时器到期
        pcb->timer_retransmit = 1;
        cap_reset();
        net::Tcp::timer();
        ASSERT_EQ(1, cap_count);
        if (cap_is_ip_proto(0, IP_PROTO_TCP)) {
            // 重传的 SYN 用原序列号，而且不带 ACK
            ASSERT_EQ(TCP_FLAG_SYN, cap_tcp(0)->flags);
            ASSERT_EQ_U(pcb->iss, ntohl(cap_tcp(0)->seq_num));
        }
    }

    net::Tcp::pcb_free(pcb);
    test_net_teardown();
}

// ============================================================================
// TCP：监听
// ============================================================================

#define T_LISTEN_PORT   8080

/**
 * 监听端口收到 SYN 不会死锁；半连接被 RST 后归还 backlog；backlog 同时约束
 * 握手中和等待 accept 的连接；释放监听 PCB 时回收它名下的子连接。
 */
TEST_CASE(test_tcp_listen_backlog_and_teardown) {
    test_net_setup();
    net::Arp::cache_update(T_PEER, T_PEER_MAC);

    // 一个无关的活动 PCB：新连接到来之后它必须还在活动链表上
    tcp_pcb_t *other = net::Tcp::pcb_new();
    tcp_pcb_t *lp = net::Tcp::pcb_new();
    ASSERT_NOT_NULL(other);
    ASSERT_NOT_NULL(lp);
    if (!other || !lp) {
        net::Tcp::pcb_free(other);
        net::Tcp::pcb_free(lp);
        test_net_teardown();
        return;
    }
    ASSERT_EQ(0, net::Tcp::bind(other, 0, 9090));
    ASSERT_EQ(0, net::Tcp::bind(lp, 0, T_LISTEN_PORT));
    ASSERT_EQ(0, net::Tcp::listen(lp, 2));

    // SYN -> SYN+ACK（以前在这里对 tcp_lock 自死锁）
    inject_tcp(5555, T_LISTEN_PORT, 7000, 0, TCP_FLAG_SYN, 65535, NULL, 0);
    ASSERT_EQ(1, lp->pending_count);
    int idx = cap_last_tcp(5555);
    ASSERT_TRUE(idx >= 0);
    if (idx >= 0) {
        ASSERT_EQ(TCP_FLAG_SYN | TCP_FLAG_ACK, cap_tcp(idx)->flags);
        ASSERT_EQ_U(7001, ntohl(cap_tcp(idx)->ack_num));
    }

    // 活动链表没有被截断：9090 仍然被 other 占着
    tcp_pcb_t *probe = net::Tcp::pcb_new();
    ASSERT_NOT_NULL(probe);
    if (probe) {
        ASSERT_EQ(-1, net::Tcp::bind(probe, 0, 9090));
        net::Tcp::pcb_free(probe);
    }

    // 对端放弃半连接：backlog 名额归还
    inject_tcp(5555, T_LISTEN_PORT, 7001, 0, TCP_FLAG_RST, 0, NULL, 0);
    ASSERT_EQ(0, lp->pending_count);

    // 两个半连接占满 backlog，第三个 SYN 被忽略
    inject_tcp(5556, T_LISTEN_PORT, 7000, 0, TCP_FLAG_SYN, 65535, NULL, 0);
    inject_tcp(5557, T_LISTEN_PORT, 7000, 0, TCP_FLAG_SYN, 65535, NULL, 0);
    ASSERT_EQ(2, lp->pending_count);
    cap_reset();
    inject_tcp(5558, T_LISTEN_PORT, 7000, 0, TCP_FLAG_SYN, 65535, NULL, 0);
    ASSERT_EQ(0, cap_count);
    ASSERT_EQ(2, lp->pending_count);

    // 5556 完成握手：进入 accept 队列，仍然占着 backlog
    tcp_pcb_t *half = lp->pending_queue;
    while (half && half->remote_port != 5556) {
        half = half->queue_next;
    }
    ASSERT_NOT_NULL(half);
    if (half) {
        inject_tcp(5556, T_LISTEN_PORT, 7001, half->iss + 1, TCP_FLAG_ACK, 65535, NULL, 0);
    }
    ASSERT_EQ(1, lp->pending_count);
    ASSERT_EQ(1, lp->accept_count);
    inject_tcp(5558, T_LISTEN_PORT, 7000, 0, TCP_FLAG_SYN, 65535, NULL, 0);
    ASSERT_EQ(0, cap_count);

    tcp_pcb_t *child = net::Tcp::accept(lp);
    ASSERT_NOT_NULL(child);
    ASSERT_EQ(0, lp->accept_count);
    if (child) {
        ASSERT_EQ(TCP_ESTABLISHED, child->state);
        ASSERT_EQ(5556, child->remote_port);
        // SYN+ACK 已被确认，不会再被重传
        ASSERT_NULL(child->unacked);
        net::Tcp::pcb_free(child);
    }

    // 释放监听 PCB：还在握手的 5557 被重置并回收
    cap_reset();
    net::Tcp::pcb_free(lp);
    idx = cap_last_tcp(5557);
    ASSERT_TRUE(idx >= 0);
    if (idx >= 0) {
        ASSERT_TRUE((cap_tcp(idx)->flags & TCP_FLAG_RST) != 0);
    }

    // 迟到的握手 ACK 找不到连接，得到 RST（不会去写已释放的监听 PCB）
    cap_reset();
    inject_tcp(5557, T_LISTEN_PORT, 7001, 12345, TCP_FLAG_ACK, 65535, NULL, 0);
    idx = cap_last_tcp(5557);
    ASSERT_TRUE(idx >= 0);
    if (idx >= 0) {
        ASSERT_TRUE((cap_tcp(idx)->flags & TCP_FLAG_RST) != 0);
    }

    net::Tcp::pcb_free(other);
    test_net_teardown();
}

// ============================================================================
// 测试套件
// ============================================================================

TEST_SUITE(netstack_arp_tests) {
    RUN_TEST(test_arp_pending_packet_survives_caller_free);
    RUN_TEST(test_arp_pending_queue_bounded);
}

TEST_SUITE(netstack_ip_tests) {
    RUN_TEST(test_ip_broadcast_from_unconfigured_interface);
    RUN_TEST(test_ip_input_drops_ethernet_padding);
    RUN_TEST(test_ip_loopback_to_own_address);
}

TEST_SUITE(netstack_udp_tests) {
    RUN_TEST(test_udp_recv_queue_bounded);
}

TEST_SUITE(netstack_dhcp_tests) {
    RUN_TEST(test_dhcp_acquires_lease);
    RUN_TEST(test_dhcp_start_failure_rolls_back);
}

TEST_SUITE(netstack_tcp_tests) {
    RUN_TEST(test_tcp_client_connection);
    RUN_TEST(test_tcp_send_failure_is_retransmitted);
    RUN_TEST(test_tcp_listen_backlog_and_teardown);
}

void run_netstack_tests(void) {
    RUN_SUITE(netstack_arp_tests);
    RUN_SUITE(netstack_ip_tests);
    RUN_SUITE(netstack_udp_tests);
    RUN_SUITE(netstack_dhcp_tests);
    RUN_SUITE(netstack_tcp_tests);
}
