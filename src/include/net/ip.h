/**
 * @file ip.h
 * @brief IPv4 协议
 * 
 * 实现 RFC 791 定义的 IPv4 协议。
 * 
 * IPv4 头部格式:
 * +-------+-------+---------------+-------------------------------+
 * |Version| IHL   |      TOS      |         Total Length          |
 * +-------+-------+---------------+-------------------------------+
 * |      Identification           |Flags|    Fragment Offset      |
 * +-------------------------------+-------------------------------+
 * |   TTL         |   Protocol    |        Header Checksum        |
 * +-------------------------------+-------------------------------+
 * |                      Source IP Address                        |
 * +---------------------------------------------------------------+
 * |                   Destination IP Address                      |
 * +---------------------------------------------------------------+
 */

#ifndef _NET_IP_H_
#define _NET_IP_H_

#include <types.h>
#include <net/netbuf.h>
#include <net/netdev.h>

#define IP_VERSION_4        4       ///< IP 版本 4
#define IP_HEADER_MIN_LEN   20      ///< 最小头部长度
#define IP_DEFAULT_TTL      64      ///< 默认 TTL 值

// IP 协议号
#define IP_PROTO_ICMP       1       ///< ICMP
#define IP_PROTO_TCP        6       ///< TCP
#define IP_PROTO_UDP        17      ///< UDP

// IP 标志位
#define IP_FLAG_DF          0x4000  ///< Don't Fragment
#define IP_FLAG_MF          0x2000  ///< More Fragments
#define IP_FRAG_OFFSET_MASK 0x1FFF  ///< Fragment Offset 掩码

// IP 分片重组常量
#define IP_REASS_MAX_ENTRIES    8       ///< 最大同时重组条目数
#define IP_REASS_TIMEOUT        30000   ///< 重组超时（30秒，毫秒）
#define IP_REASS_MAX_SIZE       65535   ///< 最大 IP 数据包大小

/**
 * @brief IPv4 头部
 */
typedef struct ip_header {
    uint8_t  version_ihl;       ///< 版本 (4 bits) + 头部长度 (4 bits)
    uint8_t  tos;               ///< 服务类型
    uint16_t total_length;      ///< 总长度（网络字节序）
    uint16_t identification;    ///< 标识（网络字节序）
    uint16_t flags_fragment;    ///< 标志 (3 bits) + 分片偏移 (13 bits)（网络字节序）
    uint8_t  ttl;               ///< 生存时间
    uint8_t  protocol;          ///< 上层协议
    uint16_t checksum;          ///< 头部校验和（网络字节序）
    uint32_t src_addr;          ///< 源 IP 地址（网络字节序）
    uint32_t dst_addr;          ///< 目的 IP 地址（网络字节序）
} __attribute__((packed)) ip_header_t;

/**
 * @brief IP 分片结构
 */
typedef struct ip_fragment {
    uint16_t offset;            ///< 分片偏移（字节）
    uint16_t len;               ///< 分片长度
    uint8_t *data;              ///< 分片数据
    struct ip_fragment *next;   ///< 下一个分片
} ip_fragment_t;

/**
 * @brief IP 重组条目
 */
typedef struct ip_reassembly {
    uint32_t src_ip;            ///< 源 IP
    uint32_t dst_ip;            ///< 目的 IP
    uint16_t id;                ///< 标识
    uint8_t protocol;           ///< 协议
    
    uint16_t total_len;         ///< 总长度（0 表示未知）
    uint16_t received_len;      ///< 已接收长度
    
    ip_fragment_t *fragments;   ///< 分片链表（按偏移排序）
    
    uint32_t timeout;           ///< 超时时间
    bool valid;                 ///< 条目是否有效
} ip_reassembly_t;

// 路由表大小
#define IP_ROUTE_MAX        16      ///< 最大路由条目数

/**
 * @brief 路由条目
 */
typedef struct ip_route {
    uint32_t dest;              ///< 目的网络（网络字节序）
    uint32_t netmask;           ///< 子网掩码（网络字节序）
    uint32_t gateway;           ///< 网关（0 表示直连）
    netdev_t *dev;              ///< 出接口
    uint32_t metric;            ///< 度量值（跳数）
    bool valid;                 ///< 条目是否有效
} ip_route_t;

/**
 * @brief 初始化 IP 协议
 */
void ip_init(void);

/**
 * @brief IP 分片重组定时器（清理超时的重组条目）
 */
void ip_reass_timer(void);

