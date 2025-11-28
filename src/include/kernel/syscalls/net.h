/**
 * @file net.h
 * @brief 网络相关系统调用声明
 * 
 * 提供符合 POSIX 标准的 BSD Socket API 以及网络配置 ioctl 接口。
 */

#ifndef _KERNEL_SYSCALLS_NET_H_
#define _KERNEL_SYSCALLS_NET_H_

#include <types.h>
#include <net/socket.h>

/* ============================================================================
 * 网络配置 ioctl 请求码
 * 用于 ifconfig、arp 等网络管理功能
 * ============================================================================ */

// ioctl 请求码基址
#define SIOCBASE        0x8900

// 网络接口配置
#define SIOCGIFADDR     (SIOCBASE + 0x01)  // 获取 IP 地址
#define SIOCSIFADDR     (SIOCBASE + 0x02)  // 设置 IP 地址
#define SIOCGIFNETMASK  (SIOCBASE + 0x03)  // 获取子网掩码
#define SIOCSIFNETMASK  (SIOCBASE + 0x04)  // 设置子网掩码
#define SIOCGIFFLAGS    (SIOCBASE + 0x05)  // 获取接口标志
#define SIOCSIFFLAGS    (SIOCBASE + 0x06)  // 设置接口标志
#define SIOCGIFHWADDR   (SIOCBASE + 0x07)  // 获取 MAC 地址
#define SIOCGIFMTU      (SIOCBASE + 0x08)  // 获取 MTU
#define SIOCSIFMTU      (SIOCBASE + 0x09)  // 设置 MTU
#define SIOCGIFCONF     (SIOCBASE + 0x10)  // 获取接口列表
#define SIOCGIFINDEX    (SIOCBASE + 0x11)  // 获取接口索引

// ARP 操作
#define SIOCSARP        (SIOCBASE + 0x20)  // 添加 ARP 条目
#define SIOCGARP        (SIOCBASE + 0x21)  // 获取 ARP 条目
#define SIOCDARP        (SIOCBASE + 0x22)  // 删除 ARP 条目

// 路由操作
#define SIOCADDRT       (SIOCBASE + 0x30)  // 添加路由
#define SIOCDELRT       (SIOCBASE + 0x31)  // 删除路由

// CastorOS 扩展：ICMP ping（用于内核调试/测试）
#define SIOCPING        (SIOCBASE + 0x40)  // ICMP ping

// 接口标志
#define IFF_UP          0x0001  // 接口启用
#define IFF_BROADCAST   0x0002  // 支持广播
#define IFF_LOOPBACK    0x0008  // 回环接口
#define IFF_RUNNING     0x0040  // 资源已分配
#define IFF_MULTICAST   0x1000  // 支持多播

/* ============================================================================
 * ioctl 数据结构定义
 * ============================================================================ */

/**
 * @brief 网络接口请求结构（用于 ioctl）
 * 类似 Linux 的 struct ifreq
 */
struct ifreq {
    char ifr_name[16];              // 接口名称
    union {
        struct sockaddr_in ifr_addr;      // IP 地址
        struct sockaddr_in ifr_netmask;   // 子网掩码
        struct sockaddr_in ifr_gateway;   // 网关地址
        struct {
            uint8_t sa_data[14];          // 硬件地址
        } ifr_hwaddr;
        int32_t ifr_flags;                // 接口标志
        int32_t ifr_mtu;                  // MTU
        int32_t ifr_ifindex;              // 接口索引
    };
};

/**
 * @brief 接口配置列表（用于 SIOCGIFCONF）
 */
struct ifconf {
    int32_t ifc_len;                // 缓冲区长度
    union {
        char *ifc_buf;              // 缓冲区
        struct ifreq *ifc_req;      // 请求数组
    };
};

/**
 * @brief ARP 请求结构（用于 ioctl）
 * 类似 Linux 的 struct arpreq
 */
struct arpreq {
    struct sockaddr_in arp_pa;      // 协议地址（IP）
    struct {
        uint16_t sa_family;
        uint8_t sa_data[14];        // 硬件地址（MAC）
    } arp_ha;
    int32_t arp_flags;              // 标志
    char arp_dev[16];               // 设备名称
};

// ARP 标志
#define ATF_COM         0x02        // 已完成（已解析）
#define ATF_PERM        0x04        // 永久条目
#define ATF_PUBL        0x08        // 发布

/**
 * @brief Ping 请求结构（CastorOS 扩展，用于 SIOCPING）
 */
struct ping_req {
    char host[64];                  // 目标主机（IP 地址字符串）
    int32_t count;                  // ping 次数
    int32_t timeout_ms;             // 超时（毫秒）
    // 输出
    uint32_t sent;                  // 发送的包数
    uint32_t received;              // 收到的包数
    uint32_t min_rtt;               // 最小 RTT（毫秒）
    uint32_t max_rtt;               // 最大 RTT（毫秒）
    uint32_t avg_rtt;               // 平均 RTT（毫秒）
};

/* ============================================================================
 * 兼容性结构体（保持向后兼容）
 * ============================================================================ */

/**
 * @brief 网络接口信息结构（用于用户空间）
 * @deprecated 建议使用 ioctl + ifreq 代替
 */
typedef struct {
    char name[16];              // 接口名称
    uint8_t mac[6];             // MAC 地址
    uint8_t padding[2];         // 对齐填充
    uint32_t ip_addr;           // IP 地址（网络字节序）
    uint32_t netmask;           // 子网掩码
    uint32_t gateway;           // 网关地址
    uint32_t mtu;               // 最大传输单元
    uint8_t state;              // 状态：0=down, 1=up
    uint8_t reserved[3];        // 保留
    uint64_t rx_packets;        // 接收包数
    uint64_t tx_packets;        // 发送包数
    uint64_t rx_bytes;          // 接收字节数
    uint64_t tx_bytes;          // 发送字节数
} netif_info_t;

/**
 * @brief ARP 缓存条目结构（用于用户空间）
 */
typedef struct {
    uint32_t ip_addr;           // IP 地址
    uint8_t mac[6];             // MAC 地址
    uint8_t state;              // 状态：0=free, 1=pending, 2=resolved
    uint8_t padding;
} arp_entry_info_t;

/* ============================================================================
 * 系统调用函数声明
 * ============================================================================ */

/**
 * @brief 通用 ioctl 系统调用
 * @param fd 文件描述符（socket fd 或特殊设备）
 * @param request ioctl 请求码
 * @param argp 请求相关的参数
 * @return 0 成功，-1 失败
 */
int32_t sys_ioctl(int32_t fd, uint32_t request, void *argp);

#endif // _KERNEL_SYSCALLS_NET_H_
