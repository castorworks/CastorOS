/**
 * @file icmp.h
 * @brief ICMP（Internet 控制消息协议）
 * 
 * 实现 RFC 792 定义的 ICMP 协议，用于网络诊断和错误报告。
 * 
 * ICMP 头部格式:
 * +-------+-------+-------------------------------+
 * | Type  | Code  |          Checksum             |
 * +-------+-------+-------------------------------+
 * |              Rest of Header                   |
 * +-----------------------------------------------+
 */

#ifndef _NET_ICMP_H_
#define _NET_ICMP_H_

#include <types.h>
#include <net/netbuf.h>
#include <net/netdev.h>

// ICMP 类型
#define ICMP_ECHO_REPLY         0   ///< Echo Reply（ping 响应）
#define ICMP_DEST_UNREACHABLE   3   ///< Destination Unreachable
#define ICMP_SOURCE_QUENCH      4   ///< Source Quench
#define ICMP_REDIRECT           5   ///< Redirect
#define ICMP_ECHO_REQUEST       8   ///< Echo Request（ping 请求）
#define ICMP_TIME_EXCEEDED      11  ///< Time Exceeded
#define ICMP_PARAM_PROBLEM      12  ///< Parameter Problem

// ICMP 目的不可达代码
#define ICMP_NET_UNREACHABLE    0   ///< 网络不可达
#define ICMP_HOST_UNREACHABLE   1   ///< 主机不可达
#define ICMP_PROTO_UNREACHABLE  2   ///< 协议不可达
#define ICMP_PORT_UNREACHABLE   3   ///< 端口不可达
#define ICMP_FRAG_NEEDED        4   ///< 需要分片但设置了 DF
#define ICMP_SOURCE_ROUTE_FAILED 5  ///< 源路由失败

// ICMP 时间超时代码
#define ICMP_TTL_EXCEEDED       0   ///< TTL 超时
#define ICMP_FRAG_TIMEOUT       1   ///< 分片重组超时

/**
 * @brief ICMP 头部
 */
typedef struct icmp_header {
    uint8_t  type;              ///< 类型
    uint8_t  code;              ///< 代码
    uint16_t checksum;          ///< 校验和
    union {
        struct {
            uint16_t id;        ///< 标识符
            uint16_t sequence;  ///< 序列号
        } echo;
        uint32_t gateway;       ///< 重定向网关地址
        struct {
            uint16_t __unused;
            uint16_t mtu;       ///< 下一跳 MTU
        } frag;
        uint32_t unused;        ///< 未使用
    } un;
} __attribute__((packed)) icmp_header_t;

/**
 * @brief ICMP Echo 数据（用于 ping）
 */
typedef struct icmp_echo {
    icmp_header_t header;
    uint8_t       data[];       ///< 可变长度数据
} __attribute__((packed)) icmp_echo_t;

// Ping 回调函数类型
typedef void (*ping_callback_t)(uint32_t src_ip, uint16_t seq, uint32_t rtt_ms, bool success);

/**
 * @brief 初始化 ICMP 协议
 */
void icmp_init(void);

/**
 * @brief 处理接收到的 ICMP 报文
 * @param dev 网络设备
 * @param buf 接收缓冲区
 * @param src_ip 源 IP 地址（网络字节序）
 */
void icmp_input(netdev_t *dev, netbuf_t *buf, uint32_t src_ip);

/**
 * @brief 发送 ICMP Echo 请求（ping）
 * @param dst_ip 目的 IP 地址（网络字节序）
 * @param id 标识符
 * @param seq 序列号
 * @param data 数据
 * @param len 数据长度
 * @return 0 成功，-1 失败
 */
int icmp_send_echo_request(uint32_t dst_ip, uint16_t id, uint16_t seq,
                           uint8_t *data, uint32_t len);

/**
 * @brief 发送 ICMP Echo 应答
 * @param dst_ip 目的 IP 地址（网络字节序）
 * @param id 标识符
 * @param seq 序列号
 * @param data 数据
 * @param len 数据长度
 * @return 0 成功，-1 失败
 */
int icmp_send_echo_reply(uint32_t dst_ip, uint16_t id, uint16_t seq,
                         uint8_t *data, uint32_t len);

/**
 * @brief 发送 ICMP 目的不可达消息
 * @param dst_ip 目的 IP 地址（网络字节序）
 * @param code 代码
 * @param orig_header 原始 IP 头部
 * @param orig_data 原始数据（至少 8 字节）
 * @return 0 成功，-1 失败
 */
int icmp_send_dest_unreachable(uint32_t dst_ip, uint8_t code,
                               void *orig_header, void *orig_data);

/**
 * @brief 注册 ping 回调函数
 * @param callback 回调函数
 */
void icmp_register_ping_callback(ping_callback_t callback);

/**
 * @brief 获取最后一次 ping 的 RTT
 * @return RTT（毫秒），-1 表示无数据
 */
int32_t icmp_get_last_rtt(void);

#endif // _NET_ICMP_H_

