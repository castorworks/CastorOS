#ifndef _NET_INTERNAL_H_
#define _NET_INTERNAL_H_

// 网络服务内部：协议栈（net.cpp）和 TCP（tcp.cpp）之间共用的东西

#include <syscall.h>
#include <net.h>

#define IP_PROTO_TCP    6

/** 一个 IP 包里最多能放多少上层数据（以太网帧 1514 - 以太网头 14 - IP 头 20） */
#define IP_PAYLOAD_MAX  1480

// 网络字节序是大端，三个架构都是小端
static inline uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static inline uint32_t swap32(uint32_t v) {
    return (v << 24) | ((v << 8) & 0x00FF0000) | ((v >> 8) & 0x0000FF00) | (v >> 24);
}

// ---- net.cpp 提供 ----

/** 本机地址（主机字节序）；0 表示还没配置好 */
extern uint32_t my_ip;

/** 互联网校验和：16 位反码求和。start 用来接着前一段（伪首部）的和算 */
uint16_t checksum(const void *data, size_t len, uint32_t start);

/** TCP/UDP 伪首部（源、目的地址，协议，长度）的校验和部分 */
uint32_t pseudo_sum(uint32_t src, uint32_t dst, uint8_t protocol, size_t len);

/** 发一个 IP 包。payload 在返回之前就被拷走了 */
void ip_send(uint32_t dst, uint8_t protocol, const uint8_t *payload, size_t len);

/** 应答一个阻塞在请求里的客户：data[0] = result, data[1] = d1, data[2] = d2 */
void reply_client(int pid, uint32_t label, int64_t result, uint64_t d1, uint64_t d2);

// ---- tcp.cpp 提供 ----

/** 收到一个 TCP 段。注意：发往本机的段会递归进来，处理中不能在发送之后再读 data */
void tcp_input(uint32_t src, uint32_t dst, const uint8_t *data, size_t len);

/** 处理一个 NET_TCP_* 请求（自己负责应答，可能是之后才应答） */
void tcp_request(const struct ipc_msg *m);

/** 定时器：重传、各种超时。@return 是否还有连接需要定时器 */
bool tcp_tick(uint64_t now);

/** 客户退出了：丢掉它的连接 */
void tcp_drop_owner(int pid);

#endif // _NET_INTERNAL_H_
