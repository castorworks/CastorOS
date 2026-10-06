#ifndef _USERLAND_LIB_NET_H_
#define _USERLAND_LIB_NET_H_

#include <types.h>

// 网络服务：协议和客户端接口。
//
// 服务进程以 "net" 登记（user/net：virtio-net 驱动加一个很小的协议栈，
// 支持 ARP、IPv4、ICMP 回显、UDP 和 TCP，启动时用 DHCP 获取地址）。IP 地址在这套接口里都是主机字节序的
// 32 位整数，a.b.c.d 写成 NET_IP(a, b, c, d)。UDP 和 TCP 的数据经共享缓冲区传递。

#define NET_SERVICE_NAME    "net"

#define NET_IP(a, b, c, d)  (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

/** 共享缓冲区大小 */
#define NET_BUF_SIZE        4096
/** 单个 UDP 数据报的最大载荷（不分片） */
#define NET_UDP_MAX         1472

// 请求的 label。应答的 data[0] 是结果（负数表示失败，按 int64_t 解释）
enum {
    NET_INFO      = 1,  // 应答 data[1]: MAC（低 6 字节；第 48 位: 地址来自 DHCP）, data[2]: IP（0 = 还在配置）,
                        //   data[3]: 掩码, data[4]: 网关, data[5]: DNS 服务器
    NET_PING      = 2,  // data[0]: 目标 IP, data[1]: 超时（毫秒）。阻塞到收到回显应答或超时；
                        //   应答 data[1]: 往返时间（毫秒）
    NET_UDP_OPEN  = 3,  // data[0]: 本地端口（0 = 由服务挑一个）。应答 data[0]: 套接字号, data[1]: 端口
    NET_UDP_CLOSE = 4,  // data[0]: 套接字号
    NET_UDP_SEND  = 5,  // data[0]: 套接字号, data[1]: 目标 IP, data[2]: 目标端口, data[3]: 长度；缓冲区: 数据
    NET_UDP_RECV  = 6,  // data[0]: 套接字号, data[1]: 超时（毫秒，0 = 不等待）。阻塞到有数据报或超时；
                        //   应答 data[0]: 长度, data[1]: 源 IP, data[2]: 源端口；数据在缓冲区

    NET_TCP_CONNECT = 10,   // data[0]: 目标 IP, data[1]: 目标端口, data[2]: 超时（毫秒）。
                            //   阻塞到连接建立、被拒绝或超时。应答 data[0]: 连接号
    NET_TCP_SEND    = 11,   // data[0]: 连接号, data[1]: 长度 (<= NET_BUF_SIZE)；缓冲区: 数据。
                            //   发送缓冲区满时阻塞。应答 data[0]: 接受了多少字节（可能少于请求的）
    NET_TCP_RECV    = 12,   // data[0]: 连接号, data[1]: 最多要多少字节, data[2]: 超时（毫秒，0 = 不等待）。
                            //   应答 data[0]: 字节数（0 = 对方已关闭，没有更多数据），数据在缓冲区
    NET_TCP_CLOSE   = 13,   // data[0]: 连接号（或监听号）。已经交给 send 的数据仍会发完
    NET_TCP_LISTEN  = 14,   // data[0]: 本地端口。应答 data[0]: 监听号
    NET_TCP_ACCEPT  = 15,   // data[0]: 监听号, data[1]: 超时（毫秒，0 = 不等待）。阻塞到有连接进来；
                            //   应答 data[0]: 连接号, data[1]: 对方 IP, data[2]: 对方端口

    NET_DEBUG_DROP  = 20,   // 调试：data[0] / data[1]: 丢掉接下来发出 / 收到的这么多个 TCP 帧
                            //   （发出的只算带 SYN 或数据的帧，纯确认不算）。
                            //   应答 data[1]: TCP 至今重传的次数
    NET_DEBUG_FRAGMENT = 21, // 调试：data[0]: 往外发的 IP 包每片最多放这么多字节上层数据（0 = 恢复正常），
                            //   让本来一个帧放得下的包也被分片
    NET_DEBUG_EXIT  = 23,   // 调试：服务应答之后立刻退出（像崩溃了一样），用来验证 init 会重启它
    NET_DEBUG_RENEW = 22,   // 调试：现在就向 DHCP 服务器续租。应答 data[0]: 0 / -1（地址不是 DHCP 给的），
                            //   data[1]: 至今续租成功的次数（不含这一次）
};

struct net_info {
    uint8_t mac[6];
    uint32_t ip;            // 0 表示地址还没配置好（DHCP 进行中）
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    bool dhcp;              // 地址来自 DHCP（否则是退回的固定地址）
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

/**
 * 建立一条到 ip:port 的 TCP 连接，最多等 timeout_ms 毫秒。
 * @return 连接号，被拒绝、超时或失败返回 -1
 */
int net_tcp_connect(uint32_t ip, uint16_t port, uint32_t timeout_ms);

/** 发送 len 字节（发送缓冲区满时阻塞）。@return len，连接已断返回 -1 */
long net_tcp_send(int conn, const void *data, size_t len);

/**
 * 接收最多 len 字节，没有数据时最多等 timeout_ms 毫秒（0 = 不等待）。
 * @return 收到的字节数；0 表示对方已经关闭、数据也读完了；超时或连接出错返回 -1
 */
long net_tcp_recv(int conn, void *buf, size_t len, uint32_t timeout_ms);

/** 关闭连接（或监听）。已经发送的数据仍会送达 */
int net_tcp_close(int conn);

/** 在本地端口 port 上监听。@return 监听号，端口已被占用或失败返回 -1 */
int net_tcp_listen(uint16_t port);

/**
 * 接受一条进来的连接，最多等 timeout_ms 毫秒（0 = 不等待）。
 * @return 连接号（之后用 send / recv / close），超时或失败返回 -1
 */
int net_tcp_accept(int listener, uint32_t timeout_ms, uint32_t *peer_ip, uint16_t *peer_port);

/**
 * 调试用：让网络服务丢掉接下来发出的 drop_tx 个、收到的 drop_rx 个 TCP 帧，用来验证重传。
 * @return TCP 至今重传的次数，没有网络服务返回 -1
 */
long net_debug_drop(uint32_t drop_tx, uint32_t drop_rx);

/**
 * 调试用：让网络服务把往外发的 IP 包切成每片最多 max_payload 字节上层数据（0 = 恢复正常），
 * 用来验证分片和重组。@return 0，没有网络服务返回 -1
 */
int net_debug_fragment(uint32_t max_payload);

/**
 * 调试用：让网络服务现在就向 DHCP 服务器续租（已经在续的时候不重复发）。
 * @return 至今续租成功的次数；地址不是 DHCP 给的、或者没有网络服务返回 -1
 */
long net_debug_renew(void);

/**
 * 调试用：让网络服务退出，像它崩溃了一样（init 会重启它，见 user/init）。
 * @return 0，没有网络服务返回 -1
 */
int net_debug_exit(void);

/**
 * 把主机名解析成 IPv4 地址（先当作 "a.b.c.d" 试，不是的话向配置的 DNS 服务器查询）。
 * @return 0 成功，-1 失败
 */
int net_resolve(const char *name, uint32_t *ip);

/** "a.b.c.d" 转成 IP。@return 0 成功，-1 格式不对 */
int net_parse_ip(const char *text, uint32_t *ip);

/** 把 IP 写成 "a.b.c.d"，buf 至少 16 字节 */
void net_format_ip(uint32_t ip, char *buf);

#endif // _USERLAND_LIB_NET_H_
