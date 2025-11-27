# 阶段 13: 网络栈

## 概述

本阶段将实现 CastorOS 的网络协议栈，这是操作系统连接互联网世界的关键一步。我们将构建一个简洁但功能完整的 TCP/IP 协议栈，为未来的网络应用（如 Web 服务器、SSH 客户端）奠定基础。

**📝 设计理念**：

本阶段采用**分层网络架构**，遵循 OSI/TCP-IP 模型：

✅ **网络设备抽象层（netdev）**
   - 统一的网卡驱动接口
   - 支持多网卡管理
   - 数据包缓冲队列

✅ **数据链路层**
   - 以太网帧处理
   - MAC 地址管理
   - 帧校验

✅ **网络层**
   - ARP 协议（地址解析）
   - IPv4 协议
   - ICMP 协议（ping）

✅ **传输层**
   - UDP 协议（无连接）
   - TCP 协议（面向连接）

✅ **Socket API**
   - BSD Socket 接口
   - 支持用户态网络编程

这种分层架构保证了各层的独立性，便于调试和扩展。

---

## 目标

- [ ] 设计并实现网络设备抽象层（netdev）
- [ ] 实现以太网帧的收发处理
- [ ] 实现 ARP 协议（IP 到 MAC 地址解析）
- [ ] 实现 IPv4 协议（基础收发、分片重组）
- [ ] 实现 ICMP 协议（支持 ping）
- [ ] 实现 UDP 协议
- [ ] 实现 TCP 协议（三次握手、可靠传输、四次挥手）
- [ ] 实现 BSD Socket API
- [ ] 添加网络相关的 shell 命令（ifconfig、ping）
- [ ] 在 QEMU 中测试网络功能

---

## 技术背景

### 网络协议栈架构

TCP/IP 协议栈的分层结构：

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│                  (HTTP, DNS, DHCP, etc.)                     │
├─────────────────────────────────────────────────────────────┤
│                       Socket API                             │
│             (socket, bind, listen, connect, ...)             │
├─────────────────────────────────────────────────────────────┤
│                    Transport Layer                           │
│                     TCP / UDP                                │
├─────────────────────────────────────────────────────────────┤
│                     Network Layer                            │
│                   IPv4 / ICMP / ARP                          │
├─────────────────────────────────────────────────────────────┤
│                   Data Link Layer                            │
│                   Ethernet Frame                             │
├─────────────────────────────────────────────────────────────┤
│                   Physical Layer                             │
│               Network Device (NIC)                           │
└─────────────────────────────────────────────────────────────┘
```

**数据包封装流程**：
```
应用数据
   ↓ 添加 TCP/UDP 头
[TCP Header][Application Data]
   ↓ 添加 IP 头
[IP Header][TCP Header][Application Data]
   ↓ 添加以太网帧头和尾
[Eth Header][IP Header][TCP Header][Application Data][FCS]
   ↓
发送到网卡
```

### 以太网帧结构

**Ethernet II 帧格式**（最常用）：

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Destination MAC (6 bytes)                  |
+                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                               |                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               +
|                      Source MAC (6 bytes)                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          EtherType            |                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               +
|                                                               |
+                        Payload (46-1500 bytes)                +
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       FCS (4 bytes)                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

总长度: 14 + 46~1500 + 4 = 64~1518 bytes
```

**常用 EtherType 值**：
| 值 | 协议 |
|------|------|
| 0x0800 | IPv4 |
| 0x0806 | ARP |
| 0x86DD | IPv6 |

### ARP 协议

**ARP（Address Resolution Protocol）** 用于将 IP 地址解析为 MAC 地址。

**ARP 报文格式**：
```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|         Hardware Type         |         Protocol Type         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| HW Addr Len   | Proto Addr Len|          Operation            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Sender Hardware Address                    |
+                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                               |                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               +
|                    Sender Protocol Address                    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Target Hardware Address                    |
+                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                               |                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               +
|                    Target Protocol Address                    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**ARP 操作码**：
- 1：ARP 请求（Request）
- 2：ARP 应答（Reply）

**ARP 工作流程**：
```
主机 A (10.0.0.1)                     主机 B (10.0.0.2)
      |                                    |
      |  ARP Request (Who has 10.0.0.2?)   |
      |----------------------------------->|  (广播)
      |                                    |
      |  ARP Reply (10.0.0.2 is at MAC-B)  |
      |<-----------------------------------|  (单播)
      |                                    |
      |  [缓存 ARP 条目]                   |
```

### IPv4 协议

**IPv4 头部格式**：
```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|Version|  IHL  |Type of Service|          Total Length         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|         Identification        |Flags|      Fragment Offset    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Time to Live |    Protocol   |         Header Checksum       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Source Address                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Destination Address                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Options (variable)                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**重要字段说明**：
| 字段 | 大小 | 说明 |
|------|------|------|
| Version | 4 bits | IP 版本（IPv4 = 4）|
| IHL | 4 bits | 头部长度（单位：4 字节）|
| Total Length | 16 bits | 整个 IP 包长度 |
| TTL | 8 bits | 生存时间（每经过路由器减 1）|
| Protocol | 8 bits | 上层协议（ICMP=1, TCP=6, UDP=17）|
| Checksum | 16 bits | 头部校验和 |

### ICMP 协议

**ICMP（Internet Control Message Protocol）** 用于网络诊断和错误报告。

**ICMP 头部格式**：
```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Type      |     Code      |          Checksum             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Rest of Header                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**常用 ICMP 类型**：
| Type | Code | 说明 |
|------|------|------|
| 0 | 0 | Echo Reply（ping 响应）|
| 3 | * | Destination Unreachable |
| 8 | 0 | Echo Request（ping 请求）|
| 11 | * | Time Exceeded |

**Ping 工作流程**：
```
ICMP Echo Request (Type=8)
       ↓
[IP Header][ICMP Header][Identifier][Sequence][Data]
       ↓
目标主机收到后返回
       ↓
ICMP Echo Reply (Type=0)
```

### UDP 协议

**UDP（User Datagram Protocol）** 是无连接的传输层协议。

**UDP 头部格式**：
```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Source Port          |       Destination Port        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|            Length             |           Checksum            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**UDP 特点**：
- 无连接：不需要建立连接
- 不可靠：不保证送达、不保证顺序
- 简单高效：头部开销小（8 字节）
- 适用场景：DNS、DHCP、视频流、游戏

### TCP 协议

**TCP（Transmission Control Protocol）** 是面向连接的可靠传输协议。

