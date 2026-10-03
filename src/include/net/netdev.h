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
namespace net { struct Netdev; }

typedef struct netdev_ops {
    int (*open)(net::Netdev *dev);                        ///< 打开设备
    int (*close)(net::Netdev *dev);                       ///< 关闭设备
    int (*transmit)(net::Netdev *dev, net::Netbuf *buf);     ///< 发送数据包
    int (*set_mac)(net::Netdev *dev, uint8_t *mac);       ///< 设置 MAC 地址
} netdev_ops_t;

namespace net {

/**
 * @brief 网络设备结构
 */
struct Netdev {
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
    
    sync::Mutex lock;                   ///< 设备锁

    /**
     * @brief 初始化网络设备子系统
     */
    static void init();

    /**
     * @brief 注册网络设备
     * @param dev 设备结构
     * @return 0 成功，-1 失败
     */
    static int register_device(Netdev *dev);

    /**
     * @brief 注销网络设备
     * @param dev 设备结构
     * @return 0 成功，-1 失败
     */
    static int unregister_device(Netdev *dev);

    /**
     * @brief 分配新的网络设备结构
     * @param name 设备名称前缀（如 "eth"）
     * @return 新设备指针，失败返回 NULL
     */
    static Netdev *alloc(const char *name);

    /**
     * @brief 释放网络设备结构
     * @param dev 设备指针
     */
    static void free(Netdev *dev);

    /**
     * @brief 通过名称查找网络设备
     * @param name 设备名称
     * @return 设备指针，未找到返回 NULL
     */
    static Netdev *get_by_name(const char *name);

    /**
     * @brief 获取默认网络设备
     * @return 默认设备指针，没有则返回 NULL
     */
    static Netdev *get_default();

    /**
     * @brief 设置默认网络设备
     * @param dev 设备指针
     */
    static void set_default(Netdev *dev);

    /**
     * @brief 启用网络设备
     * @param dev 设备结构
     * @return 0 成功，-1 失败
     */
    static int up(Netdev *dev);

    /**
     * @brief 禁用网络设备
     * @param dev 设备结构
     * @return 0 成功，-1 失败
     */
    static int down(Netdev *dev);

    /**
     * @brief 发送数据包
     * @param dev 设备结构
     * @param buf 网络缓冲区
     * @return 0 成功，-1 失败
     */
    static int transmit(Netdev *dev, net::Netbuf *buf);

    /**
     * @brief 接收数据包（由驱动调用）
     * @param dev 设备结构
     * @param buf 网络缓冲区
     */
    static void receive(Netdev *dev, net::Netbuf *buf);

    /**
     * @brief 设置网络设备 IP 地址
     * @param dev 设备结构
     * @param ip IP 地址（网络字节序）
     */
    static void set_ipaddr(Netdev *dev, uint32_t ip);

    /**
     * @brief 设置网络设备子网掩码
     * @param dev 设备结构
     * @param netmask 子网掩码（网络字节序）
     */
    static void set_netmask(Netdev *dev, uint32_t netmask);

    /**
     * @brief 设置网络设备默认网关
     * @param dev 设备结构
     * @param gateway 网关地址（网络字节序）
     */
    static void set_gateway(Netdev *dev, uint32_t gateway);

    /**
     * @brief 获取所有网络设备列表
     * @param devs 设备指针数组
     * @param max_count 数组最大容量
     * @return 实际设备数量
     */
    static int get_all(Netdev **devs, int max_count);

    /**
     * @brief 打印网络设备信息
     * @param dev 设备指针
     */
    static void print_info(Netdev *dev);

    /**
     * @brief 打印所有网络设备信息
     */
    static void print_all();
};

} // namespace net

#endif // _NET_NETDEV_H_
