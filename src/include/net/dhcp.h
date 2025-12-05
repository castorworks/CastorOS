/**
 * @file dhcp.h
 * @brief DHCP 客户端定义
 * 
 * 实现 RFC 2131 DHCP 协议客户端功能
 */

#ifndef _NET_DHCP_H_
#define _NET_DHCP_H_

#include <types.h>
#include <net/netdev.h>

// ============================================================================
// DHCP 常量定义
// ============================================================================

#define DHCP_SERVER_PORT    67      // DHCP 服务器端口
#define DHCP_CLIENT_PORT    68      // DHCP 客户端端口

// DHCP 操作码
#define DHCP_OP_REQUEST     1       // 客户端请求
#define DHCP_OP_REPLY       2       // 服务器响应

// 硬件类型
#define DHCP_HTYPE_ETH      1       // 以太网

// DHCP 消息类型（选项 53）
#define DHCP_DISCOVER       1       // 发现
#define DHCP_OFFER          2       // 提供
#define DHCP_REQUEST        3       // 请求
#define DHCP_DECLINE        4       // 拒绝
#define DHCP_ACK            5       // 确认
#define DHCP_NAK            6       // 否认
#define DHCP_RELEASE        7       // 释放
#define DHCP_INFORM         8       // 通知

// DHCP 选项
#define DHCP_OPT_PAD            0       // 填充
#define DHCP_OPT_SUBNET_MASK    1       // 子网掩码
#define DHCP_OPT_ROUTER         3       // 默认网关
#define DHCP_OPT_DNS            6       // DNS 服务器
#define DHCP_OPT_HOSTNAME       12      // 主机名
#define DHCP_OPT_DOMAIN         15      // 域名
#define DHCP_OPT_BROADCAST      28      // 广播地址
#define DHCP_OPT_REQ_IP         50      // 请求的 IP 地址
#define DHCP_OPT_LEASE_TIME     51      // 租约时间
#define DHCP_OPT_MSG_TYPE       53      // DHCP 消息类型
#define DHCP_OPT_SERVER_ID      54      // DHCP 服务器标识
#define DHCP_OPT_PARAM_REQ      55      // 参数请求列表
#define DHCP_OPT_RENEWAL_TIME   58      // 续约时间 (T1)
#define DHCP_OPT_REBIND_TIME    59      // 重新绑定时间 (T2)
#define DHCP_OPT_CLIENT_ID      61      // 客户端标识
#define DHCP_OPT_END            255     // 结束

// DHCP Magic Cookie
#define DHCP_MAGIC_COOKIE       0x63825363U

// DHCP 超时
#define DHCP_DISCOVER_TIMEOUT   4000    // 发现超时（毫秒）
#define DHCP_REQUEST_TIMEOUT    4000    // 请求超时（毫秒）
#define DHCP_MAX_RETRIES        4       // 最大重试次数

// ============================================================================
// DHCP 数据结构
// ============================================================================

/**
 * DHCP 报文头（固定部分）
 */
typedef struct __attribute__((packed)) {
    uint8_t op;             // 操作码
    uint8_t htype;          // 硬件类型
    uint8_t hlen;           // 硬件地址长度
    uint8_t hops;           // 跳数
    uint32_t xid;           // 事务 ID
    uint16_t secs;          // 客户端启动后的秒数
    uint16_t flags;         // 标志
    uint32_t ciaddr;        // 客户端 IP 地址
    uint32_t yiaddr;        // "你的" IP 地址（服务器分配）
    uint32_t siaddr;        // 下一个服务器 IP 地址
    uint32_t giaddr;        // 网关 IP 地址
    uint8_t chaddr[16];     // 客户端硬件地址
    uint8_t sname[64];      // 服务器主机名
    uint8_t file[128];      // 引导文件名
    uint32_t magic;         // Magic Cookie
    uint8_t options[312];   // 选项（可变长度）
} dhcp_packet_t;

/**
 * DHCP 客户端状态
 */
typedef enum {
    DHCP_STATE_INIT,        // 初始状态
    DHCP_STATE_SELECTING,   // 选择中（等待 OFFER）
    DHCP_STATE_REQUESTING,  // 请求中（等待 ACK）
    DHCP_STATE_BOUND,       // 已绑定
    DHCP_STATE_RENEWING,    // 续约中
    DHCP_STATE_REBINDING,   // 重新绑定中
    DHCP_STATE_ERROR        // 错误状态
} dhcp_state_t;

/**
 * DHCP 配置信息
 */
typedef struct {
    uint32_t ip_addr;       // 分配的 IP 地址
    uint32_t netmask;       // 子网掩码
    uint32_t gateway;       // 默认网关
    uint32_t dns_primary;   // 主 DNS 服务器
    uint32_t dns_secondary; // 备用 DNS 服务器
    uint32_t server_ip;     // DHCP 服务器 IP
    uint32_t lease_time;    // 租约时间（秒）
    uint32_t renewal_time;  // 续约时间 T1（秒）
    uint32_t rebind_time;   // 重绑时间 T2（秒）
    uint32_t lease_start;   // 租约开始时间（系统 tick）
} dhcp_info_t;

/**
 * DHCP 客户端上下文
 */
typedef struct {
    netdev_t *dev;          // 网络设备
    dhcp_state_t state;     // 客户端状态
    dhcp_info_t info;       // 配置信息
    uint32_t xid;           // 当前事务 ID
    int socket_fd;          // UDP socket
    uint8_t retries;        // 重试次数
} dhcp_client_t;

// ============================================================================
// DHCP 函数接口
// ============================================================================

/**
 * @brief 启动 DHCP 客户端
 * @param dev 网络设备
 * @return 0 成功，-1 失败
 */
int dhcp_start(netdev_t *dev);

/**
 * @brief 停止 DHCP 客户端
 * @param dev 网络设备
 */
void dhcp_stop(netdev_t *dev);

/**
 * @brief 释放 DHCP 租约
 * @param dev 网络设备
 * @return 0 成功，-1 失败
 */
int dhcp_release(netdev_t *dev);

/**
 * @brief 获取 DHCP 状态
 * @param dev 网络设备
 * @param info 输出配置信息
 * @return 当前状态
 */
dhcp_state_t dhcp_get_status(netdev_t *dev, dhcp_info_t *info);

/**
 * @brief DHCP 定时器处理（需要定期调用）
 * 
 * 处理租约续期和重绑定
 */
void dhcp_timer(void);

/**
 * @brief 处理收到的 DHCP 数据包
 * @param dev 网络设备
 * @param data 数据包内容
 * @param len 数据包长度
 */
void dhcp_input(netdev_t *dev, uint8_t *data, uint32_t len);

#endif // _NET_DHCP_H_

