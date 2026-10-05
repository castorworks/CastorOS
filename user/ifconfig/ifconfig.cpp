// ifconfig - 显示网络配置

#include <stdio.h>
#include <net.h>

int main() {
    struct net_info info;
    if (net_info(&info) != 0) {
        eprintf("ifconfig: no network\n");
        return 1;
    }
    if (info.ip == 0) {
        printf("mac     %02x:%02x:%02x:%02x:%02x:%02x\n", info.mac[0], info.mac[1], info.mac[2],
               info.mac[3], info.mac[4], info.mac[5]);
        printf("ip      (waiting for DHCP)\n");
        return 0;
    }
    char ip[16], mask[16], gw[16], dns[16];
    net_format_ip(info.ip, ip);
    net_format_ip(info.netmask, mask);
    net_format_ip(info.gateway, gw);
    net_format_ip(info.dns, dns);
    printf("mac     %02x:%02x:%02x:%02x:%02x:%02x\n", info.mac[0], info.mac[1], info.mac[2],
           info.mac[3], info.mac[4], info.mac[5]);
    printf("ip      %s (%s)\n", ip, info.dhcp ? "dhcp" : "static");
    printf("netmask %s\n", mask);
    printf("gateway %s\n", gw);
    printf("dns     %s\n", dns);
    return 0;
}
