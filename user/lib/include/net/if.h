/**
 * @file net/if.h
 * @brief 网络接口定义
 */

#ifndef _NET_IF_H_
#define _NET_IF_H_

#include <types.h>
#include <sys/socket.h>
#include <netinet/in.h>

// ============================================================================
// 网络接口 ioctl 请求码
// ============================================================================

#define SIOCBASE        0x8900

// 接口配置
#define SIOCGIFADDR     (SIOCBASE + 0x01)   // 获取 IP 地址
#define SIOCSIFADDR     (SIOCBASE + 0x02)   // 设置 IP 地址
#define SIOCGIFNETMASK  (SIOCBASE + 0x03)   // 获取子网掩码
#define SIOCSIFNETMASK  (SIOCBASE + 0x04)   // 设置子网掩码
#define SIOCGIFFLAGS    (SIOCBASE + 0x05)   // 获取接口标志
#define SIOCSIFFLAGS    (SIOCBASE + 0x06)   // 设置接口标志
#define SIOCGIFHWADDR   (SIOCBASE + 0x07)   // 获取 MAC 地址
#define SIOCGIFMTU      (SIOCBASE + 0x08)   // 获取 MTU
#define SIOCSIFMTU      (SIOCBASE + 0x09)   // 设置 MTU
#define SIOCGIFCONF     (SIOCBASE + 0x10)   // 获取接口列表
#define SIOCGIFINDEX    (SIOCBASE + 0x11)   // 获取接口索引
#define SIOCGIFGATEWAY  (SIOCBASE + 0x12)   // 获取网关
#define SIOCSIFGATEWAY  (SIOCBASE + 0x13)   // 设置网关

// ARP 操作
#define SIOCSARP        (SIOCBASE + 0x20)   // 添加 ARP 条目
#define SIOCGARP        (SIOCBASE + 0x21)   // 获取 ARP 条目
#define SIOCDARP        (SIOCBASE + 0x22)   // 删除 ARP 条目

// CastorOS 扩展
#define SIOCPING        (SIOCBASE + 0x40)   // Ping
#define SIOCGIFSTATS    (SIOCBASE + 0x41)   // 获取接口统计

// ============================================================================
// 接口标志
// ============================================================================

#define IFF_UP          0x0001      // 接口已启用
#define IFF_BROADCAST   0x0002      // 支持广播
#define IFF_DEBUG       0x0004      // 调试模式
#define IFF_LOOPBACK    0x0008      // 回环接口
#define IFF_POINTOPOINT 0x0010      // 点对点链路
#define IFF_NOTRAILERS  0x0020      // 避免使用 trailers
#define IFF_RUNNING     0x0040      // 资源已分配
#define IFF_NOARP       0x0080      // 无 ARP
#define IFF_PROMISC     0x0100      // 混杂模式
#define IFF_ALLMULTI    0x0200      // 接收所有多播
#define IFF_MULTICAST   0x1000      // 支持多播

// ============================================================================
// 接口名称长度
// ============================================================================

#define IFNAMSIZ        16

// ============================================================================
// 数据结构
// ============================================================================

/**
 * @brief 网络接口请求结构
 */
struct ifreq {
    char ifr_name[IFNAMSIZ];            // 接口名称
    union {
        struct sockaddr_in ifr_addr;    // IP 地址
        struct sockaddr_in ifr_netmask; // 子网掩码
        struct sockaddr_in ifr_gateway; // 网关地址
        struct {
            uint16_t sa_family;
            uint8_t  sa_data[14];
        } ifr_hwaddr;                   // MAC 地址
        int32_t ifr_flags;              // 标志
        int32_t ifr_mtu;                // MTU
        int32_t ifr_ifindex;            // 接口索引
    };
};

/**
 * @brief 接口配置结构（用于 SIOCGIFCONF）
 */
struct ifconf {
    int32_t ifc_len;                    // 缓冲区长度
    union {
        char         *ifc_buf;          // 缓冲区
        struct ifreq *ifc_req;          // 请求数组
    };
};

/**
 * @brief ARP 请求结构
 */
struct arpreq {
    struct sockaddr_in arp_pa;          // 协议地址
    struct {
        uint16_t sa_family;
        uint8_t  sa_data[14];
    } arp_ha;                           // 硬件地址
    int32_t arp_flags;                  // 标志
    char    arp_dev[IFNAMSIZ];          // 设备名称
};

// ARP 标志
#define ATF_COM         0x02    // 完整条目
#define ATF_PERM        0x04    // 永久条目
#define ATF_PUBL        0x08    // 发布条目
#define ATF_USETRAILERS 0x10    // 使用 trailers

/**
 * @brief Ping 请求结构（CastorOS 扩展）
 */
struct ping_req {
    char        host[64];       // 目标主机
    int32_t     count;          // 发送次数
    int32_t     timeout_ms;     // 超时（毫秒）
    uint32_t    sent;           // 已发送
    uint32_t    received;       // 已接收
    uint32_t    min_rtt;        // 最小 RTT
    uint32_t    max_rtt;        // 最大 RTT
    uint32_t    avg_rtt;        // 平均 RTT
};

/**
 * @brief 接口统计结构（CastorOS 扩展）
 */
struct ifstats {
    char        ifr_name[IFNAMSIZ];
    uint64_t    rx_packets;     // 接收包数
    uint64_t    tx_packets;     // 发送包数
    uint64_t    rx_bytes;       // 接收字节数
    uint64_t    tx_bytes;       // 发送字节数
    uint64_t    rx_errors;      // 接收错误
    uint64_t    tx_errors;      // 发送错误
    uint64_t    rx_dropped;     // 接收丢包
    uint64_t    tx_dropped;     // 发送丢包
};

// ============================================================================
// 函数声明
// ============================================================================

unsigned int if_nametoindex(const char *ifname);
char *if_indextoname(unsigned int ifindex, char *ifname);

#endif // _NET_IF_H_
