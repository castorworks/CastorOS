/**
 * @file sys/socket.h
 * @brief BSD Socket API
 * 
 * 符合 POSIX.1-2008 标准
 */

#ifndef _SYS_SOCKET_H_
#define _SYS_SOCKET_H_

#include <types.h>

// ============================================================================
// 地址族
// ============================================================================

#define AF_UNSPEC       0       // 未指定
#define AF_LOCAL        1       // 本地通信
#define AF_UNIX         AF_LOCAL
#define AF_INET         2       // IPv4
#define AF_INET6        10      // IPv6

#define PF_UNSPEC       AF_UNSPEC
#define PF_LOCAL        AF_LOCAL
#define PF_UNIX         AF_UNIX
#define PF_INET         AF_INET
#define PF_INET6        AF_INET6

// ============================================================================
// Socket 类型
// ============================================================================

#define SOCK_STREAM     1       // 流式（TCP）
#define SOCK_DGRAM      2       // 数据报（UDP）
#define SOCK_RAW        3       // 原始套接字
#define SOCK_SEQPACKET  5       // 顺序数据包

// Socket 类型标志（与类型 OR）
#define SOCK_NONBLOCK   0x0800  // 非阻塞
#define SOCK_CLOEXEC    0x80000 // exec 时关闭

// ============================================================================
// 协议号
// ============================================================================

#define IPPROTO_IP      0       // IP 协议
#define IPPROTO_ICMP    1       // ICMP
#define IPPROTO_TCP     6       // TCP
#define IPPROTO_UDP     17      // UDP
#define IPPROTO_RAW     255     // 原始 IP

// ============================================================================
// Socket 选项级别
// ============================================================================

#define SOL_SOCKET      1       // Socket 级别

// ============================================================================
// Socket 选项（SOL_SOCKET 级别）
// ============================================================================

#define SO_DEBUG        1       // 调试信息
#define SO_REUSEADDR    2       // 地址重用
#define SO_TYPE         3       // Socket 类型
#define SO_ERROR        4       // 错误状态
#define SO_DONTROUTE    5       // 不使用路由
#define SO_BROADCAST    6       // 允许广播
#define SO_SNDBUF       7       // 发送缓冲区大小
#define SO_RCVBUF       8       // 接收缓冲区大小
#define SO_KEEPALIVE    9       // 保持连接
#define SO_OOBINLINE    10      // 带外数据内联
#define SO_LINGER       13      // 延迟关闭
#define SO_RCVTIMEO     20      // 接收超时
#define SO_SNDTIMEO     21      // 发送超时
#define SO_ACCEPTCONN   30      // 是否在监听

// ============================================================================
// shutdown() how 参数
// ============================================================================

#define SHUT_RD         0       // 关闭读端
#define SHUT_WR         1       // 关闭写端
#define SHUT_RDWR       2       // 关闭读写

// ============================================================================
// send/recv 标志
// ============================================================================

#define MSG_OOB         0x01    // 带外数据
#define MSG_PEEK        0x02    // 预览数据
#define MSG_DONTROUTE   0x04    // 不使用路由
#define MSG_DONTWAIT    0x40    // 非阻塞
#define MSG_WAITALL     0x100   // 等待所有数据
#define MSG_NOSIGNAL    0x4000  // 不发送 SIGPIPE

// ============================================================================
// 其他常量
// ============================================================================

#define SOMAXCONN       128     // listen() 最大等待队列

// ============================================================================
// 类型定义
// ============================================================================

typedef uint32_t socklen_t;
typedef uint16_t sa_family_t;

/**
 * @brief 通用 socket 地址结构
 */
struct sockaddr {
    sa_family_t sa_family;      // 地址族
    char        sa_data[14];    // 地址数据
};

/**
 * @brief socket 地址存储结构（足够存储任何地址）
 */
struct sockaddr_storage {
    sa_family_t ss_family;
    char        __ss_pad[126];
};

// ============================================================================
// Socket 函数
// ============================================================================

/**
 * @brief 创建 socket
 * @param domain 地址族（AF_INET 等）
 * @param type 类型（SOCK_STREAM/SOCK_DGRAM）
 * @param protocol 协议（通常为 0）
 * @return socket 描述符，-1 失败
 */
int socket(int domain, int type, int protocol);

/**
 * @brief 绑定地址
 */
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

/**
 * @brief 监听连接
 */
int listen(int sockfd, int backlog);

/**
 * @brief 接受连接
 */
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief 发起连接
 */
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

/**
 * @brief 发送数据
 */
ssize_t send(int sockfd, const void *buf, size_t len, int flags);

/**
 * @brief 发送数据到指定地址
 */
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);

/**
 * @brief 接收数据
 */
ssize_t recv(int sockfd, void *buf, size_t len, int flags);

/**
 * @brief 接收数据并获取源地址
 */
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);

/**
 * @brief 部分关闭 socket
 */
int shutdown(int sockfd, int how);

/**
 * @brief 设置 socket 选项
 */
int setsockopt(int sockfd, int level, int optname,
               const void *optval, socklen_t optlen);

/**
 * @brief 获取 socket 选项
 */
int getsockopt(int sockfd, int level, int optname,
               void *optval, socklen_t *optlen);

/**
 * @brief 获取本地地址
 */
int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief 获取对端地址
 */
int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief 创建 socket 对
 */
int socketpair(int domain, int type, int protocol, int sv[2]);

#endif // _SYS_SOCKET_H_
