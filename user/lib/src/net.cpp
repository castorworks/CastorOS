/**
 * 网络服务的客户端：把请求发给登记为 "net" 的服务进程
 */

#include <net.h>
#include <names.h>
#include <syscall.h>
#include <stdio.h>
#include <string.h>

static int net_server = 0;
static char *net_buf = NULL;
static int net_owner = 0;       // 建立这条连接的进程：fork 出来的子进程要自己重新建立

/** 找到网络服务。没有时立刻失败（不等待） */
static bool net_find(void) {
    if (net_server > 0 && net_owner == getpid()) {
        return true;
    }
    int server = name_lookup(NET_SERVICE_NAME);
    if (server <= 0) {
        return false;
    }
    net_server = server;
    net_owner = getpid();
    net_buf = NULL;
    return true;
}

/** 确保有共享缓冲区（只有 UDP 收发需要） */
static bool net_connect(void) {
    if (!net_find()) {
        return false;
    }
    if (net_buf) {
        return true;
    }
    char *buf = (char *)mmap(NULL, NET_BUF_SIZE, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) {
        return false;
    }
    if (mem_grant(net_server, buf, NET_BUF_SIZE) != 0) {
        munmap(buf, NET_BUF_SIZE);
        return false;
    }
    net_buf = buf;
    return true;
}

static long net_request(struct ipc_msg *m) {
    if (ipc_call(net_server, m) != 0) {
        return -1;
    }
    return (long)(int64_t)m->data[0];
}

int net_info(struct net_info *info) {
    if (!net_find()) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = NET_INFO;
    if (net_request(&m) != 0) {
        return -1;
    }
    for (int i = 0; i < 6; i++) {
        info->mac[i] = (uint8_t)(m.data[1] >> (8 * i));
    }
    info->ip = (uint32_t)m.data[2];
    info->netmask = (uint32_t)m.data[3];
    info->gateway = (uint32_t)m.data[4];
    return 0;
}

int net_ping(uint32_t ip, uint32_t timeout_ms, uint32_t *rtt_ms) {
    if (!net_find()) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = NET_PING;
    m.data[0] = ip;
    m.data[1] = timeout_ms;
    if (net_request(&m) != 0) {
        return -1;
    }
    if (rtt_ms) {
        *rtt_ms = (uint32_t)m.data[1];
    }
    return 0;
}

int net_udp_open(uint16_t port) {
    if (!net_connect()) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = NET_UDP_OPEN;
    m.data[0] = port;
    return (int)net_request(&m);
}

int net_udp_close(int sock) {
    if (!net_find()) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = NET_UDP_CLOSE;
    m.data[0] = (uint64_t)sock;
    return (int)net_request(&m);
}

int net_udp_send(int sock, uint32_t ip, uint16_t port, const void *data, size_t len) {
    if (!net_connect() || len > NET_UDP_MAX) {
        return -1;
    }
    memcpy(net_buf, data, len);
    struct ipc_msg m = {};
    m.label = NET_UDP_SEND;
    m.data[0] = (uint64_t)sock;
    m.data[1] = ip;
    m.data[2] = port;
    m.data[3] = len;
    return (int)net_request(&m);
}

long net_udp_recv(int sock, void *buf, size_t len, uint32_t timeout_ms,
                  uint32_t *src_ip, uint16_t *src_port) {
    if (!net_connect()) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = NET_UDP_RECV;
    m.data[0] = (uint64_t)sock;
    m.data[1] = timeout_ms;
    long n = net_request(&m);
    if (n < 0) {
        return -1;
    }
    memcpy(buf, net_buf, (size_t)n < len ? (size_t)n : len);
    if (src_ip) {
        *src_ip = (uint32_t)m.data[1];
    }
    if (src_port) {
        *src_port = (uint16_t)m.data[2];
    }
    return n;
}

int net_parse_ip(const char *text, uint32_t *ip) {
    uint32_t result = 0;
    for (int part = 0; part < 4; part++) {
        if (*text < '0' || *text > '9') {
            return -1;
        }
        uint32_t value = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10 + (uint32_t)(*text++ - '0');
            if (value > 255) {
                return -1;
            }
        }
        result = (result << 8) | value;
        if (part < 3 && *text++ != '.') {
            return -1;
        }
    }
    if (*text != '\0') {
        return -1;
    }
    *ip = result;
    return 0;
}

void net_format_ip(uint32_t ip, char *buf) {
    snprintf(buf, 16, "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
}
