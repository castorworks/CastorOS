// dns - 用 UDP 向 DNS 服务器查询一个主机名的 IPv4 地址
//
//   dns <name> [server]
//
// 默认的服务器 10.0.2.3 是 QEMU 用户网络自带的转发器，它把查询交给宿主机的解析器，
// 所以能不能查到取决于宿主机能不能上网。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <net.h>

static uint8_t query[512], answer[512];

// 把 "www.example.com" 写成 DNS 的标签格式，返回写了多少字节（失败返回 0）
static size_t encode_name(const char *name, uint8_t *out, size_t max) {
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
static size_t skip_name(const uint8_t *msg, size_t len, size_t pos) {
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

int main(int argc, char **argv) {
    uint32_t server = NET_IP(10, 0, 2, 3);
    if (argc < 2 || (argc > 2 && net_parse_ip(argv[2], &server) != 0)) {
        printf("usage: dns <name> [server]\n");
        return 1;
    }
    int sock = net_udp_open(0);
    if (sock < 0) {
        printf("dns: no network\n");
        return 1;
    }

    // 12 字节的头：ID、标志（要求递归）、一个问题
    uint16_t id = (uint16_t)(0x4300 | (getpid() & 0xFF));
    memset(query, 0, sizeof(query));
    query[0] = (uint8_t)(id >> 8);
    query[1] = (uint8_t)id;
    query[2] = 0x01;
    query[5] = 1;
    size_t n = encode_name(argv[1], query + 12, sizeof(query) - 16);
    if (n == 0) {
        printf("dns: bad name\n");
        return 1;
    }
    size_t qlen = 12 + n;
    query[qlen + 1] = 1;    // 类型 A
    query[qlen + 3] = 1;    // 类 IN
    qlen += 4;

    long len = -1;
    for (int attempt = 0; attempt < 2 && len < 0; attempt++) {
        if (net_udp_send(sock, server, 53, query, qlen) != 0) {
            break;
        }
        len = net_udp_recv(sock, answer, sizeof(answer), 2000, NULL, NULL);
    }
    net_udp_close(sock);
    if (len < 12 || answer[0] != query[0] || answer[1] != query[1]) {
        printf("dns: no answer from the server\n");
        return 1;
    }
    if ((answer[3] & 0x0F) != 0) {
        printf("dns: %s: not found (rcode %d)\n", argv[1], answer[3] & 0x0F);
        return 1;
    }

    // 跳过问题，在回答里找第一条 A 记录
    size_t pos = 12;
    int questions = (answer[4] << 8) | answer[5];
    int answers = (answer[6] << 8) | answer[7];
    for (int i = 0; i < questions; i++) {
        pos = skip_name(answer, (size_t)len, pos) + 4;
    }
    for (int i = 0; i < answers && pos + 10 <= (size_t)len; i++) {
        pos = skip_name(answer, (size_t)len, pos);
        if (pos + 10 > (size_t)len) {
            break;
        }
        int type = (answer[pos] << 8) | answer[pos + 1];
        size_t rdlen = (size_t)((answer[pos + 8] << 8) | answer[pos + 9]);
        pos += 10;
        if (type == 1 && rdlen == 4 && pos + 4 <= (size_t)len) {
            printf("%s has address %d.%d.%d.%d\n", argv[1], answer[pos], answer[pos + 1],
                   answer[pos + 2], answer[pos + 3]);
            return 0;
        }
        pos += rdlen;
    }
    printf("dns: %s: no address in the answer\n", argv[1]);
    return 1;
}