**TCP 头部格式**：
```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Source Port          |       Destination Port        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Sequence Number                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Acknowledgment Number                      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Data |       |C|E|U|A|P|R|S|F|                               |
| Offset| Rsrvd |W|C|R|C|S|S|Y|I|            Window             |
|       |       |R|E|G|K|H|T|N|N|                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           Checksum            |         Urgent Pointer        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Options (variable)                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**TCP 标志位**：
| 标志 | 说明 |
|------|------|
| SYN | 同步序列号（建立连接）|
| ACK | 确认字段有效 |
| FIN | 发送方完成发送（关闭连接）|
| RST | 重置连接 |
| PSH | 推送数据给应用 |
| URG | 紧急指针有效 |

**TCP 三次握手**：
```
Client                                    Server
   |          SYN (seq=x)                   |
   |--------------------------------------->|
   |                                        |
   |       SYN+ACK (seq=y, ack=x+1)         |
   |<---------------------------------------|
   |                                        |
   |         ACK (ack=y+1)                  |
   |--------------------------------------->|
   |                                        |
   |         [连接建立完成]                 |
```

**TCP 四次挥手**：
```
Client                                    Server
   |          FIN (seq=u)                   |
   |--------------------------------------->|
   |                                        |
   |         ACK (ack=u+1)                  |
   |<---------------------------------------|
   |                                        |
   |          FIN (seq=v)                   |
   |<---------------------------------------|
   |                                        |
   |         ACK (ack=v+1)                  |
   |--------------------------------------->|
   |                                        |
   |         [连接关闭完成]                 |
```

**TCP 状态机**：
```
                              +---------+
                              |  CLOSED |
                              +---------+
                                   |
            主动打开 ─────────────┼───────────── 被动打开
                   |              |              |
                   ↓              |              ↓
            +-----------+        |        +---------+
            |  SYN_SENT |        |        |  LISTEN |
            +-----------+        |        +---------+
                   |              |              |
         收到 SYN+ACK            |         收到 SYN
           发送 ACK              |          发送 SYN+ACK
                   |              |              |
                   ↓              |              ↓
            +-------------+      |      +-------------+
            | ESTABLISHED |<─────+─────>| ESTABLISHED |
            +-------------+             +-------------+
                   |
          主动关闭 |
           发送 FIN|
                   ↓
            +-----------+
            |  FIN_WAIT1|
            +-----------+
                   |
              收到 ACK
                   |
                   ↓
            +-----------+
            |  FIN_WAIT2|
            +-----------+
                   |
              收到 FIN
              发送 ACK
                   ↓
            +-----------+
            | TIME_WAIT |
            +-----------+
                   |
              2MSL 超时
                   ↓
            +---------+
            |  CLOSED |
            +---------+
```

### BSD Socket API

**Socket** 是应用程序访问网络的编程接口。

**核心 API**：
```c
// 创建 socket
int socket(int domain, int type, int protocol);

// 绑定地址
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

// 监听连接（TCP 服务端）
int listen(int sockfd, int backlog);

// 接受连接（TCP 服务端）
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

// 发起连接（TCP 客户端）
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

// 发送数据
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);

// 接收数据
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);

// 关闭 socket
int close(int sockfd);
```

**Socket 类型**：
| 类型 | 说明 |
|------|------|
| SOCK_STREAM | 面向连接的可靠传输（TCP）|
| SOCK_DGRAM | 无连接的数据报（UDP）|
| SOCK_RAW | 原始套接字（直接访问 IP 层）|

---

## 实现设计

### 1. 文件结构规划

```
src/
├── include/
│   └── net/
│       ├── netdev.h      # 网络设备抽象
│       ├── ethernet.h    # 以太网帧处理
│       ├── arp.h         # ARP 协议
│       ├── ip.h          # IPv4 协议
│       ├── icmp.h        # ICMP 协议
│       ├── udp.h         # UDP 协议
│       ├── tcp.h         # TCP 协议
│       ├── socket.h      # Socket API
│       ├── netbuf.h      # 网络缓冲区
│       └── checksum.h    # 校验和计算
└── net/
    ├── netdev.c
    ├── ethernet.c
    ├── arp.c
    ├── ip.c
    ├── icmp.c
    ├── udp.c
    ├── tcp.c
    ├── socket.c
    ├── netbuf.c
    └── checksum.c
```

### 2. 网络缓冲区（netbuf）

网络缓冲区是网络栈的基础数据结构，用于高效管理数据包内存。

**文件**: `src/include/net/netbuf.h`

```c
#ifndef _NET_NETBUF_H_
#define _NET_NETBUF_H_

#include <types.h>

/**
 * 网络缓冲区（类似 Linux 的 sk_buff）
 * 
 * 数据包结构:
 * +------------------+
 * |   headroom       |  <- 用于添加协议头
 * +------------------+
 * |   data           |  <- 实际数据
 * +------------------+
 * |   tailroom       |  <- 预留空间
 * +------------------+
 */

#define NETBUF_MAX_SIZE     2048    // 最大缓冲区大小
#define NETBUF_HEADROOM     128     // 预留头部空间（用于协议头）

typedef struct netbuf {
    uint8_t *head;          // 缓冲区起始地址
    uint8_t *data;          // 数据起始地址
    uint8_t *tail;          // 数据结束地址
    uint8_t *end;           // 缓冲区结束地址
    
    uint32_t len;           // 数据长度
    uint32_t total_size;    // 缓冲区总大小
    
    // 协议相关指针（用于快速访问各层头部）
    void *mac_header;       // 链路层头部
    void *network_header;   // 网络层头部
    void *transport_header; // 传输层头部
    
    // 接收信息
    struct netdev *dev;     // 接收数据包的网络设备
    
    struct netbuf *next;    // 链表指针（用于队列）
} netbuf_t;

/**
 * 分配网络缓冲区
 * @param size 数据区大小
 * @return 新分配的缓冲区，失败返回 NULL
 */
netbuf_t *netbuf_alloc(uint32_t size);

/**
 * 释放网络缓冲区
 * @param buf 缓冲区
 */
void netbuf_free(netbuf_t *buf);

/**
 * 在数据前添加空间（用于添加协议头）
 * @param buf 缓冲区
 * @param len 要添加的长度
 * @return 新的 data 指针，失败返回 NULL
 */
uint8_t *netbuf_push(netbuf_t *buf, uint32_t len);

/**
 * 从数据前移除空间（用于剥离协议头）
 * @param buf 缓冲区
 * @param len 要移除的长度
 * @return 新的 data 指针
 */
uint8_t *netbuf_pull(netbuf_t *buf, uint32_t len);

/**
 * 在数据后添加空间
 * @param buf 缓冲区
 * @param len 要添加的长度
 * @return 旧的 tail 指针，失败返回 NULL
 */
uint8_t *netbuf_put(netbuf_t *buf, uint32_t len);

/**
 * 复制缓冲区
 * @param buf 源缓冲区
 * @return 新缓冲区的副本
 */
netbuf_t *netbuf_clone(netbuf_t *buf);

#endif // _NET_NETBUF_H_
```

**实现**: `src/net/netbuf.c`

```c
#include <net/netbuf.h>
#include <mm/heap.h>
#include <lib/string.h>

