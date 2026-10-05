// dns - 查询一个主机名的 IPv4 地址
//
//   dns <name>
//
// 用 UDP 向 DHCP 给的 DNS 服务器查询。在 QEMU 用户网络里那是 10.0.2.3，一个把查询交给
// 宿主机解析器的转发器，所以能不能查到取决于宿主机能不能上网。

#include <stdio.h>
#include <net.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: dns <name>\n");
        return 1;
    }
    struct net_info info;
    if (net_info(&info) != 0 || info.ip == 0) {
        printf("dns: no network\n");
        return 1;
    }
    uint32_t ip;
    if (net_resolve(argv[1], &ip) != 0) {
        printf("dns: %s: not found\n", argv[1]);
        return 1;
    }
    char text[16];
    net_format_ip(ip, text);
    printf("%s has address %s\n", argv[1], text);
    return 0;
}
