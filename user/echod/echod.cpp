// echod - TCP 回显服务：把收到的数据原样发回去
//
//   echod [port] [connections]
//
// 默认在 7 号端口上服务 1 条连接然后退出（命令行没有后台任务，服务期间它是被占住的）。
// 从宿主机连进来需要 QEMU 的端口转发，例如 -netdev user,...,hostfwd=tcp:127.0.0.1:8007-:7

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <net.h>

int main(int argc, char **argv) {
    int port = argc > 1 ? atoi(argv[1]) : 7;
    int count = argc > 2 ? atoi(argv[2]) : 1;

    int listener = net_tcp_listen((uint16_t)port);
    if (listener < 0) {
        printf("echod: cannot listen on port %d\n", port);
        return 1;
    }
    printf("echod: listening on port %d\n", port);

    static char buf[2048];
    for (int i = 0; i < count; i++) {
        uint32_t peer_ip = 0;
        uint16_t peer_port = 0;
        int conn = net_tcp_accept(listener, 60000, &peer_ip, &peer_port);
        if (conn < 0) {
            printf("echod: nobody connected within a minute\n");
            break;
        }
        char text[16];
        net_format_ip(peer_ip, text);
        printf("echod: connection from %s:%u\n", text, peer_port);

        unsigned total = 0;
        long n;
        while ((n = net_tcp_recv(conn, buf, sizeof(buf), 60000)) > 0) {
            if (net_tcp_send(conn, buf, (size_t)n) != n) {
                break;
            }
            total += (unsigned)n;
        }
        net_tcp_close(conn);
        printf("echod: connection closed, %u bytes echoed\n", total);
    }
    net_tcp_close(listener);
    return 0;
}
