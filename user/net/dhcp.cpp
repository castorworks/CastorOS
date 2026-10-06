// dhcp.cpp - DHCP 客户端
//
// 启动时走一遍 DISCOVER -> OFFER -> REQUEST -> ACK。请求里带广播标志，
// 让服务器把应答发到广播地址（我们这时还没有地址）。不续租。
// 等不到应答就退回 QEMU 用户网络（-netdev user）的固定配置。
// 结果写进 net.cpp 里的地址配置（my_ip、netmask、gateway、dns_server）。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <net.h>
#include "net_internal.h"

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

void dhcp_start(void) {
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

void dhcp_input(const uint8_t *data, size_t len) {
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
bool dhcp_tick(uint64_t now) {
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
