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

/** 确保有共享缓冲区（UDP 和 TCP 收发数据时需要） */
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
    info->dns = (uint32_t)m.data[5];
    info->dhcp = (m.data[1] >> 48) & 1;
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

int net_tcp_connect(uint32_t ip, uint16_t port, uint32_t timeout_ms) {
    if (!net_connect()) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = NET_TCP_CONNECT;
    m.data[0] = ip;
    m.data[1] = port;
    m.data[2] = timeout_ms;
    return (int)net_request(&m);
}

long net_tcp_send(int conn, const void *data, size_t len) {
    if (!net_connect()) {
        return -1;
    }
    size_t done = 0;
    while (done < len) {
        size_t chunk = len - done > NET_BUF_SIZE ? NET_BUF_SIZE : len - done;
        memcpy(net_buf, (const char *)data + done, chunk);
        struct ipc_msg m = {};
        m.label = NET_TCP_SEND;
        m.data[0] = (uint64_t)conn;
        m.data[1] = chunk;
        long n = net_request(&m);       // 服务可能只收下一部分
        if (n <= 0) {
            return -1;
        }
        done += (size_t)n;
    }
    return (long)done;
}

long net_tcp_recv(int conn, void *buf, size_t len, uint32_t timeout_ms) {
    if (!net_connect() || len == 0) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = NET_TCP_RECV;
    m.data[0] = (uint64_t)conn;
    m.data[1] = len > NET_BUF_SIZE ? NET_BUF_SIZE : len;
    m.data[2] = timeout_ms;
    long n = net_request(&m);
    if (n > 0) {
        memcpy(buf, net_buf, (size_t)n);
    }
    return n;
}

int net_tcp_close(int conn) {
    if (!net_find()) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = NET_TCP_CLOSE;
    m.data[0] = (uint64_t)conn;
    return (int)net_request(&m);
}

int net_tcp_listen(uint16_t port) {
    if (!net_connect()) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = NET_TCP_LISTEN;
    m.data[0] = port;
    return (int)net_request(&m);
}

int net_tcp_accept(int listener, uint32_t timeout_ms, uint32_t *peer_ip, uint16_t *peer_port) {
    if (!net_connect()) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = NET_TCP_ACCEPT;
    m.data[0] = (uint64_t)listener;
    m.data[1] = timeout_ms;
    long conn = net_request(&m);
    if (conn < 0) {
        return -1;
    }
    if (peer_ip) {
        *peer_ip = (uint32_t)m.data[1];
    }
    if (peer_port) {
        *peer_port = (uint16_t)m.data[2];
    }
    return (int)conn;
}

long net_debug_drop(uint32_t drop_tx, uint32_t drop_rx) {
    if (!net_find()) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = NET_DEBUG_DROP;
    m.data[0] = drop_tx;
    m.data[1] = drop_rx;
    if (net_request(&m) != 0) {
        return -1;
    }
    return (long)m.data[1];
}

int net_debug_fragment(uint32_t max_payload) {
    if (!net_find()) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = NET_DEBUG_FRAGMENT;
    m.data[0] = max_payload;
    return net_request(&m) == 0 ? 0 : -1;
}

long net_debug_renew(void) {
    if (!net_find()) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = NET_DEBUG_RENEW;
    if (net_request(&m) != 0 || (int64_t)m.data[0] != 0) {
        return -1;
    }
    return (long)m.data[1];
}

// ---------------------------------------------------------------------------
// 名字解析：向配置的 DNS 服务器发一个 A 记录查询
// ---------------------------------------------------------------------------

// 把 "www.example.com" 写成 DNS 的标签格式，返回写了多少字节（失败返回 0）
static size_t dns_encode_name(const char *name, uint8_t *out, size_t max) {
    size_t pos = 0;
    while (*name) {
        const char *dot = strchr(name, '.');
        size_t len = dot ? (size_t)(dot - name) : strlen(name);
        if (len == 0 || len > 63 || pos + len + 2 > max) {
            return 0;
        }
        out[pos++] = (uint8_t)len;
        memcpy(out + pos, name, len);
        pos += len;
        name += len;
        if (*name == '.') {
            name++;
        }
    }
    out[pos++] = 0;
    return pos;
}

// 跳过应答里的一个名字（可能是压缩指针），返回它后面的位置
static size_t dns_skip_name(const uint8_t *msg, size_t len, size_t pos) {
    while (pos < len) {
        uint8_t n = msg[pos];
        if (n == 0) {
            return pos + 1;
        }
        if ((n & 0xC0) == 0xC0) {
            return pos + 2;
        }
        pos += (size_t)n + 1;
    }
    return len;
}

int net_resolve(const char *name, uint32_t *ip) {
    if (net_parse_ip(name, ip) == 0) {
        return 0;
    }
    struct net_info info;
    if (net_info(&info) != 0 || info.dns == 0) {
        return -1;
    }
    int sock = net_udp_open(0);
    if (sock < 0) {
        return -1;
    }

    // 12 字节的头：ID、标志（要求递归）、一个问题
    static uint8_t query[512], answer[512];
    uint16_t id = (uint16_t)(0x4300 | (getpid() & 0xFF));
    memset(query, 0, sizeof(query));
    query[0] = (uint8_t)(id >> 8);
    query[1] = (uint8_t)id;
    query[2] = 0x01;
    query[5] = 1;
    size_t n = dns_encode_name(name, query + 12, sizeof(query) - 16);
    if (n == 0) {
        net_udp_close(sock);
        return -1;
    }
    size_t qlen = 12 + n;
    query[qlen + 1] = 1;    // 类型 A
    query[qlen + 3] = 1;    // 类 IN
    qlen += 4;

    long len = -1;
    for (int attempt = 0; attempt < 2 && len < 0; attempt++) {
        if (net_udp_send(sock, info.dns, 53, query, qlen) != 0) {
            break;
        }
        len = net_udp_recv(sock, answer, sizeof(answer), 2000, NULL, NULL);
    }
    net_udp_close(sock);
    if (len < 12 || answer[0] != query[0] || answer[1] != query[1] || (answer[3] & 0x0F) != 0) {
        return -1;
    }

    // 跳过问题，在回答里找第一条 A 记录
    size_t pos = 12;
    int questions = (answer[4] << 8) | answer[5];
    int answers = (answer[6] << 8) | answer[7];
    for (int i = 0; i < questions; i++) {
        pos = dns_skip_name(answer, (size_t)len, pos) + 4;
    }
    for (int i = 0; i < answers && pos + 10 <= (size_t)len; i++) {
        pos = dns_skip_name(answer, (size_t)len, pos);
        if (pos + 10 > (size_t)len) {
            break;
        }
        int type = (answer[pos] << 8) | answer[pos + 1];
        size_t rdlen = (size_t)((answer[pos + 8] << 8) | answer[pos + 9]);
        pos += 10;
        if (type == 1 && rdlen == 4 && pos + 4 <= (size_t)len) {
            *ip = NET_IP(answer[pos], answer[pos + 1], answer[pos + 2], answer[pos + 3]);
            return 0;
        }
        pos += rdlen;
    }
    return -1;
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
