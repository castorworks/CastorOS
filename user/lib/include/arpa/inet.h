/**
 * @file arpa/inet.h
 * @brief Internet 地址转换函数
 * 
 * 符合 POSIX.1-2008 标准
 */

#ifndef _ARPA_INET_H_
#define _ARPA_INET_H_

#include <netinet/in.h>

// ============================================================================
// 地址转换函数
// ============================================================================

/**
 * @brief 将点分十进制 IP 地址转换为网络字节序整数
 * @param cp 点分十进制字符串（如 "192.168.1.1"）
 * @return 网络字节序的 IP 地址，失败返回 INADDR_NONE
 */
in_addr_t inet_addr(const char *cp);

/**
 * @brief 将网络字节序 IP 地址转换为点分十进制字符串
 * @param in IP 地址结构
 * @return 静态缓冲区中的字符串（非线程安全）
 */
char *inet_ntoa(struct in_addr in);

/**
 * @brief 将点分十进制字符串转换为二进制地址
 * @param cp 点分十进制字符串
 * @param inp 输出地址结构
 * @return 1 成功，0 失败
 */
int inet_aton(const char *cp, struct in_addr *inp);

/**
 * @brief 通用地址转换（字符串 -> 二进制）
 * @param af 地址族（AF_INET/AF_INET6）
 * @param src 字符串地址
 * @param dst 输出缓冲区
 * @return 1 成功，0 格式错误，-1 不支持的地址族
 */
int inet_pton(int af, const char *src, void *dst);

/**
 * @brief 通用地址转换（二进制 -> 字符串）
 * @param af 地址族（AF_INET/AF_INET6）
 * @param src 二进制地址
 * @param dst 输出缓冲区
 * @param size 缓冲区大小
 * @return dst 成功，NULL 失败
 */
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);

// ============================================================================
// IP 地址辅助宏
// ============================================================================

/**
 * @brief 从四个字节构造 IP 地址（网络字节序）
 */
#define MAKE_IP(a, b, c, d) \
    ((in_addr_t)((a) | ((b) << 8) | ((c) << 16) | ((d) << 24)))

/**
 * @brief 获取 IP 地址的各个字节
 */
#define IP_BYTE1(ip)    ((uint8_t)((ip) & 0xFF))
#define IP_BYTE2(ip)    ((uint8_t)(((ip) >> 8) & 0xFF))
#define IP_BYTE3(ip)    ((uint8_t)(((ip) >> 16) & 0xFF))
#define IP_BYTE4(ip)    ((uint8_t)(((ip) >> 24) & 0xFF))

#endif // _ARPA_INET_H_
