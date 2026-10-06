// udp.cpp - UDP：套接字表、收发数据报、NET_UDP_* 请求
//
// 一个套接字属于打开它的客户进程，绑定一个端口，积压几个收到的数据报。数据经客户和
// 服务之间的共享缓冲区传递（clients.h）。DHCP 客户端（dhcp.cpp）不走套接字：它的端口
// 上收到的数据报直接交给 dhcp_input，发的时候用 udp_send_raw。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <net.h>
#include <clients.h>
#include "net_internal.h"

#define UDP_HDR         8
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

void udp_drop_owner(int pid) {
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

uint32_t pseudo_sum(uint32_t src, uint32_t dst, uint8_t protocol, size_t len) {
    return (src >> 16) + (src & 0xFFFF) + (dst >> 16) + (dst & 0xFFFF) + protocol + (uint32_t)len;
}

void udp_send_raw(uint16_t src_port, uint32_t dst, uint16_t dst_port, const uint8_t *data, size_t len) {
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
    uint16_t sum = checksum(msg, UDP_HDR + len, pseudo_sum(my_ip, dst, IP_PROTO_UDP, UDP_HDR + len));
    sum = swap16(sum == 0 ? 0xFFFF : sum);      // 算出来是 0 时按规定发全 1
    memcpy(msg + 6, &sum, 2);
    ip_send(dst, IP_PROTO_UDP, msg, UDP_HDR + len);
}

void udp_input(uint32_t src, uint32_t dst, const uint8_t *data, size_t len) {
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
    if (h.checksum != 0 && checksum(data, total, pseudo_sum(src, dst, IP_PROTO_UDP, total)) != 0) {
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

void udp_request(const struct ipc_msg *m) {
    int pid = (int)m->sender;
    struct ipc_msg reply = {};
    reply.label = m->label;
    int64_t result = -1;

    if (m->label == NET_UDP_OPEN) {
        result = udp_open(pid, (uint16_t)m->data[0], &reply.data[1]);
    } else {
        // 其余的请求都针对一个已经打开的套接字
        struct udp_socket *s = socket_of(pid, m->data[0]);
        if (s && m->label == NET_UDP_CLOSE) {
            s->owner = 0;
            result = 0;
        } else if (s && m->label == NET_UDP_SEND) {
            char *buf = clients_buf(pid);
            if (buf && my_ip != 0 && m->data[3] <= NET_UDP_MAX && m->data[2] != 0 && m->data[2] <= 0xFFFF) {
                // 先应答再发：发给自己的数据报会立刻进到某个套接字，那里可能又要应答别的客户
                reply_client(pid, NET_UDP_SEND, 0, 0, 0);
                udp_send(s, (uint32_t)m->data[1], (uint16_t)m->data[2], buf, (size_t)m->data[3]);
                return;
            }
        } else if (s && m->label == NET_UDP_RECV) {
            if (s->count > 0) {
                udp_deliver(s, pid);
                return;
            }
            if (m->data[1] != 0) {
                // 等数据报到来（udp_input）或者超时（udp_tick）时再应答
                s->waiting = pid;
                s->deadline = uptime_ms() + m->data[1];
                return;
            }
        }
    }

    reply.data[0] = (uint64_t)result;
    ipc_reply(pid, &reply);
}

bool udp_tick(uint64_t now) {
    bool waiting = false;
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