netbuf_t *netbuf_alloc(uint32_t size) {
    uint32_t total_size = NETBUF_HEADROOM + size;
    if (total_size > NETBUF_MAX_SIZE) {
        total_size = NETBUF_MAX_SIZE;
    }
    
    netbuf_t *buf = (netbuf_t *)kmalloc(sizeof(netbuf_t));
    if (!buf) {
        return NULL;
    }
    
    buf->head = (uint8_t *)kmalloc(total_size);
    if (!buf->head) {
        kfree(buf);
        return NULL;
    }
    
    memset(buf->head, 0, total_size);
    
    buf->data = buf->head + NETBUF_HEADROOM;
    buf->tail = buf->data;
    buf->end = buf->head + total_size;
    buf->len = 0;
    buf->total_size = total_size;
    
    buf->mac_header = NULL;
    buf->network_header = NULL;
    buf->transport_header = NULL;
    buf->dev = NULL;
    buf->next = NULL;
    
    return buf;
}

void netbuf_free(netbuf_t *buf) {
    if (buf) {
        if (buf->head) {
            kfree(buf->head);
        }
        kfree(buf);
    }
}

uint8_t *netbuf_push(netbuf_t *buf, uint32_t len) {
    if (buf->data - buf->head < len) {
        return NULL;  // 没有足够的 headroom
    }
    buf->data -= len;
    buf->len += len;
    return buf->data;
}

uint8_t *netbuf_pull(netbuf_t *buf, uint32_t len) {
    if (buf->len < len) {
        return NULL;
    }
    buf->data += len;
    buf->len -= len;
    return buf->data;
}

uint8_t *netbuf_put(netbuf_t *buf, uint32_t len) {
    if (buf->end - buf->tail < len) {
        return NULL;  // 没有足够的 tailroom
    }
    uint8_t *old_tail = buf->tail;
    buf->tail += len;
    buf->len += len;
    return old_tail;
}

netbuf_t *netbuf_clone(netbuf_t *buf) {
    netbuf_t *new_buf = netbuf_alloc(buf->len);
    if (!new_buf) {
        return NULL;
    }
    
    memcpy(new_buf->data, buf->data, buf->len);
    new_buf->tail = new_buf->data + buf->len;
    new_buf->len = buf->len;
    
    return new_buf;
}
```

### 3. 网络设备抽象层（netdev）

**文件**: `src/include/net/netdev.h`

```c
#ifndef _NET_NETDEV_H_
#define _NET_NETDEV_H_

#include <types.h>
#include <net/netbuf.h>
#include <kernel/sync/mutex.h>

#define NETDEV_NAME_LEN     16
#define MAC_ADDR_LEN        6
#define MAX_NETDEV          4

/**
 * 网络设备状态
 */
typedef enum {
    NETDEV_DOWN,        // 设备未启用
    NETDEV_UP,          // 设备已启用
} netdev_state_t;

/**
 * 网络设备操作函数
 */
struct netdev;

typedef struct netdev_ops {
    int (*open)(struct netdev *dev);            // 打开设备
    int (*close)(struct netdev *dev);           // 关闭设备
    int (*transmit)(struct netdev *dev, netbuf_t *buf);  // 发送数据包
    int (*set_mac)(struct netdev *dev, uint8_t *mac);    // 设置 MAC 地址
} netdev_ops_t;

/**
 * 网络设备结构
 */
typedef struct netdev {
    char name[NETDEV_NAME_LEN];     // 设备名称（如 "eth0"）
    uint8_t mac[MAC_ADDR_LEN];      // MAC 地址
    uint32_t ip_addr;               // IPv4 地址
    uint32_t netmask;               // 子网掩码
    uint32_t gateway;               // 默认网关
    
    netdev_state_t state;           // 设备状态
    uint16_t mtu;                   // 最大传输单元
    
    // 统计信息
    uint64_t rx_packets;            // 接收数据包数
    uint64_t tx_packets;            // 发送数据包数
    uint64_t rx_bytes;              // 接收字节数
    uint64_t tx_bytes;              // 发送字节数
    uint64_t rx_errors;             // 接收错误数
    uint64_t tx_errors;             // 发送错误数
    
    netdev_ops_t *ops;              // 设备操作函数
    void *priv;                     // 驱动私有数据
    
    mutex_t lock;                   // 设备锁
} netdev_t;

/**
 * 初始化网络设备子系统
 */
void netdev_init(void);

/**
 * 注册网络设备
 * @param dev 设备结构
 * @return 0 成功，-1 失败
 */
int netdev_register(netdev_t *dev);

/**
 * 注销网络设备
 * @param dev 设备结构
 * @return 0 成功，-1 失败
 */
int netdev_unregister(netdev_t *dev);

/**
 * 通过名称查找网络设备
 * @param name 设备名称
 * @return 设备指针，未找到返回 NULL
 */
netdev_t *netdev_get_by_name(const char *name);

/**
 * 获取默认网络设备
 * @return 默认设备指针，没有则返回 NULL
 */
netdev_t *netdev_get_default(void);

/**
 * 启用网络设备
 * @param dev 设备结构
 * @return 0 成功，-1 失败
 */
int netdev_up(netdev_t *dev);

/**
 * 禁用网络设备
 * @param dev 设备结构
 * @return 0 成功，-1 失败
 */
int netdev_down(netdev_t *dev);

/**
 * 发送数据包
 * @param dev 设备结构
 * @param buf 网络缓冲区
 * @return 0 成功，-1 失败
 */
int netdev_transmit(netdev_t *dev, netbuf_t *buf);

/**
 * 接收数据包（由驱动调用）
 * @param dev 设备结构
 * @param buf 网络缓冲区
 */
void netdev_receive(netdev_t *dev, netbuf_t *buf);

/**
 * 配置网络设备 IP 地址
 * @param dev 设备结构
 * @param ip IP 地址
 * @param netmask 子网掩码
 * @param gateway 默认网关
 * @return 0 成功，-1 失败
 */
int netdev_set_ip(netdev_t *dev, uint32_t ip, uint32_t netmask, uint32_t gateway);

/**
 * 获取所有网络设备列表
 * @param devs 设备指针数组
 * @param max_count 数组最大容量
 * @return 实际设备数量
 */
int netdev_get_all(netdev_t **devs, int max_count);

#endif // _NET_NETDEV_H_
```

### 4. 以太网帧处理

**文件**: `src/include/net/ethernet.h`

```c
#ifndef _NET_ETHERNET_H_
#define _NET_ETHERNET_H_

#include <types.h>
#include <net/netbuf.h>
#include <net/netdev.h>

#define ETH_HEADER_LEN      14
#define ETH_ADDR_LEN        6
#define ETH_MTU             1500
#define ETH_MIN_FRAME_LEN   60
#define ETH_MAX_FRAME_LEN   1514