/**
 * @brief 路由查找
 * @param dst_ip 目的 IP 地址（网络字节序）
 * @param next_hop 输出下一跳地址（网络字节序）
 * @return 出接口，NULL 表示无路由
 */
netdev_t *ip_route_lookup(uint32_t dst_ip, uint32_t *next_hop);

/**
 * @brief 添加路由
 * @param dest 目的网络（网络字节序）
 * @param netmask 子网掩码（网络字节序）
 * @param gateway 网关（网络字节序，0 表示直连）
 * @param dev 出接口
 * @param metric 度量值
 * @return 0 成功，-1 失败
 */
int ip_route_add(uint32_t dest, uint32_t netmask, uint32_t gateway, 
                 netdev_t *dev, uint32_t metric);

/**
 * @brief 删除路由
 * @param dest 目的网络（网络字节序）
 * @param netmask 子网掩码（网络字节序）
 * @return 0 成功，-1 失败
 */
int ip_route_del(uint32_t dest, uint32_t netmask);

/**
 * @brief 打印/输出路由表
 * @param buf 输出缓冲区，NULL 则直接打印到控制台
 * @param size 缓冲区大小（buf 非 NULL 时有效）
 * @return 写入/打印的字节数
 */
int ip_route_dump(char *buf, size_t size);

/**
 * @brief 处理接收到的 IP 数据包
 * @param dev 网络设备
 * @param buf 接收缓冲区
 */
void ip_input(netdev_t *dev, netbuf_t *buf);

/**
 * @brief 发送 IP 数据包
 * @param dev 网络设备（NULL 则自动选择）
 * @param buf 发送缓冲区（应包含上层协议数据）
 * @param dst_ip 目的 IP 地址（网络字节序）
 * @param protocol 上层协议号
 * @return 0 成功，-1 失败
 */
int ip_output(netdev_t *dev, netbuf_t *buf, uint32_t dst_ip, uint8_t protocol);

/**
 * @brief 计算 IP 头部校验和
 * @param header IP 头部指针
 * @param len 头部长度（字节）
 * @return 校验和（网络字节序）
 */
uint16_t ip_checksum(void *header, int len);

/**
 * @brief IP 地址转字符串
 * @param ip IP 地址（网络字节序）
 * @param buf 输出缓冲区（至少 16 字节）
 * @return buf 指针
 */
char *ip_to_str(uint32_t ip, char *buf);

/**
 * @brief 字符串转 IP 地址
 * @param str IP 地址字符串（如 "192.168.1.1"）
 * @param ip 输出 IP 地址（网络字节序）
 * @return 0 成功，-1 失败
 */
int str_to_ip(const char *str, uint32_t *ip);

/**
 * @brief 检查 IP 地址是否在同一子网
 * @param ip1 第一个 IP 地址（网络字节序）
 * @param ip2 第二个 IP 地址（网络字节序）
 * @param netmask 子网掩码（网络字节序）
 * @return true 同一子网，false 不同子网
 */
bool ip_same_subnet(uint32_t ip1, uint32_t ip2, uint32_t netmask);

/**
 * @brief 获取下一跳 IP 地址
 * @param dev 网络设备
 * @param dst_ip 目的 IP 地址（网络字节序）
 * @return 下一跳 IP 地址（网络字节序）
 */
uint32_t ip_get_next_hop(netdev_t *dev, uint32_t dst_ip);

/**
 * @brief 获取 IP 头部长度
 * @param ip IP 头部指针
 * @return 头部长度（字节）
 */
static inline uint8_t ip_header_len(ip_header_t *ip) {
    return (ip->version_ihl & 0x0F) * 4;
}

/**
 * @brief 获取 IP 版本
 * @param ip IP 头部指针
 * @return IP 版本
 */
static inline uint8_t ip_version(ip_header_t *ip) {
    return (ip->version_ihl >> 4) & 0x0F;
}

// 字节序转换宏
#define htons(x) ((uint16_t)(((x) << 8) | (((x) >> 8) & 0xFF)))
#define ntohs(x) htons(x)
#define htonl(x) ((uint32_t)(((x) << 24) | (((x) >> 8) & 0xFF00) | \
                 (((x) << 8) & 0xFF0000) | (((x) >> 24) & 0xFF)))
#define ntohl(x) htonl(x)

// 构造 IP 地址的宏（主机字节序）
#define IP_ADDR(a, b, c, d) (((uint32_t)(a)) | ((uint32_t)(b) << 8) | \
                            ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#endif // _NET_IP_H_

