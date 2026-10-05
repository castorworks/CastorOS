// ifconfig - 显示网络配置

#include <stdio.h>
#include <net.h>

int main() {
    struct net_info info;
    if (net_info(&info) != 0) {
        printf("ifconfig: no network\n");
        return 1;
    }
    char ip[16], mask[16], gw[16];
    net_format_ip(info.ip, ip);
    net_format_ip(info.netmask, mask);
    net_format_ip(info.gateway, gw);
    printf("mac     %02x:%02x:%02x:%02x:%02x:%02x\n", info.mac[0], info.mac[1], info.mac[2],
           info.mac[3], info.mac[4], info.mac[5]);
    printf("ip      %s\n", ip);
    printf("netmask %s\n", mask);
    printf("gateway %s\n", gw);
    return 0;
}