// EtherType 值
#define ETH_TYPE_IP         0x0800
#define ETH_TYPE_ARP        0x0806
#define ETH_TYPE_IPV6       0x86DD

// 广播 MAC 地址
extern const uint8_t ETH_BROADCAST_ADDR[ETH_ADDR_LEN];

/**
 * 以太网帧头部
 */
typedef struct eth_header {
    uint8_t  dst[ETH_ADDR_LEN];     // 目的 MAC 地址
    uint8_t  src[ETH_ADDR_LEN];     // 源 MAC 地址
    uint16_t type;                   // EtherType（网络字节序）
} __attribute__((packed)) eth_header_t;

/**
 * 初始化以太网层
 */
void ethernet_init(void);

/**
 * 处理接收到的以太网帧
 * @param dev 网络设备
 * @param buf 接收缓冲区
 */
void ethernet_input(netdev_t *dev, netbuf_t *buf);

/**
 * 发送以太网帧
 * @param dev 网络设备
 * @param buf 发送缓冲区（应包含上层协议数据）
 * @param dst_mac 目的 MAC 地址
 * @param type EtherType
 * @return 0 成功，-1 失败
 */
int ethernet_output(netdev_t *dev, netbuf_t *buf, uint8_t *dst_mac, uint16_t type);

/**
 * 比较两个 MAC 地址
 * @return 0 相等，非 0 不相等
 */
int mac_addr_cmp(const uint8_t *a, const uint8_t *b);

/**
 * 复制 MAC 地址
 */
void mac_addr_copy(uint8_t *dst, const uint8_t *src);

/**
 * 检查是否为广播地址
 */
bool mac_addr_is_broadcast(const uint8_t *addr);

/**
 * MAC 地址转字符串
 * @param mac MAC 地址
 * @param buf 输出缓冲区（至少 18 字节）
 * @return buf 指针
 */
char *mac_to_str(const uint8_t *mac, char *buf);

#endif // _NET_ETHERNET_H_
```

### 5. ARP 协议

**文件**: `src/include/net/arp.h`

```c
#ifndef _NET_ARP_H_
#define _NET_ARP_H_

#include <types.h>
#include <net/netbuf.h>
#include <net/netdev.h>

#define ARP_HARDWARE_ETHERNET   1
#define ARP_PROTOCOL_IP         0x0800

#define ARP_OP_REQUEST          1
#define ARP_OP_REPLY            2

#define ARP_CACHE_SIZE          32
#define ARP_CACHE_TIMEOUT       (300 * 1000)  // 5 分钟（毫秒）

/**
 * ARP 报文头部
 */
typedef struct arp_header {
    uint16_t hardware_type;     // 硬件类型（1 = 以太网）
    uint16_t protocol_type;     // 协议类型（0x0800 = IP）
    uint8_t  hardware_len;      // 硬件地址长度（6）
    uint8_t  protocol_len;      // 协议地址长度（4）
    uint16_t operation;         // 操作码（1=请求，2=应答）
    uint8_t  sender_mac[6];     // 发送方 MAC 地址
    uint32_t sender_ip;         // 发送方 IP 地址
    uint8_t  target_mac[6];     // 目标 MAC 地址
    uint32_t target_ip;         // 目标 IP 地址
} __attribute__((packed)) arp_header_t;

/**
 * ARP 缓存条目
 */
typedef struct arp_entry {
    uint32_t ip_addr;           // IP 地址
    uint8_t  mac_addr[6];       // MAC 地址
    uint32_t timestamp;         // 上次更新时间
    bool     valid;             // 条目是否有效
    bool     pending;           // 是否正在等待 ARP 响应
    netbuf_t *pending_buf;      // 等待发送的数据包
} arp_entry_t;

/**
 * 初始化 ARP 协议
 */
void arp_init(void);

/**
 * 处理接收到的 ARP 报文
 * @param dev 网络设备
 * @param buf 接收缓冲区
 */
void arp_input(netdev_t *dev, netbuf_t *buf);

/**
 * 解析 IP 地址对应的 MAC 地址
 * @param dev 网络设备
 * @param ip 目标 IP 地址
 * @param mac 输出 MAC 地址
 * @return 0 成功（mac 已填充），-1 正在解析中，-2 失败
 */
int arp_resolve(netdev_t *dev, uint32_t ip, uint8_t *mac);

/**
 * 发送 ARP 请求
 * @param dev 网络设备
 * @param target_ip 目标 IP 地址
 * @return 0 成功，-1 失败
 */
int arp_request(netdev_t *dev, uint32_t target_ip);

/**
 * 发送 ARP 应答
 * @param dev 网络设备
 * @param target_ip 目标 IP 地址
 * @param target_mac 目标 MAC 地址
 * @return 0 成功，-1 失败
 */
int arp_reply(netdev_t *dev, uint32_t target_ip, uint8_t *target_mac);

/**
 * 添加或更新 ARP 缓存条目
 * @param ip IP 地址
 * @param mac MAC 地址
 */
void arp_cache_update(uint32_t ip, uint8_t *mac);

/**
 * 查找 ARP 缓存
 * @param ip IP 地址
 * @param mac 输出 MAC 地址
 * @return 0 找到，-1 未找到
 */
int arp_cache_lookup(uint32_t ip, uint8_t *mac);

/**
 * 清理过期的 ARP 缓存条目
 */
void arp_cache_cleanup(void);

/**
 * 打印 ARP 缓存表（调试用）
 */
void arp_cache_dump(void);

#endif // _NET_ARP_H_
```

### 6. IPv4 协议

**文件**: `src/include/net/ip.h`

```c
#ifndef _NET_IP_H_
#define _NET_IP_H_

#include <types.h>
#include <net/netbuf.h>
#include <net/netdev.h>

#define IP_VERSION_4        4
#define IP_HEADER_MIN_LEN   20
#define IP_DEFAULT_TTL      64

// IP 协议号
#define IP_PROTO_ICMP       1
#define IP_PROTO_TCP        6
#define IP_PROTO_UDP        17

// IP 标志位
#define IP_FLAG_DF          0x4000  // Don't Fragment
#define IP_FLAG_MF          0x2000  // More Fragments
#define IP_FRAG_OFFSET_MASK 0x1FFF  // Fragment Offset 掩码

/**
 * IPv4 头部
 */
typedef struct ip_header {
    uint8_t  version_ihl;       // 版本 (4 bits) + 头部长度 (4 bits)
    uint8_t  tos;               // 服务类型
    uint16_t total_length;      // 总长度
    uint16_t identification;    // 标识
    uint16_t flags_fragment;    // 标志 (3 bits) + 分片偏移 (13 bits)
    uint8_t  ttl;               // 生存时间
    uint8_t  protocol;          // 上层协议
    uint16_t checksum;          // 头部校验和
    uint32_t src_addr;          // 源 IP 地址
    uint32_t dst_addr;          // 目的 IP 地址
} __attribute__((packed)) ip_header_t;

