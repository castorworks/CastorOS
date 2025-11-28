/**
 * @file ethernet.h
 * @brief 以太网帧处理
 * 
 * 实现 Ethernet II 帧的收发处理。
 * 
 * 帧格式:
 * +------------------+------------------+----------+-----------------+-----+
 * |  Dest MAC (6B)   |  Src MAC (6B)    | Type(2B) | Payload (46-1500) | FCS |
 * +------------------+------------------+----------+-----------------+-----+
 */

#ifndef _NET_ETHERNET_H_
#define _NET_ETHERNET_H_

#include <types.h>
#include <net/netbuf.h>
#include <net/netdev.h>

#define ETH_HEADER_LEN      14      ///< 以太网头部长度
#define ETH_ADDR_LEN        6       ///< MAC 地址长度
#define ETH_MTU             1500    ///< 最大传输单元
#define ETH_MIN_FRAME_LEN   60      ///< 最小帧长度（不含 FCS）
#define ETH_MAX_FRAME_LEN   1514    ///< 最大帧长度（不含 FCS）

// EtherType 值
#define ETH_TYPE_IP         0x0800  ///< IPv4
#define ETH_TYPE_ARP        0x0806  ///< ARP
#define ETH_TYPE_IPV6       0x86DD  ///< IPv6

// 广播 MAC 地址
extern const uint8_t ETH_BROADCAST_ADDR[ETH_ADDR_LEN];

// 零 MAC 地址
extern const uint8_t ETH_ZERO_ADDR[ETH_ADDR_LEN];

/**
 * @brief 以太网帧头部
 */
typedef struct eth_header {
    uint8_t  dst[ETH_ADDR_LEN];     ///< 目的 MAC 地址
    uint8_t  src[ETH_ADDR_LEN];     ///< 源 MAC 地址
    uint16_t type;                   ///< EtherType（网络字节序）
} __attribute__((packed)) eth_header_t;

/**
 * @brief 初始化以太网层
 */
void ethernet_init(void);

/**
 * @brief 处理接收到的以太网帧
 * @param dev 网络设备
 * @param buf 接收缓冲区
 */
void ethernet_input(netdev_t *dev, netbuf_t *buf);

/**
 * @brief 发送以太网帧
 * @param dev 网络设备
 * @param buf 发送缓冲区（应包含上层协议数据）
 * @param dst_mac 目的 MAC 地址
 * @param type EtherType
 * @return 0 成功，-1 失败
 */
int ethernet_output(netdev_t *dev, netbuf_t *buf, const uint8_t *dst_mac, uint16_t type);

/**
 * @brief 比较两个 MAC 地址
 * @param a 第一个 MAC 地址
 * @param b 第二个 MAC 地址
 * @return 0 相等，非 0 不相等
 */
int mac_addr_cmp(const uint8_t *a, const uint8_t *b);

/**
 * @brief 复制 MAC 地址
 * @param dst 目的地址
 * @param src 源地址
 */
void mac_addr_copy(uint8_t *dst, const uint8_t *src);

/**
 * @brief 检查是否为广播地址
 * @param addr MAC 地址
 * @return true 是广播地址，false 不是
 */
bool mac_addr_is_broadcast(const uint8_t *addr);

/**
 * @brief 检查是否为多播地址
 * @param addr MAC 地址
 * @return true 是多播地址，false 不是
 */
bool mac_addr_is_multicast(const uint8_t *addr);

/**
 * @brief 检查是否为零地址
 * @param addr MAC 地址
 * @return true 是零地址，false 不是
 */
bool mac_addr_is_zero(const uint8_t *addr);

/**
 * @brief MAC 地址转字符串
 * @param mac MAC 地址
 * @param buf 输出缓冲区（至少 18 字节）
 * @return buf 指针
 */
char *mac_to_str(const uint8_t *mac, char *buf);

#endif // _NET_ETHERNET_H_

