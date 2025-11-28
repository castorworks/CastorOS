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
struct netdev;

/**
 * @brief 网络缓冲区结构
 */
typedef struct netbuf {
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
    struct netdev *dev;     ///< 接收数据包的网络设备
    
    struct netbuf *next;    ///< 链表指针（用于队列）
} netbuf_t;

/**
 * @brief 分配网络缓冲区
 * @param size 数据区大小
 * @return 新分配的缓冲区，失败返回 NULL
 */
netbuf_t *netbuf_alloc(uint32_t size);

/**
 * @brief 释放网络缓冲区
 * @param buf 缓冲区
 */
void netbuf_free(netbuf_t *buf);

/**
 * @brief 在数据前添加空间（用于添加协议头）
 * @param buf 缓冲区
 * @param len 要添加的长度
 * @return 新的 data 指针，失败返回 NULL
 */
uint8_t *netbuf_push(netbuf_t *buf, uint32_t len);

/**
 * @brief 从数据前移除空间（用于剥离协议头）
 * @param buf 缓冲区
 * @param len 要移除的长度
 * @return 新的 data 指针，失败返回 NULL
 */
uint8_t *netbuf_pull(netbuf_t *buf, uint32_t len);

/**
 * @brief 在数据后添加空间
 * @param buf 缓冲区
 * @param len 要添加的长度
 * @return 旧的 tail 指针，失败返回 NULL
 */
uint8_t *netbuf_put(netbuf_t *buf, uint32_t len);

/**
 * @brief 复制缓冲区
 * @param buf 源缓冲区
 * @return 新缓冲区的副本，失败返回 NULL
 */
netbuf_t *netbuf_clone(netbuf_t *buf);

/**
 * @brief 重置缓冲区为初始状态
 * @param buf 缓冲区
 */
void netbuf_reset(netbuf_t *buf);

/**
 * @brief 获取缓冲区剩余的头部空间
 * @param buf 缓冲区
 * @return 头部剩余空间字节数
 */
uint32_t netbuf_headroom(netbuf_t *buf);

/**
 * @brief 获取缓冲区剩余的尾部空间
 * @param buf 缓冲区
 * @return 尾部剩余空间字节数
 */
uint32_t netbuf_tailroom(netbuf_t *buf);

#endif // _NET_NETBUF_H_