/**
 * 初始化 IP 协议
 */
void ip_init(void);

/**
 * 处理接收到的 IP 数据包
 * @param dev 网络设备
 * @param buf 接收缓冲区
 */
void ip_input(netdev_t *dev, netbuf_t *buf);

/**
 * 发送 IP 数据包
 * @param dev 网络设备（NULL 则自动选择）
 * @param buf 发送缓冲区（应包含上层协议数据）
 * @param dst_ip 目的 IP 地址
 * @param protocol 上层协议号
 * @return 0 成功，-1 失败
 */
int ip_output(netdev_t *dev, netbuf_t *buf, uint32_t dst_ip, uint8_t protocol);

/**
 * 计算 IP 头部校验和
 * @param header IP 头部指针
 * @param len 头部长度（字节）
 * @return 校验和
 */
uint16_t ip_checksum(void *header, int len);

/**
 * IP 地址转字符串
 * @param ip IP 地址（网络字节序）
 * @param buf 输出缓冲区（至少 16 字节）
 * @return buf 指针
 */
char *ip_to_str(uint32_t ip, char *buf);

/**
 * 字符串转 IP 地址
 * @param str IP 地址字符串（如 "192.168.1.1"）
 * @param ip 输出 IP 地址（网络字节序）
 * @return 0 成功，-1 失败
 */
int str_to_ip(const char *str, uint32_t *ip);

/**
 * 检查 IP 地址是否在同一子网
 * @param ip1 第一个 IP 地址
 * @param ip2 第二个 IP 地址
 * @param netmask 子网掩码
 * @return true 同一子网，false 不同子网
 */
bool ip_same_subnet(uint32_t ip1, uint32_t ip2, uint32_t netmask);

/**
 * 获取下一跳 IP 地址
 * @param dev 网络设备
 * @param dst_ip 目的 IP 地址
 * @return 下一跳 IP 地址
 */
uint32_t ip_get_next_hop(netdev_t *dev, uint32_t dst_ip);

// 字节序转换宏
#define htons(x) ((uint16_t)(((x) << 8) | (((x) >> 8) & 0xFF)))
#define ntohs(x) htons(x)
#define htonl(x) ((uint32_t)(((x) << 24) | (((x) >> 8) & 0xFF00) | \
                 (((x) << 8) & 0xFF0000) | (((x) >> 24) & 0xFF)))
#define ntohl(x) htonl(x)

#endif // _NET_IP_H_
```

### 7. ICMP 协议

**文件**: `src/include/net/icmp.h`

```c
#ifndef _NET_ICMP_H_
#define _NET_ICMP_H_

#include <types.h>
#include <net/netbuf.h>
#include <net/netdev.h>

// ICMP 类型
#define ICMP_ECHO_REPLY         0
#define ICMP_DEST_UNREACHABLE   3
#define ICMP_ECHO_REQUEST       8
#define ICMP_TIME_EXCEEDED      11

// ICMP 目的不可达代码
#define ICMP_NET_UNREACHABLE    0
#define ICMP_HOST_UNREACHABLE   1
#define ICMP_PROTO_UNREACHABLE  2
#define ICMP_PORT_UNREACHABLE   3

/**
 * ICMP 头部
 */
typedef struct icmp_header {
    uint8_t  type;              // 类型
    uint8_t  code;              // 代码
    uint16_t checksum;          // 校验和
    union {
        struct {
            uint16_t id;        // 标识符
            uint16_t sequence;  // 序列号
        } echo;
        uint32_t gateway;       // 重定向网关地址
        struct {
            uint16_t __unused;
            uint16_t mtu;       // 下一跳 MTU
        } frag;
    } un;
} __attribute__((packed)) icmp_header_t;

/**
 * 初始化 ICMP 协议
 */
void icmp_init(void);

/**
 * 处理接收到的 ICMP 报文
 * @param dev 网络设备
 * @param buf 接收缓冲区
 * @param src_ip 源 IP 地址
 */
void icmp_input(netdev_t *dev, netbuf_t *buf, uint32_t src_ip);

/**
 * 发送 ICMP Echo 请求（ping）
 * @param dst_ip 目的 IP 地址
 * @param id 标识符
 * @param seq 序列号
 * @param data 数据
 * @param len 数据长度
 * @return 0 成功，-1 失败
 */
int icmp_send_echo_request(uint32_t dst_ip, uint16_t id, uint16_t seq,
                           uint8_t *data, uint32_t len);

/**
 * 发送 ICMP Echo 应答
 * @param dst_ip 目的 IP 地址
 * @param id 标识符
 * @param seq 序列号
 * @param data 数据
 * @param len 数据长度
 * @return 0 成功，-1 失败
 */
int icmp_send_echo_reply(uint32_t dst_ip, uint16_t id, uint16_t seq,
                         uint8_t *data, uint32_t len);

/**
 * 发送 ICMP 目的不可达消息
 * @param dst_ip 目的 IP 地址
 * @param code 代码
 * @param orig_buf 原始数据包
 * @return 0 成功，-1 失败
 */
int icmp_send_dest_unreachable(uint32_t dst_ip, uint8_t code, netbuf_t *orig_buf);

#endif // _NET_ICMP_H_
```

### 8. UDP 协议

**文件**: `src/include/net/udp.h`

```c
#ifndef _NET_UDP_H_
#define _NET_UDP_H_

#include <types.h>
#include <net/netbuf.h>
#include <net/netdev.h>

#define UDP_HEADER_LEN  8

/**
 * UDP 头部
 */
typedef struct udp_header {
    uint16_t src_port;          // 源端口
    uint16_t dst_port;          // 目的端口
    uint16_t length;            // UDP 长度（头部 + 数据）
    uint16_t checksum;          // 校验和
} __attribute__((packed)) udp_header_t;

/**
 * UDP 伪首部（用于计算校验和）
 */
typedef struct udp_pseudo_header {
    uint32_t src_addr;          // 源 IP 地址
    uint32_t dst_addr;          // 目的 IP 地址
    uint8_t  zero;              // 保留
    uint8_t  protocol;          // 协议号 (17)
    uint16_t udp_length;        // UDP 长度
} __attribute__((packed)) udp_pseudo_header_t;

/**
 * 初始化 UDP 协议
 */
void udp_init(void);

/**
 * 处理接收到的 UDP 数据报
 * @param dev 网络设备
 * @param buf 接收缓冲区
 * @param src_ip 源 IP 地址
 * @param dst_ip 目的 IP 地址
 */
void udp_input(netdev_t *dev, netbuf_t *buf, uint32_t src_ip, uint32_t dst_ip);

