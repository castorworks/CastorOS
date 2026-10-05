// ping - 发 ICMP 回显请求
//
//   ping <ip> [count]

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <net.h>

int main(int argc, char **argv) {
    uint32_t ip;
    if (argc < 2 || net_parse_ip(argv[1], &ip) != 0) {
        eprintf("usage: ping <a.b.c.d> [count]\n");
        return 1;
    }
    struct net_info info;
    if (net_info(&info) != 0) {
        eprintf("ping: no network\n");
        return 1;
    }
    int count = argc > 2 ? atoi(argv[2]) : 3;

    int received = 0;
    for (int i = 0; i < count; i++) {
        uint32_t rtt = 0;
        if (net_ping(ip, 1000, &rtt) == 0) {
            // 往返时间量出来是 0：小于时钟的精度（x86 上是一个 10ms 的滴答）
            if (rtt == 0) {
#if defined(ARCH_ARM64)
                printf("reply from %s: seq=%d time<1ms\n", argv[1], i + 1);
#else
                printf("reply from %s: seq=%d time<10ms\n", argv[1], i + 1);
#endif
            } else {
                printf("reply from %s: seq=%d time=%ums\n", argv[1], i + 1, rtt);
            }
            received++;
        } else {
            printf("no reply from %s: seq=%d\n", argv[1], i + 1);
        }
        if (i + 1 < count) {
            usleep(200000);
        }
    }
    printf("%d sent, %d received\n", count, received);
    return received > 0 ? 0 : 1;
}
