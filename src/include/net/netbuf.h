/**
 * @file netbuf.h
 * @brief 网络缓冲区管理（类似 Linux 的 sk_buff）
 * 
 * 网络缓冲区是网络栈的基础数据结构，用于高效管理数据包内存。
 * 
 * 数据包结构:
 * +------------------+
 * |   headroom       |  <- 用于添加协议头
 * +------------------+
 * |   data           |  <- 实际数据
 * +------------------+
 * |   tailroom       |  <- 预留空间
 * +------------------+
 */

#ifndef _NET_NETBUF_H_
#define _NET_NETBUF_H_

#include <types.h>

#define NETBUF_MAX_SIZE     2048    // 最大缓冲区大小
#define NETBUF_HEADROOM     128     // 预留头部空间（用于协议头）

// 前向声明
namespace net { struct Netdev; }

namespace net {

/**
 * @brief 网络缓冲区结构
 */
struct Netbuf {
    uint8_t *head;          ///< 缓冲区起始地址
    uint8_t *data;          ///< 数据起始地址
    uint8_t *tail;          ///< 数据结束地址
    uint8_t *end;           ///< 缓冲区结束地址
    
    uint32_t len;           ///< 数据长度
    uint32_t total_size;    ///< 缓冲区总大小
    
    // 协议相关指针（用于快速访问各层头部）
    void *mac_header;       ///< 链路层头部
    void *network_header;   ///< 网络层头部
    void *transport_header; ///< 传输层头部
    
    // 接收信息
    net::Netdev *dev;     ///< 接收数据包的网络设备
    
    // 源地址信息（用于 recvfrom）
    uint32_t src_ip;        ///< 源 IP 地址（网络字节序）
    uint16_t src_port;      ///< 源端口（主机字节序）
    
    Netbuf *next;    ///< 链表指针（用于队列）

    /**
     * @brief 分配网络缓冲区
     * @param size 数据区大小
     * @return 新分配的缓冲区，失败返回 NULL
     */
    static Netbuf *alloc(uint32_t size);

    /**
     * @brief 释放网络缓冲区
     * @param buf 缓冲区
     */
    static void free(Netbuf *buf);

    /**
     * @brief 在数据前添加空间（用于添加协议头）
     * @param buf 缓冲区
     * @param len 要添加的长度
     * @return 新的 data 指针，失败返回 NULL
     */
    static uint8_t *push(Netbuf *buf, uint32_t len);

    /**
     * @brief 从数据前移除空间（用于剥离协议头）
     * @param buf 缓冲区
     * @param len 要移除的长度
     * @return 新的 data 指针，失败返回 NULL
     */
    static uint8_t *pull(Netbuf *buf, uint32_t len);

    /**
     * @brief 在数据后添加空间
     * @param buf 缓冲区
     * @param len 要添加的长度
     * @return 旧的 tail 指针，失败返回 NULL
     */
    static uint8_t *put(Netbuf *buf, uint32_t len);

    /**
     * @brief 把数据截短到 len 字节（丢弃尾部，例如以太网填充）
     * @param buf 缓冲区
     * @param len 保留的长度；不小于当前长度时不做任何事
     */
    static void trim(Netbuf *buf, uint32_t len);

    /**
     * @brief 复制缓冲区
     * @param buf 源缓冲区
     * @return 新缓冲区的副本，失败返回 NULL
     */
    static Netbuf *clone(Netbuf *buf);

    /**
     * @brief 重置缓冲区为初始状态
     * @param buf 缓冲区
     */
    static void reset(Netbuf *buf);

    /**
     * @brief 获取缓冲区剩余的头部空间
     * @param buf 缓冲区
     * @return 头部剩余空间字节数
     */
    static uint32_t headroom(Netbuf *buf);

    /**
     * @brief 获取缓冲区剩余的尾部空间
     * @param buf 缓冲区
     * @return 尾部剩余空间字节数
     */
    static uint32_t tailroom(Netbuf *buf);
};

} // namespace net

#endif // _NET_NETBUF_H_