/**
 * 发送 UDP 数据报
 * @param src_port 源端口
 * @param dst_ip 目的 IP 地址
 * @param dst_port 目的端口
 * @param data 数据
 * @param len 数据长度
 * @return 0 成功，-1 失败
 */
int udp_output(uint16_t src_port, uint32_t dst_ip, uint16_t dst_port,
               uint8_t *data, uint32_t len);

/**
 * 计算 UDP 校验和
 * @param src_ip 源 IP 地址
 * @param dst_ip 目的 IP 地址
 * @param udp UDP 头部指针
 * @param len UDP 总长度（头部 + 数据）
 * @return 校验和
 */
uint16_t udp_checksum(uint32_t src_ip, uint32_t dst_ip, udp_header_t *udp, uint16_t len);

#endif // _NET_UDP_H_
```

### 9. TCP 协议

**文件**: `src/include/net/tcp.h`

```c
#ifndef _NET_TCP_H_
#define _NET_TCP_H_

#include <types.h>
#include <net/netbuf.h>
#include <net/netdev.h>
#include <kernel/sync/mutex.h>

#define TCP_HEADER_MIN_LEN  20

// TCP 标志位
#define TCP_FLAG_FIN    0x01
#define TCP_FLAG_SYN    0x02
#define TCP_FLAG_RST    0x04
#define TCP_FLAG_PSH    0x08
#define TCP_FLAG_ACK    0x10
#define TCP_FLAG_URG    0x20

// TCP 状态
typedef enum {
    TCP_CLOSED,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT,
} tcp_state_t;

/**
 * TCP 头部
 */
typedef struct tcp_header {
    uint16_t src_port;          // 源端口
    uint16_t dst_port;          // 目的端口
    uint32_t seq_num;           // 序列号
    uint32_t ack_num;           // 确认号
    uint8_t  data_offset;       // 数据偏移 (4 bits) + 保留 (4 bits)
    uint8_t  flags;             // 标志位
    uint16_t window;            // 窗口大小
    uint16_t checksum;          // 校验和
    uint16_t urgent_ptr;        // 紧急指针
} __attribute__((packed)) tcp_header_t;

/**
 * TCP 控制块（TCB）- 表示一个 TCP 连接
 */
typedef struct tcp_pcb {
    // 连接标识
    uint32_t local_ip;          // 本地 IP 地址
    uint16_t local_port;        // 本地端口
    uint32_t remote_ip;         // 远程 IP 地址
    uint16_t remote_port;       // 远程端口
    
    tcp_state_t state;          // 连接状态
    
    // 发送序列号变量
    uint32_t snd_una;           // 已发送未确认的最小序列号
    uint32_t snd_nxt;           // 下一个要发送的序列号
    uint32_t snd_wnd;           // 发送窗口大小
    uint32_t iss;               // 初始发送序列号
    
    // 接收序列号变量
    uint32_t rcv_nxt;           // 期望接收的下一个序列号
    uint32_t rcv_wnd;           // 接收窗口大小
    uint32_t irs;               // 初始接收序列号
    
    // 重传相关
    uint32_t rto;               // 重传超时时间（毫秒）
    uint32_t retransmit_count;  // 重传次数
    
    // 缓冲区
    netbuf_t *send_buf;         // 发送缓冲区
    netbuf_t *recv_buf;         // 接收缓冲区
    
    // 同步
    mutex_t lock;
    
    // 链表指针
    struct tcp_pcb *next;
} tcp_pcb_t;

/**
 * 初始化 TCP 协议
 */
void tcp_init(void);

/**
 * 处理接收到的 TCP 段
 * @param dev 网络设备
 * @param buf 接收缓冲区
 * @param src_ip 源 IP 地址
 * @param dst_ip 目的 IP 地址
 */
void tcp_input(netdev_t *dev, netbuf_t *buf, uint32_t src_ip, uint32_t dst_ip);

/**
 * 创建新的 TCP 控制块
 * @return TCP 控制块，失败返回 NULL
 */
tcp_pcb_t *tcp_pcb_new(void);

/**
 * 释放 TCP 控制块
 * @param pcb TCP 控制块
 */
void tcp_pcb_free(tcp_pcb_t *pcb);

/**
 * 绑定本地地址和端口
 * @param pcb TCP 控制块
 * @param local_ip 本地 IP（0 表示任意）
 * @param local_port 本地端口
 * @return 0 成功，-1 失败
 */
int tcp_bind(tcp_pcb_t *pcb, uint32_t local_ip, uint16_t local_port);

/**
 * 开始监听连接
 * @param pcb TCP 控制块
 * @param backlog 等待连接队列长度
 * @return 0 成功，-1 失败
 */
int tcp_listen(tcp_pcb_t *pcb, int backlog);

/**
 * 发起连接
 * @param pcb TCP 控制块
 * @param remote_ip 远程 IP
 * @param remote_port 远程端口
 * @return 0 成功，-1 失败
 */
int tcp_connect(tcp_pcb_t *pcb, uint32_t remote_ip, uint16_t remote_port);

/**
 * 接受连接
 * @param pcb 监听 TCP 控制块
 * @return 新连接的 TCP 控制块，无连接返回 NULL
 */
tcp_pcb_t *tcp_accept(tcp_pcb_t *pcb);

/**
 * 发送数据
 * @param pcb TCP 控制块
 * @param data 数据
 * @param len 长度
 * @return 实际发送的字节数，-1 失败
 */
int tcp_send(tcp_pcb_t *pcb, void *data, uint32_t len);

/**
 * 接收数据
 * @param pcb TCP 控制块
 * @param buf 缓冲区
 * @param len 缓冲区大小
 * @return 实际接收的字节数，0 连接关闭，-1 失败
 */
int tcp_recv(tcp_pcb_t *pcb, void *buf, uint32_t len);

/**
 * 关闭连接
 * @param pcb TCP 控制块
 * @return 0 成功，-1 失败
 */
int tcp_close(tcp_pcb_t *pcb);

/**
 * 计算 TCP 校验和
 */
uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, tcp_header_t *tcp, uint16_t len);

/**
 * 获取 TCP 状态名称（调试用）
 */
const char *tcp_state_name(tcp_state_t state);

#endif // _NET_TCP_H_
```

### 10. Socket API

**文件**: `src/include/net/socket.h`

```c
#ifndef _NET_SOCKET_H_
#define _NET_SOCKET_H_

#include <types.h>

// 地址族
#define AF_INET         2       // IPv4

// Socket 类型
#define SOCK_STREAM     1       // TCP
#define SOCK_DGRAM      2       // UDP
#define SOCK_RAW        3       // Raw socket

// 协议号（通常设为 0 表示自动选择）
#define IPPROTO_IP      0
#define IPPROTO_ICMP    1
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17

// Socket 选项
#define SOL_SOCKET      1
#define SO_REUSEADDR    2
#define SO_KEEPALIVE    9
#define SO_RCVTIMEO     20
#define SO_SNDTIMEO     21

