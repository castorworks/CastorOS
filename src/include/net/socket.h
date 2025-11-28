/**
 * @file socket.h
 * @brief BSD Socket API
 * 
 * 提供标准的 BSD Socket 接口，用于网络编程。
 */

#ifndef _NET_SOCKET_H_
#define _NET_SOCKET_H_

#include <types.h>

// 地址族
#define AF_UNSPEC       0       ///< 未指定
#define AF_INET         2       ///< IPv4

// Socket 类型
#define SOCK_STREAM     1       ///< TCP
#define SOCK_DGRAM      2       ///< UDP
#define SOCK_RAW        3       ///< Raw socket

// 协议号
#define IPPROTO_IP      0       ///< 自动选择
#define IPPROTO_ICMP    1       ///< ICMP
#define IPPROTO_TCP     6       ///< TCP
#define IPPROTO_UDP     17      ///< UDP

// Socket 选项级别
#define SOL_SOCKET      1       ///< Socket 层

// Socket 选项
#define SO_REUSEADDR    2       ///< 重用地址
#define SO_KEEPALIVE    9       ///< 保活
#define SO_RCVTIMEO     20      ///< 接收超时
#define SO_SNDTIMEO     21      ///< 发送超时
#define SO_RCVBUF       8       ///< 接收缓冲区大小
#define SO_SNDBUF       7       ///< 发送缓冲区大小
#define SO_ERROR        4       ///< 获取错误状态

// shutdown() how 参数
#define SHUT_RD         0       ///< 关闭读
#define SHUT_WR         1       ///< 关闭写
#define SHUT_RDWR       2       ///< 关闭读写

// 消息标志
#define MSG_PEEK        0x02    ///< 查看数据但不移除
#define MSG_DONTWAIT    0x40    ///< 非阻塞操作
#define MSG_WAITALL     0x100   ///< 等待所有数据

// 最大连接数
#define SOMAXCONN       128

// 错误码
#define SOCKET_ERROR    (-1)

/**
 * @brief 通用 socket 地址结构
 */
struct sockaddr {
    uint16_t sa_family;         ///< 地址族
    char     sa_data[14];       ///< 地址数据
};

/**
 * @brief IPv4 socket 地址结构
 */
struct sockaddr_in {
    uint16_t sin_family;        ///< AF_INET
    uint16_t sin_port;          ///< 端口号（网络字节序）
    uint32_t sin_addr;          ///< IP 地址（网络字节序）
    uint8_t  sin_zero[8];       ///< 填充
};

typedef uint32_t socklen_t;

/**
 * @brief 特殊地址
 */
#define INADDR_ANY          0x00000000  ///< 任意地址
#define INADDR_BROADCAST    0xFFFFFFFF  ///< 广播地址
#define INADDR_LOOPBACK     0x7F000001  ///< 回环地址 (127.0.0.1)

/**
 * @brief 初始化 socket 子系统
 */
void socket_init(void);

/* ============================================================================
 * 内核 Socket API（供系统调用使用）
 * ============================================================================ */

/**
 * @brief 创建 socket
 * @param domain 地址族（AF_INET）
 * @param type socket 类型（SOCK_STREAM/SOCK_DGRAM）
 * @param protocol 协议（通常为 0）
 * @return socket 描述符，-1 失败
 */
int sys_socket(int domain, int type, int protocol);

/**
 * @brief 绑定地址
 * @param sockfd socket 描述符
 * @param addr 地址
 * @param addrlen 地址长度
 * @return 0 成功，-1 失败
 */
int sys_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

/**
 * @brief 监听连接
 * @param sockfd socket 描述符
 * @param backlog 等待队列长度
 * @return 0 成功，-1 失败
 */
int sys_listen(int sockfd, int backlog);

/**
 * @brief 接受连接
 * @param sockfd socket 描述符
 * @param addr 客户端地址（输出）
 * @param addrlen 地址长度（输入/输出）
 * @return 新 socket 描述符，-1 失败
 */
int sys_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief 发起连接
 * @param sockfd socket 描述符
 * @param addr 服务端地址
 * @param addrlen 地址长度
 * @return 0 成功，-1 失败
 */
int sys_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

/**
 * @brief 发送数据
 * @param sockfd socket 描述符
 * @param buf 数据缓冲区
 * @param len 数据长度
 * @param flags 标志
 * @return 发送的字节数，-1 失败
 */
ssize_t sys_send(int sockfd, const void *buf, size_t len, int flags);

/**
 * @brief 发送数据到指定地址
 */
ssize_t sys_sendto(int sockfd, const void *buf, size_t len, int flags,
                   const struct sockaddr *dest_addr, socklen_t addrlen);

/**
 * @brief 接收数据
 * @param sockfd socket 描述符
 * @param buf 数据缓冲区
 * @param len 缓冲区大小
 * @param flags 标志
 * @return 接收的字节数，0 连接关闭，-1 失败
 */
ssize_t sys_recv(int sockfd, void *buf, size_t len, int flags);

/**
 * @brief 接收数据并获取源地址
 */
ssize_t sys_recvfrom(int sockfd, void *buf, size_t len, int flags,
                     struct sockaddr *src_addr, socklen_t *addrlen);

/**
 * @brief 关闭 socket
 * @param sockfd socket 描述符
 * @return 0 成功，-1 失败
 */
int sys_closesocket(int sockfd);

/**
 * @brief 部分关闭 socket
 * @param sockfd socket 描述符
 * @param how 关闭方式（SHUT_RD/SHUT_WR/SHUT_RDWR）
 * @return 0 成功，-1 失败
 */
int sys_shutdown(int sockfd, int how);

/**
 * @brief 设置 socket 选项
 */
int sys_setsockopt(int sockfd, int level, int optname, 
                   const void *optval, socklen_t optlen);

/**
 * @brief 获取 socket 选项
 */
int sys_getsockopt(int sockfd, int level, int optname, 
                   void *optval, socklen_t *optlen);

/**
 * @brief 获取本地地址
 */
int sys_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief 获取对端地址
 */
int sys_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

#endif // _NET_SOCKET_H_

