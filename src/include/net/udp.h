/**
 * @file udp.h
 * @brief UDP（用户数据报协议）
 * 
 * 实现 RFC 768 定义的 UDP 协议，提供无连接的数据报服务。
 * 
 * UDP 头部格式:
 * +-------------------------------+-------------------------------+
 * |       Source Port             |       Destination Port        |
 * +-------------------------------+-------------------------------+
 * |           Length              |          Checksum             |
 * +-------------------------------+-------------------------------+
 */

#ifndef _NET_UDP_H_
#define _NET_UDP_H_

#include <types.h>
#include <net/netbuf.h>
#include <net/netdev.h>

#define UDP_HEADER_LEN  8   ///< UDP 头部长度

/**
 * @brief UDP 头部
 */
typedef struct udp_header {
    uint16_t src_port;          ///< 源端口（网络字节序）
    uint16_t dst_port;          ///< 目的端口（网络字节序）
    uint16_t length;            ///< UDP 长度（头部 + 数据）（网络字节序）
    uint16_t checksum;          ///< 校验和（网络字节序）
} __attribute__((packed)) udp_header_t;

/**
 * @brief UDP 伪首部（用于计算校验和）
 */
typedef struct udp_pseudo_header {
    uint32_t src_addr;          ///< 源 IP 地址
    uint32_t dst_addr;          ///< 目的 IP 地址
    uint8_t  zero;              ///< 保留（0）
    uint8_t  protocol;          ///< 协议号 (17)
    uint16_t udp_length;        ///< UDP 长度
} __attribute__((packed)) udp_pseudo_header_t;

/**
 * @brief UDP 控制块（PCB）- 表示一个 UDP 端点
 */
typedef struct udp_pcb {
    uint32_t local_ip;          ///< 本地 IP（0 表示任意）
    uint16_t local_port;        ///< 本地端口
    uint32_t remote_ip;         ///< 远程 IP（0 表示任意）
    uint16_t remote_port;       ///< 远程端口（0 表示任意）

    // 接收缓冲区
    net::Netbuf *recv_queue;       ///< 接收队列
    uint32_t recv_queue_len;    ///< 队列中的数据包数

    // 回调函数
    void (*recv_callback)(struct udp_pcb *pcb, net::Netbuf *buf,
                         uint32_t src_ip, uint16_t src_port);
    void *callback_arg;         ///< 回调参数

    struct udp_pcb *next;       ///< 链表指针
} udp_pcb_t;

namespace net {

/**
 * @brief UDP 协议
 */
class Udp {
public:
    /**
     * @brief 初始化 UDP 协议
     */
    static void init();

    /**
     * @brief 处理接收到的 UDP 数据报
     * @param dev 网络设备
     * @param buf 接收缓冲区
     * @param src_ip 源 IP 地址（网络字节序）
     * @param dst_ip 目的 IP 地址（网络字节序）
     */
    static void input(net::Netdev *dev, net::Netbuf *buf, uint32_t src_ip, uint32_t dst_ip);

    /**
     * @brief 发送 UDP 数据报
     * @param src_port 源端口（主机字节序）
     * @param dst_ip 目的 IP 地址（网络字节序）
     * @param dst_port 目的端口（主机字节序）
     * @param data 数据
     * @param len 数据长度
     * @return 0 成功，-1 失败
     */
    static int output(uint16_t src_port, uint32_t dst_ip, uint16_t dst_port,
                   uint8_t *data, uint32_t len);

    /**
     * @brief 创建新的 UDP PCB
     * @return UDP PCB，失败返回 NULL
     */
    static udp_pcb_t *pcb_new();

    /**
     * @brief 释放 UDP PCB
     * @param pcb UDP PCB
     */
    static void pcb_free(udp_pcb_t *pcb);

    /**
     * @brief 绑定本地地址和端口
     * @param pcb UDP PCB
     * @param local_ip 本地 IP（0 表示任意）
     * @param local_port 本地端口（主机字节序）
     * @return 0 成功，-1 失败
     */
    static int bind(udp_pcb_t *pcb, uint32_t local_ip, uint16_t local_port);

    /**
     * @brief 连接到远程地址
     * @param pcb UDP PCB
     * @param remote_ip 远程 IP（网络字节序）
     * @param remote_port 远程端口（主机字节序）
     * @return 0 成功，-1 失败
     */
    static int connect(udp_pcb_t *pcb, uint32_t remote_ip, uint16_t remote_port);

    /**
     * @brief 断开连接（清除远程地址）
     * @param pcb UDP PCB
     */
    static void disconnect(udp_pcb_t *pcb);

    /**
     * @brief 通过 PCB 发送数据
     * @param pcb UDP PCB
     * @param buf 数据缓冲区
     * @return 0 成功，-1 失败
     */
    static int send(udp_pcb_t *pcb, net::Netbuf *buf);

    /**
     * @brief 通过 PCB 发送数据到指定地址
     * @param pcb UDP PCB
     * @param buf 数据缓冲区
     * @param dst_ip 目的 IP（网络字节序）
     * @param dst_port 目的端口（主机字节序）
     * @param dev 发送设备；NULL 表示使用默认设备
     * @return 0 成功，-1 失败（两种情况下 buf 都仍归调用者，由调用者释放）
     */
    static int sendto(udp_pcb_t *pcb, net::Netbuf *buf, uint32_t dst_ip, uint16_t dst_port,
                      net::Netdev *dev = NULL);

    /**
     * @brief 设置接收回调函数
     * @param pcb UDP PCB
     * @param callback 回调函数
     * @param arg 回调参数
     */
    static void recv(udp_pcb_t *pcb,
                  void (*callback)(udp_pcb_t *pcb, net::Netbuf *buf,
                                  uint32_t src_ip, uint16_t src_port),
                  void *arg);

    /**
     * @brief 从接收队列轮询获取数据包（非阻塞）
     * @param pcb UDP PCB
     * @return 数据包缓冲区，NULL 表示队列为空
     * 
     * 注意：调用者负责释放返回的 netbuf
     */
    static net::Netbuf *recv_poll(udp_pcb_t *pcb);

    /**
     * @brief 检查是否有待接收的数据
     * @param pcb UDP PCB
     * @return true 有数据，false 无数据
     */
    static bool has_data(udp_pcb_t *pcb);

    /**
     * @brief 计算 UDP 校验和
     * @param src_ip 源 IP 地址（网络字节序）
     * @param dst_ip 目的 IP 地址（网络字节序）
     * @param udp UDP 头部指针
     * @param len UDP 总长度（头部 + 数据）
     * @return 校验和
     */
    static uint16_t checksum(uint32_t src_ip, uint32_t dst_ip, udp_header_t *udp, uint16_t len);

    /**
     * @brief 分配一个临时端口
     * @return 端口号（主机字节序），0 表示失败
     */
    static uint16_t alloc_port();

    /**
     * @brief 打印/输出所有 UDP 端点状态（用于 netstat 命令）
     * @param buf 输出缓冲区，NULL 则直接打印到控制台
     * @param size 缓冲区大小（buf 非 NULL 时有效）
     * @return 写入/打印的字节数
     */
    static int pcb_list_dump(char *buf, size_t size);
};

} // namespace net

#endif // _NET_UDP_H_
