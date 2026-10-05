// http - 用 TCP 对一个网站做一次 HTTP GET，打印应答的开头
//
//   http <host> [path]
//
// 只支持不加密的 HTTP（80 端口）。能不能连上取决于宿主机能不能上网。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <net.h>

#define SHOW_MAX 600        // 最多显示应答的前这么多字节

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: http <host> [path]\n");
        return 1;
    }
    const char *host = argv[1];
    const char *path = argc > 2 ? argv[2] : "/";

    uint32_t ip;
    if (net_resolve(host, &ip) != 0) {
        printf("http: cannot resolve %s\n", host);
        return 1;
    }
    char text[16];
    net_format_ip(ip, text);
    printf("connecting to %s (%s) port 80\n", host, text);

    int conn = net_tcp_connect(ip, 80, 5000);
    if (conn < 0) {
        printf("http: connection failed\n");
        return 1;
    }

    static char request[512];
    int n = snprintf(request, sizeof(request),
                     "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: CastorOS\r\nConnection: close\r\n\r\n",
                     path, host);
    if (net_tcp_send(conn, request, (size_t)n) != n) {
        printf("http: send failed\n");
        net_tcp_close(conn);
        return 1;
    }

    // 读到对方关闭为止；只显示开头，其余的只计数
    static char buf[2048];
    size_t total = 0;
    long got;
    while ((got = net_tcp_recv(conn, buf, sizeof(buf), 5000)) > 0) {
        if (total < SHOW_MAX) {
            size_t show = (size_t)got < SHOW_MAX - total ? (size_t)got : SHOW_MAX - total;
            write_out(buf, show);
        }
        total += (size_t)got;
    }
    net_tcp_close(conn);
    printf("\n[%u bytes received%s]\n", (unsigned)total, got < 0 ? ", then the connection timed out" : "");
    return total > 0 ? 0 : 1;
}