// shutdown() how 参数
#define SHUT_RD         0       // 关闭读
#define SHUT_WR         1       // 关闭写
#define SHUT_RDWR       2       // 关闭读写

/**
 * 通用 socket 地址结构
 */
struct sockaddr {
    uint16_t sa_family;         // 地址族
    char     sa_data[14];       // 地址数据
};

/**
 * IPv4 socket 地址结构
 */
struct sockaddr_in {
    uint16_t sin_family;        // AF_INET
    uint16_t sin_port;          // 端口号（网络字节序）
    uint32_t sin_addr;          // IP 地址（网络字节序）
    uint8_t  sin_zero[8];       // 填充（使大小与 sockaddr 相同）
};

typedef uint32_t socklen_t;

/**
 * 初始化 socket 子系统
 */
void socket_init(void);

/* ============================================================================
 * 内核 Socket API（供系统调用使用）
 * ============================================================================ */

/**
 * 创建 socket
 * @param domain 地址族（AF_INET）
 * @param type socket 类型（SOCK_STREAM/SOCK_DGRAM）
 * @param protocol 协议（通常为 0）
 * @return socket 描述符，-1 失败
 */
int sys_socket(int domain, int type, int protocol);

/**
 * 绑定地址
 * @param sockfd socket 描述符
 * @param addr 地址
 * @param addrlen 地址长度
 * @return 0 成功，-1 失败
 */
int sys_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

/**
 * 监听连接
 * @param sockfd socket 描述符
 * @param backlog 等待队列长度
 * @return 0 成功，-1 失败
 */
int sys_listen(int sockfd, int backlog);

/**
 * 接受连接
 * @param sockfd socket 描述符
 * @param addr 客户端地址（输出）
 * @param addrlen 地址长度（输入/输出）
 * @return 新 socket 描述符，-1 失败
 */
int sys_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * 发起连接
 * @param sockfd socket 描述符
 * @param addr 服务端地址
 * @param addrlen 地址长度
 * @return 0 成功，-1 失败
 */
int sys_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

/**
 * 发送数据
 * @param sockfd socket 描述符
 * @param buf 数据缓冲区
 * @param len 数据长度
 * @param flags 标志
 * @return 发送的字节数，-1 失败
 */
ssize_t sys_send(int sockfd, const void *buf, size_t len, int flags);

/**
 * 发送数据到指定地址（UDP）
 */
ssize_t sys_sendto(int sockfd, const void *buf, size_t len, int flags,
                   const struct sockaddr *dest_addr, socklen_t addrlen);

/**
 * 接收数据
 * @param sockfd socket 描述符
 * @param buf 数据缓冲区
 * @param len 缓冲区大小
 * @param flags 标志
 * @return 接收的字节数，0 连接关闭，-1 失败
 */
ssize_t sys_recv(int sockfd, void *buf, size_t len, int flags);

/**
 * 接收数据并获取源地址（UDP）
 */
ssize_t sys_recvfrom(int sockfd, void *buf, size_t len, int flags,
                     struct sockaddr *src_addr, socklen_t *addrlen);

/**
 * 关闭 socket
 * @param sockfd socket 描述符
 * @return 0 成功，-1 失败
 */
int sys_closesocket(int sockfd);

/**
 * 部分关闭 socket
 * @param sockfd socket 描述符
 * @param how 关闭方式（SHUT_RD/SHUT_WR/SHUT_RDWR）
 * @return 0 成功，-1 失败
 */
int sys_shutdown(int sockfd, int how);

/**
 * 设置 socket 选项
 */
int sys_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);

/**
 * 获取 socket 选项
 */
int sys_getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);

#endif // _NET_SOCKET_H_
```

### 11. 校验和计算

**文件**: `src/include/net/checksum.h`

```c
#ifndef _NET_CHECKSUM_H_
#define _NET_CHECKSUM_H_

#include <types.h>

/**
 * 计算 Internet 校验和（用于 IP、ICMP、TCP、UDP）
 * @param data 数据指针
 * @param len 数据长度
 * @return 校验和（网络字节序）
 */
uint16_t checksum(void *data, int len);

/**
 * 增量计算校验和
 * @param sum 当前累加值
 * @param data 数据指针
 * @param len 数据长度
 * @return 更新后的累加值
 */
uint32_t checksum_partial(uint32_t sum, void *data, int len);

/**
 * 完成校验和计算（折叠并取反）
 * @param sum 累加值
 * @return 最终校验和
 */
uint16_t checksum_finish(uint32_t sum);

#endif // _NET_CHECKSUM_H_
```

**实现**: `src/net/checksum.c`

```c
#include <net/checksum.h>

uint32_t checksum_partial(uint32_t sum, void *data, int len) {
    uint16_t *ptr = (uint16_t *)data;
    
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    // 处理奇数长度
    if (len == 1) {
        sum += *(uint8_t *)ptr;
    }
    
    return sum;
}

uint16_t checksum_finish(uint32_t sum) {
    // 折叠高 16 位到低 16 位
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    // 取反
    return ~sum;
}

uint16_t checksum(void *data, int len) {
    uint32_t sum = checksum_partial(0, data, len);
    return checksum_finish(sum);
}
```

---

## 系统调用

### 网络相关系统调用

| 系统调用号 | 名称 | 说明 |
|-----------|------|------|
| 0x0030 | SYS_SOCKET | 创建 socket |
| 0x0031 | SYS_BIND | 绑定地址 |
| 0x0032 | SYS_LISTEN | 监听连接 |
| 0x0033 | SYS_ACCEPT | 接受连接 |
| 0x0034 | SYS_CONNECT | 发起连接 |
| 0x0035 | SYS_SEND | 发送数据 |
| 0x0036 | SYS_RECV | 接收数据 |
| 0x0037 | SYS_SENDTO | 发送数据到指定地址 |
| 0x0038 | SYS_RECVFROM | 接收数据并获取源地址 |
| 0x0039 | SYS_SHUTDOWN | 部分关闭 socket |
| 0x003A | SYS_SETSOCKOPT | 设置 socket 选项 |
| 0x003B | SYS_GETSOCKOPT | 获取 socket 选项 |

---

## Shell 命令

### ifconfig - 网络接口配置

```
用法: ifconfig [interface] [options]

显示网络接口信息:
  ifconfig              显示所有接口
  ifconfig eth0         显示指定接口

配置接口:
  ifconfig eth0 192.168.1.100 netmask 255.255.255.0
  ifconfig eth0 up      启用接口
  ifconfig eth0 down    禁用接口

示例输出:
eth0: flags=4163<UP,BROADCAST,RUNNING>  mtu 1500
        inet 10.0.2.15  netmask 255.255.255.0  gateway 10.0.2.2
        ether 52:54:00:12:34:56
        RX packets 1234  bytes 123456
        TX packets 567   bytes 56789
