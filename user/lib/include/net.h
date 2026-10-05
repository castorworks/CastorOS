#ifndef _USERLAND_LIB_NET_H_
#define _USERLAND_LIB_NET_H_

#include <types.h>

// 网络服务：协议和客户端接口。
//
// 服务进程以 "net" 登记（user/net：virtio-net 驱动加一个很小的协议栈，
// 支持 ARP、IPv4、ICMP 回显和 UDP）。IP 地址在这套接口里都是主机字节序的
// 32 位整数，a.b.c.d 写成 NET_IP(a, b, c, d)。UDP 的数据经共享缓冲区传递。

#define NET_SERVICE_NAME    "net"

#define NET_IP(a, b, c, d)  (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

/** 共享缓冲区大小 */
#define NET_BUF_SIZE        4096
/** 单个 UDP 数据报的最大载荷（不分片） */
#define NET_UDP_MAX         1472

// 请求的 label。应答的 data[0] 是结果（负数表示失败，按 int64_t 解释）
enum {
    NET_INFO      = 1,  // 应答 data[1]: MAC（低 6 字节）, data[2]: IP, data[3]: 掩码, data[4]: 网关
    NET_PING      = 2,  // data[0]: 目标 IP, data[1]: 超时（毫秒）。阻塞到收到回显应答或超时；
                        //   应答 data[1]: 往返时间（毫秒）
    NET_UDP_OPEN  = 3,  // data[0]: 本地端口（0 = 由服务挑一个）。应答 data[0]: 套接字号, data[1]: 端口
    NET_UDP_CLOSE = 4,  // data[0]: 套接字号
    NET_UDP_SEND  = 5,  // data[0]: 套接字号, data[1]: 目标 IP, data[2]: 目标端口, data[3]: 长度；缓冲区: 数据
    NET_UDP_RECV  = 6,  // data[0]: 套接字号, data[1]: 超时（毫秒，0 = 不等待）。阻塞到有数据报或超时；
                        //   应答 data[0]: 长度, data[1]: 源 IP, data[2]: 源端口；数据在缓冲区
};

struct net_info {
    uint8_t mac[6];
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
};

/** 本机的网络配置。@return 0 成功，-1 没有网络服务 */
int net_info(struct net_info *info);

/** 发一个 ICMP 回显请求并等应答。@return 0 收到应答（*rtt_ms 是往返时间），-1 超时或不可达 */
int net_ping(uint32_t ip, uint32_t timeout_ms, uint32_t *rtt_ms);

/** 打开一个 UDP 套接字，绑定到本地端口 port（0 = 自动挑选）。@return 套接字号，失败 -1 */
int net_udp_open(uint16_t port);
int net_udp_close(int sock);

/** 发一个数据报。@return 0 成功，-1 失败 */
int net_udp_send(int sock, uint32_t ip, uint16_t port, const void *data, size_t len);

/**
 * 收一个数据报，最多等 timeout_ms 毫秒（0 = 不等待）。超过 len 的部分被丢弃。
 * @return 数据报的长度，超时或失败返回 -1
 */
long net_udp_recv(int sock, void *buf, size_t len, uint32_t timeout_ms,
                  uint32_t *src_ip, uint16_t *src_port);

/** "a.b.c.d" 转成 IP。@return 0 成功，-1 格式不对 */
int net_parse_ip(const char *text, uint32_t *ip);

/** 把 IP 写成 "a.b.c.d"，buf 至少 16 字节 */
void net_format_ip(uint32_t ip, char *buf);

#endif // _USERLAND_LIB_NET_H_
