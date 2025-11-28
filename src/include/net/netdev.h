/**
 * @file netdev.h
 * @brief 网络设备抽象层
 * 
 * 提供统一的网络设备接口，支持多网卡管理。
 */

#ifndef _NET_NETDEV_H_
#define _NET_NETDEV_H_

#include <types.h>
#include <net/netbuf.h>
#include <kernel/sync/mutex.h>

#define NETDEV_NAME_LEN     16      ///< 设备名称最大长度
#define MAC_ADDR_LEN        6       ///< MAC 地址长度
#define MAX_NETDEV          4       ///< 最大网络设备数

/**
 * @brief 网络设备状态
 */
typedef enum {
    NETDEV_DOWN,        ///< 设备未启用
    NETDEV_UP,          ///< 设备已启用
} netdev_state_t;

/**
 * @brief 网络设备操作函数（虚函数表）
 */
struct netdev;

typedef struct netdev_ops {
    int (*open)(struct netdev *dev);                        ///< 打开设备
    int (*close)(struct netdev *dev);                       ///< 关闭设备
    int (*transmit)(struct netdev *dev, netbuf_t *buf);     ///< 发送数据包
    int (*set_mac)(struct netdev *dev, uint8_t *mac);       ///< 设置 MAC 地址
} netdev_ops_t;

/**
 * @brief 网络设备结构
 */
typedef struct netdev {
    char name[NETDEV_NAME_LEN];     ///< 设备名称（如 "eth0"）
    uint8_t mac[MAC_ADDR_LEN];      ///< MAC 地址
    uint32_t ip_addr;               ///< IPv4 地址（网络字节序）
    uint32_t netmask;               ///< 子网掩码（网络字节序）
    uint32_t gateway;               ///< 默认网关（网络字节序）
    
    netdev_state_t state;           ///< 设备状态
    uint16_t mtu;                   ///< 最大传输单元
    
    // 统计信息
    uint64_t rx_packets;            ///< 接收数据包数
    uint64_t tx_packets;            ///< 发送数据包数
    uint64_t rx_bytes;              ///< 接收字节数
    uint64_t tx_bytes;              ///< 发送字节数
    uint64_t rx_errors;             ///< 接收错误数
    uint64_t tx_errors;             ///< 发送错误数
    uint64_t rx_dropped;            ///< 接收丢弃数
    uint64_t tx_dropped;            ///< 发送丢弃数
    
    netdev_ops_t *ops;              ///< 设备操作函数
    void *priv;                     ///< 驱动私有数据
    
    mutex_t lock;                   ///< 设备锁
} netdev_t;

/**
 * @brief 初始化网络设备子系统
 */
void netdev_init(void);

/**
 * @brief 注册网络设备
 * @param dev 设备结构
 * @return 0 成功，-1 失败
 */
int netdev_register(netdev_t *dev);

/**
 * @brief 注销网络设备
 * @param dev 设备结构
 * @return 0 成功，-1 失败
 */
int netdev_unregister(netdev_t *dev);

/**
 * @brief 分配新的网络设备结构
 * @param name 设备名称前缀（如 "eth"）
 * @return 新设备指针，失败返回 NULL
 */
netdev_t *netdev_alloc(const char *name);

/**
 * @brief 释放网络设备结构
 * @param dev 设备指针
 */
void netdev_free(netdev_t *dev);

/**
 * @brief 通过名称查找网络设备
 * @param name 设备名称
 * @return 设备指针，未找到返回 NULL
 */
netdev_t *netdev_get_by_name(const char *name);

/**
 * @brief 获取默认网络设备
 * @return 默认设备指针，没有则返回 NULL
 */
netdev_t *netdev_get_default(void);

/**
 * @brief 设置默认网络设备
 * @param dev 设备指针
 */
void netdev_set_default(netdev_t *dev);

/**
 * @brief 启用网络设备
 * @param dev 设备结构
 * @return 0 成功，-1 失败
 */
int netdev_up(netdev_t *dev);

/**
 * @brief 禁用网络设备
 * @param dev 设备结构
 * @return 0 成功，-1 失败
 */
int netdev_down(netdev_t *dev);

/**
 * @brief 发送数据包
 * @param dev 设备结构
 * @param buf 网络缓冲区
 * @return 0 成功，-1 失败
 */
int netdev_transmit(netdev_t *dev, netbuf_t *buf);

/**
 * @brief 接收数据包（由驱动调用）
 * @param dev 设备结构
 * @param buf 网络缓冲区
 */
void netdev_receive(netdev_t *dev, netbuf_t *buf);

/**
 * @brief 配置网络设备 IP 地址
 * @param dev 设备结构
 * @param ip IP 地址（网络字节序）
 * @param netmask 子网掩码（网络字节序）
 * @param gateway 默认网关（网络字节序）
 * @return 0 成功，-1 失败
 */
int netdev_set_ip(netdev_t *dev, uint32_t ip, uint32_t netmask, uint32_t gateway);

/**
 * @brief 获取所有网络设备列表
 * @param devs 设备指针数组
 * @param max_count 数组最大容量
 * @return 实际设备数量
 */
int netdev_get_all(netdev_t **devs, int max_count);

/**
 * @brief 打印网络设备信息
 * @param dev 设备指针
 */
void netdev_print_info(netdev_t *dev);

/**
 * @brief 打印所有网络设备信息
 */
void netdev_print_all(void);

#endif // _NET_NETDEV_H_