```

### ping - 网络连通性测试

```
用法: ping [-c count] host

选项:
  -c count    发送 count 个数据包后停止

示例:
  ping 10.0.2.2
  ping -c 4 192.168.1.1

输出:
PING 10.0.2.2: 56 data bytes
64 bytes from 10.0.2.2: icmp_seq=1 ttl=64 time=0.5 ms
64 bytes from 10.0.2.2: icmp_seq=2 ttl=64 time=0.3 ms
--- 10.0.2.2 ping statistics ---
2 packets transmitted, 2 packets received, 0% packet loss
```

### arp - ARP 缓存管理

```
用法: arp [-a] [-d host]

选项:
  -a          显示所有 ARP 缓存条目
  -d host     删除指定条目

示例输出:
Address                  HWtype  HWaddress           Flags
10.0.2.2                 ether   52:55:0a:00:02:02   C
10.0.2.3                 ether   52:55:0a:00:02:03   C
```

---

## 测试方案

### 1. 使用 QEMU 网络

QEMU 提供多种网络模式用于测试：

**用户模式网络（最简单）**：
```bash
qemu-system-i386 -kernel castor.bin \
    -netdev user,id=net0 \
    -device e1000,netdev=net0
```

在此模式下：
- 虚拟机 IP：10.0.2.15
- 网关 IP：10.0.2.2
- DNS：10.0.2.3
- 主机可通过端口转发访问虚拟机

**TAP 模式（完全网络访问）**：
```bash
# 创建 TAP 设备
sudo ip tuntap add dev tap0 mode tap
sudo ip addr add 10.0.0.1/24 dev tap0
sudo ip link set tap0 up

# 启动 QEMU
qemu-system-i386 -kernel castor.bin \
    -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
    -device e1000,netdev=net0
```

### 2. 测试用例

**基础网络测试**：
```c
// 测试 ARP 解析
void test_arp(void) {
    uint8_t mac[6];
    int ret = arp_resolve(netdev_get_default(), gateway_ip, mac);
    assert(ret == 0);
    assert(!mac_addr_is_broadcast(mac));
}

// 测试 ping
void test_ping(void) {
    // 发送 ICMP Echo Request
    int ret = icmp_send_echo_request(gateway_ip, 1, 1, NULL, 0);
    assert(ret == 0);
    
    // 等待响应...
    // 检查 ICMP Echo Reply
}
```

**UDP 测试**：
```c
// 简单 UDP 回显测试
void test_udp_echo(void) {
    int sock = sys_socket(AF_INET, SOCK_DGRAM, 0);
    assert(sock >= 0);
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(7);  // Echo 端口
    addr.sin_addr = gateway_ip;
    
    char msg[] = "Hello, UDP!";
    ssize_t sent = sys_sendto(sock, msg, sizeof(msg), 0,
                              (struct sockaddr *)&addr, sizeof(addr));
    assert(sent == sizeof(msg));
    
    char buf[64];
    ssize_t recvd = sys_recvfrom(sock, buf, sizeof(buf), 0, NULL, NULL);
    assert(recvd == sizeof(msg));
    assert(memcmp(buf, msg, sizeof(msg)) == 0);
    
    sys_closesocket(sock);
}
```

**TCP 测试**：
```c
// TCP 连接测试
void test_tcp_connect(void) {
    int sock = sys_socket(AF_INET, SOCK_STREAM, 0);
    assert(sock >= 0);
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);  // HTTP 端口
    addr.sin_addr = some_server_ip;
    
    int ret = sys_connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    assert(ret == 0);
    
    // 发送 HTTP 请求
    char req[] = "GET / HTTP/1.0\r\n\r\n";
    ssize_t sent = sys_send(sock, req, strlen(req), 0);
    assert(sent == strlen(req));
    
    // 接收响应
    char buf[1024];
    ssize_t recvd = sys_recv(sock, buf, sizeof(buf) - 1, 0);
    assert(recvd > 0);
    buf[recvd] = '\0';
    
    // 检查 HTTP 响应
    assert(strstr(buf, "HTTP/1.") != NULL);
    
    sys_closesocket(sock);
}
```

---

## 实现顺序

建议按照以下顺序实现网络栈：

### 阶段 1: 基础设施
1. [ ] 实现 `netbuf` 网络缓冲区
2. [ ] 实现 `checksum` 校验和计算
3. [ ] 实现 `netdev` 网络设备抽象
4. [ ] 字节序转换函数

### 阶段 2: 数据链路层
5. [ ] 实现以太网帧收发
6. [ ] MAC 地址处理函数

### 阶段 3: 网络层
7. [ ] 实现 ARP 协议
8. [ ] ARP 缓存管理
9. [ ] 实现 IPv4 协议
10. [ ] IP 校验和、分片处理
11. [ ] 实现 ICMP 协议
12. [ ] `ping` 命令

### 阶段 4: 传输层
13. [ ] 实现 UDP 协议
14. [ ] 实现 TCP 协议
    - [ ] 状态机
    - [ ] 三次握手
    - [ ] 数据传输
    - [ ] 四次挥手
    - [ ] 超时重传

### 阶段 5: Socket API
15. [ ] 实现 socket 系统调用
16. [ ] 与文件描述符表集成
17. [ ] 用户态库封装

### 阶段 6: 测试与调试
18. [ ] Shell 命令（ifconfig, ping, arp）
19. [ ] QEMU 网络测试
20. [ ] 性能优化

---

## 依赖关系

本阶段需要依赖以下已实现的模块：

| 依赖模块 | 用途 |
|----------|------|
| 堆内存管理（heap） | 分配网络缓冲区和数据结构 |
| 同步机制（mutex） | 保护共享数据结构 |
| 定时器（timer） | ARP 超时、TCP 重传计时 |
| 文件描述符表（fd_table） | socket 描述符管理 |
| 系统调用框架 | 网络系统调用 |

本阶段为下一阶段提供基础：

| 后续模块 | 依赖 |
|----------|------|
| Intel E1000 网卡驱动 | netdev 接口 |
| DHCP 客户端 | UDP socket |
| DNS 解析 | UDP socket |
| HTTP 客户端/服务器 | TCP socket |

---

## 参考资料

1. **RFC 文档**
   - RFC 791: Internet Protocol (IP)
   - RFC 792: Internet Control Message Protocol (ICMP)
   - RFC 793: Transmission Control Protocol (TCP)
   - RFC 768: User Datagram Protocol (UDP)
   - RFC 826: Address Resolution Protocol (ARP)

2. **开源实现参考**
   - lwIP: 轻量级 TCP/IP 协议栈
   - Linux kernel networking
   - picoTCP

3. **书籍**
   - 《TCP/IP 详解 卷1：协议》- W. Richard Stevens
   - 《深入理解 Linux 网络技术内幕》- Christian Benvenuti

