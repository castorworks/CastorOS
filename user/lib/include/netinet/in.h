/**
 * @file netinet/in.h
 * @brief Internet 地址族定义
 * 
 * 符合 POSIX.1-2008 标准
 */

#ifndef _NETINET_IN_H_
#define _NETINET_IN_H_

#include <types.h>
#include <sys/socket.h>

// ============================================================================
// 特殊地址
// ============================================================================

#define INADDR_ANY          ((in_addr_t)0x00000000)
#define INADDR_BROADCAST    ((in_addr_t)0xFFFFFFFF)
#define INADDR_LOOPBACK     ((in_addr_t)0x7F000001)
#define INADDR_NONE         ((in_addr_t)0xFFFFFFFF)

// ============================================================================
// 类型定义
// ============================================================================

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

/**
 * @brief IPv4 地址结构
 */
struct in_addr {
    in_addr_t s_addr;
};

/**
 * @brief IPv4 socket 地址结构
 */
struct sockaddr_in {
    sa_family_t     sin_family;     // AF_INET
    in_port_t       sin_port;       // 端口号（网络字节序）
    struct in_addr  sin_addr;       // IP 地址
    uint8_t         sin_zero[8];    // 填充至 16 字节
};

// ============================================================================
// IPv6（预留）
// ============================================================================

struct in6_addr {
    uint8_t s6_addr[16];
};

struct sockaddr_in6 {
    sa_family_t     sin6_family;    // AF_INET6
    in_port_t       sin6_port;      // 端口号
    uint32_t        sin6_flowinfo;  // 流信息
    struct in6_addr sin6_addr;      // IPv6 地址
    uint32_t        sin6_scope_id;  // 范围 ID
};

// ============================================================================
// 字节序转换
// ============================================================================

#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif

static inline __maybe_unused uint16_t htons(uint16_t hostshort) {
    return ((hostshort & 0xFF) << 8) | ((hostshort >> 8) & 0xFF);
}

static inline __maybe_unused uint16_t ntohs(uint16_t netshort) {
    return htons(netshort);
}

static inline __maybe_unused uint32_t htonl(uint32_t hostlong) {
    return ((hostlong & 0xFF) << 24) |
           ((hostlong & 0xFF00) << 8) |
           ((hostlong >> 8) & 0xFF00) |
           ((hostlong >> 24) & 0xFF);
}

static inline __maybe_unused uint32_t ntohl(uint32_t netlong) {
    return htonl(netlong);
}

// ============================================================================
// 缓冲区大小
// ============================================================================

#define INET_ADDRSTRLEN     16      // IPv4 字符串长度
#define INET6_ADDRSTRLEN    46      // IPv6 字符串长度

#endif // _NETINET_IN_H_
