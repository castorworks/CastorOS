/**
 * @file arp.h
 * @brief ARP（地址解析协议）
 * 
 * 实现 RFC 826 定义的 ARP 协议，用于将 IP 地址解析为 MAC 地址。
 * 
 * ARP 报文格式:
 * +----------------+----------------+
 * | Hardware Type  | Protocol Type  |
 * +----------------+----------------+
 * | HW Len | P Len | Operation      |
 * +----------------+----------------+
 * |     Sender MAC Address          |
 * +----------------+----------------+
 * |     Sender IP Address           |
 * +----------------+----------------+
 * |     Target MAC Address          |
 * +----------------+----------------+
 * |     Target IP Address           |
 * +----------------+----------------+
 */

#ifndef _NET_ARP_H_
#define _NET_ARP_H_

#include <types.h>
#include <net/netbuf.h>
#include <net/netdev.h>

// ARP 硬件类型
#define ARP_HARDWARE_ETHERNET   1       ///< 以太网

// ARP 协议类型
#define ARP_PROTOCOL_IP         0x0800  ///< IPv4

// ARP 操作码
#define ARP_OP_REQUEST          1       ///< ARP 请求
#define ARP_OP_REPLY            2       ///< ARP 应答

// ARP 缓存配置
#define ARP_CACHE_SIZE          32      ///< 缓存条目数
#define ARP_CACHE_TIMEOUT       300000  ///< 缓存超时时间（5分钟，毫秒）
#define ARP_RETRY_INTERVAL      1000    ///< ARP 重试间隔（毫秒）
#define ARP_MAX_RETRIES         3       ///< 最大重试次数

/**
 * @brief ARP 报文头部
 */
typedef struct arp_header {
    uint16_t hardware_type;     ///< 硬件类型（1 = 以太网）
    uint16_t protocol_type;     ///< 协议类型（0x0800 = IP）
    uint8_t  hardware_len;      ///< 硬件地址长度（6）
    uint8_t  protocol_len;      ///< 协议地址长度（4）
    uint16_t operation;         ///< 操作码（1=请求，2=应答）
    uint8_t  sender_mac[6];     ///< 发送方 MAC 地址
    uint32_t sender_ip;         ///< 发送方 IP 地址
    uint8_t  target_mac[6];     ///< 目标 MAC 地址
    uint32_t target_ip;         ///< 目标 IP 地址
} __attribute__((packed)) arp_header_t;

/**
 * @brief ARP 缓存条目状态
 */
typedef enum {
    ARP_STATE_FREE,         ///< 空闲条目
    ARP_STATE_PENDING,      ///< 正在等待 ARP 响应
    ARP_STATE_RESOLVED,     ///< 已解析
} arp_state_t;

/**
 * @brief ARP 缓存条目
 */
typedef struct arp_entry {
    uint32_t    ip_addr;        ///< IP 地址
    uint8_t     mac_addr[6];    ///< MAC 地址
    uint32_t    timestamp;      ///< 上次更新时间
    arp_state_t state;          ///< 条目状态
    uint8_t     retries;        ///< 重试次数
    netbuf_t    *pending_queue; ///< 等待发送的数据包队列
} arp_entry_t;

/**
 * @brief 初始化 ARP 协议
 */
void arp_init(void);

/**
 * @brief 处理接收到的 ARP 报文
 * @param dev 网络设备
 * @param buf 接收缓冲区
 */
void arp_input(netdev_t *dev, netbuf_t *buf);

/**
 * @brief 解析 IP 地址对应的 MAC 地址
 * @param dev 网络设备
 * @param ip 目标 IP 地址（网络字节序）
 * @param mac 输出 MAC 地址（6字节）
 * @return 0 成功（mac 已填充），-1 正在解析中，-2 失败
 */
int arp_resolve(netdev_t *dev, uint32_t ip, uint8_t *mac);

/**
 * @brief 发送 ARP 请求
 * @param dev 网络设备
 * @param target_ip 目标 IP 地址（网络字节序）
 * @return 0 成功，-1 失败
 */
int arp_request(netdev_t *dev, uint32_t target_ip);

/**
 * @brief 发送 ARP 应答
 * @param dev 网络设备
 * @param target_ip 目标 IP 地址（网络字节序）
 * @param target_mac 目标 MAC 地址
 * @return 0 成功，-1 失败
 */
int arp_reply(netdev_t *dev, uint32_t target_ip, const uint8_t *target_mac);

/**
 * @brief 添加或更新 ARP 缓存条目
 * @param ip IP 地址（网络字节序）
 * @param mac MAC 地址
 */
void arp_cache_update(uint32_t ip, const uint8_t *mac);

/**
 * @brief 查找 ARP 缓存
 * @param ip IP 地址（网络字节序）
 * @param mac 输出 MAC 地址（6字节）
 * @return 0 找到，-1 未找到
 */
int arp_cache_lookup(uint32_t ip, uint8_t *mac);

/**
 * @brief 添加静态 ARP 条目
 * @param ip IP 地址（网络字节序）
 * @param mac MAC 地址
 * @return 0 成功，-1 失败
 */
int arp_cache_add_static(uint32_t ip, const uint8_t *mac);

/**
 * @brief 删除 ARP 缓存条目
 * @param ip IP 地址（网络字节序）
 * @return 0 成功，-1 未找到
 */
int arp_cache_delete(uint32_t ip);

/**
 * @brief 清理过期的 ARP 缓存条目
 */
void arp_cache_cleanup(void);

/**
 * @brief 清空所有 ARP 缓存
 */
void arp_cache_clear(void);

/**
 * @brief 打印 ARP 缓存表
 */
void arp_cache_dump(void);

/**
 * @brief 获取 ARP 缓存条目数量
 * @return 当前缓存条目数
 */
int arp_cache_count(void);

/**
 * @brief 获取 ARP 缓存条目
 * @param index 条目索引（0 到 ARP_CACHE_SIZE-1）
 * @param ip 输出 IP 地址
 * @param mac 输出 MAC 地址（6 字节）
 * @param state 输出状态
 * @return 0 成功（条目有效），-1 条目无效或索引越界
 */
int arp_cache_get_entry(int index, uint32_t *ip, uint8_t *mac, uint8_t *state);

/**
 * @brief 将数据包加入 ARP 等待队列
 * @param ip 目标 IP 地址
 * @param buf 数据包
 * @return 0 成功，-1 失败
 */
int arp_queue_packet(uint32_t ip, netbuf_t *buf);

#endif // _NET_ARP_H_

