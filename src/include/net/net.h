/**
 * @file net.h
 * @brief 网络栈总头文件
 * 
 * 包含所有网络模块的头文件，提供网络栈初始化函数。
 */

#ifndef _NET_NET_H_
#define _NET_NET_H_

#include <net/netbuf.h>
#include <net/checksum.h>
#include <net/netdev.h>
#include <net/ethernet.h>
#include <net/arp.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/tcp.h>
#include <net/socket.h>
#include <net/dhcp.h>
#include <net/dns.h>

namespace net {

/**
 * @brief 网络协议栈入口（初始化、配置与诊断）
 */
class Stack {
public:
    /**
     * @brief 初始化整个网络栈
     * 
     * 按顺序初始化各层：
     * 1. 网络设备层
     * 2. 以太网层
     * 3. ARP 协议
     * 4. IP 协议
     * 5. ICMP 协议
     * 6. UDP 协议
     * 7. TCP 协议
     * 8. Socket 子系统
     */
    static void init();

    /**
     * @brief 配置默认网络设备
     * @param ip IP 地址字符串（如 "10.0.2.15"）
     * @param netmask 子网掩码字符串（如 "255.255.255.0"）
     * @param gateway 网关地址字符串（如 "10.0.2.2"）
     * @return 0 成功，-1 失败
     */
    static int configure(const char *ip, const char *netmask, const char *gateway);

    /**
     * @brief Ping 测试
     * @param ip_str 目标 IP 地址字符串
     * @param count ping 次数
     * @return 0 成功，-1 失败
     */
    static int ping(const char *ip_str, int count);
};

} // namespace net

#endif // _NET_NET_H_
