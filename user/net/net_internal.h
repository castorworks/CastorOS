#ifndef _NET_INTERNAL_H_
#define _NET_INTERNAL_H_

// 网络服务内部：各个文件（net / nic / ip / udp / tcp / dhcp）之间的接口

#include <syscall.h>
#include <net.h>

#define IP_PROTO_TCP    6

/** 一个不分片的 IP 包里最多能放多少上层数据（以太网帧 1514 - 以太网头 14 - IP 头 20） */
#define IP_PAYLOAD_MAX  1480

/** 一个 IP 包（分片之前 / 重组之后）最多能有多少上层数据 */
#define IP_DATAGRAM_MAX 8192

// 网络字节序是大端，三个架构都是小端
static inline uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static inline uint32_t swap32(uint32_t v) {
    return (v << 24) | ((v << 8) & 0x00FF0000) | ((v >> 8) & 0x0000FF00) | (v >> 24);
}

#define IP_PROTO_UDP    17
#define IP_BROADCAST    0xFFFFFFFFu
#define DHCP_CLIENT_PORT 68

/** 以太网帧的最大长度（不含 FCS） */
#define FRAME_MAX       1514

// ---- net.cpp：地址配置、应答客户 ----

/** 本机地址（主机字节序）；DHCP 完成（或放弃）之前是 0 */
extern uint32_t my_ip;
extern uint32_t netmask, gateway, dns_server;
/** 地址是不是 DHCP 给的（否则是退回的固定配置） */
extern bool from_dhcp;
/** 网卡的 MAC 地址，驱动初始化时填 */
extern uint8_t my_mac[6];

/** 应答一个阻塞在请求里的客户：data[0] = result, data[1] = d1, data[2] = d2 */
void reply_client(int pid, uint32_t label, int64_t result, uint64_t d1, uint64_t d2);

// ---- nic.cpp：网卡（哪种设备见 nic.h） ----

/** 打开许可给本进程的网卡，读出 MAC，建好收发队列。@return 没有可用的网卡返回 false */
bool nic_init(void);
/** 网卡的种类和中断号（只用来打印） */
const char *nic_kind(void);
int nic_irq_line(void);
/** 收到了网卡的中断消息：撤销中断，把收到的帧交给 eth_input，重新打开中断线 */
void nic_interrupt(void);
/** 发一个以太网帧。发送缓冲区用完时丢弃 */
void nic_send(const uint8_t *frame, size_t len);
/** 调试用：丢掉接下来发出的 tx 个、收到的 rx 个 TCP 帧（NET_DEBUG_DROP），用来验证重传 */
void nic_debug_drop(uint32_t tx, uint32_t rx);

// ---- ip.cpp：以太网、ARP、IPv4、ICMP ----

/** 网卡收到一个以太网帧 */
void eth_input(uint8_t *frame, size_t len);

/** 互联网校验和：16 位反码求和。start 用来接着前一段（伪首部）的和算 */
uint16_t checksum(const void *data, size_t len, uint32_t start);

/** 发一个 IP 包，一个帧放不下就分片（最多 IP_DATAGRAM_MAX）。payload 在返回之前就被拷走了 */
void ip_send(uint32_t dst, uint8_t protocol, const uint8_t *payload, size_t len);

/** 调试用：往外发的包每片最多放 max_payload 字节上层数据（NET_DEBUG_FRAGMENT）；0 恢复正常 */
void ip_debug_fragment(uint32_t max_payload);

/** 把发给自己的包（回环队列里的）交给协议栈。主循环每处理完一件事调用一次 */
void loopback_drain(void);

/** 替客户 pid 发一个回显请求；应答在收到回显或者超时的时候发 */
void ping_start(int pid, uint32_t ip, uint32_t timeout_ms);

/** 定时器：重发 ARP 请求 / 让超时的 ping 失败。@return 是否还有东西在等 */
bool arp_tick(uint64_t now);
bool ping_tick(uint64_t now);

// ---- udp.cpp ----

/** TCP/UDP 伪首部（源、目的地址，协议，长度）的校验和部分 */
uint32_t pseudo_sum(uint32_t src, uint32_t dst, uint8_t protocol, size_t len);

/** 收到一个 UDP 数据报 */
void udp_input(uint32_t src, uint32_t dst, const uint8_t *data, size_t len);

/** 不经过套接字直接发一个数据报（DHCP 用：那时还没有地址） */
void udp_send_raw(uint16_t src_port, uint32_t dst, uint16_t dst_port, const uint8_t *data, size_t len);

/** 处理一个 NET_UDP_* 请求（自己负责应答，recv 可能是之后才应答） */
void udp_request(const struct ipc_msg *m);

/** 定时器：让等得太久的 recv 失败。@return 是否还有 recv 在等 */
bool udp_tick(uint64_t now);

/** 客户退出了：收回它的套接字 */
void udp_drop_owner(int pid);

// ---- dhcp.cpp ----

/** 开始获取地址 */
void dhcp_start(void);
/** DHCP 客户端端口上收到一个数据报 */
void dhcp_input(const uint8_t *data, size_t len);
/** 定时器：重发、放弃、到时间续租。@return 是否还在等应答 */
bool dhcp_tick(uint64_t now);
/** 下一次要续租（或者租约到期）的时刻；没有租约返回 0。主循环据此设定时器 */
uint64_t dhcp_next_deadline(void);
/** 调试用：现在就续租（NET_DEBUG_RENEW）。@return 至今续租成功过几次 */
uint32_t dhcp_debug_renew(void);

// ---- tcp.cpp 提供 ----

/** 收到一个 TCP 段 */
void tcp_input(uint32_t src, uint32_t dst, const uint8_t *data, size_t len);

/** 处理一个 NET_TCP_* 请求（自己负责应答，可能是之后才应答） */
void tcp_request(const struct ipc_msg *m);

/** 定时器：重传、各种超时。@return 是否还有连接需要定时器 */
bool tcp_tick(uint64_t now);

/** 重传过多少次（调试/测试用） */
extern uint32_t tcp_retransmits;

/** 客户退出了：丢掉它的连接 */
void tcp_drop_owner(int pid);

#endif // _NET_INTERNAL_H_
