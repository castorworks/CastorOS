/**
 * @file tcp.h
 * @brief TCP（传输控制协议）
 * 
 * 实现 RFC 793 定义的 TCP 协议，提供可靠的面向连接的数据传输。
 * 
 * TCP 头部格式:
 * +-------------------------------+-------------------------------+
 * |       Source Port             |       Destination Port        |
 * +-------------------------------+-------------------------------+
 * |                      Sequence Number                          |
 * +---------------------------------------------------------------+
 * |                   Acknowledgment Number                       |
 * +-------+-------+-+-+-+-+-+-+-+-+-------------------------------+
 * | Offset| Rsrvd |N|C|E|U|A|P|R|S|F|         Window              |
 * +-------+-------+-+-+-+-+-+-+-+-+-------------------------------+
 * |          Checksum             |       Urgent Pointer          |
 * +-------------------------------+-------------------------------+
 */

#ifndef _NET_TCP_H_
#define _NET_TCP_H_

#include <types.h>
#include <net/netbuf.h>
#include <net/netdev.h>
#include <kernel/sync/mutex.h>

#define TCP_HEADER_MIN_LEN  20      ///< TCP 最小头部长度

// TCP 标志位
#define TCP_FLAG_FIN    0x01        ///< 结束连接
#define TCP_FLAG_SYN    0x02        ///< 同步序列号
#define TCP_FLAG_RST    0x04        ///< 重置连接
#define TCP_FLAG_PSH    0x08        ///< 推送数据
#define TCP_FLAG_ACK    0x10        ///< 确认字段有效
#define TCP_FLAG_URG    0x20        ///< 紧急指针有效

// TCP 默认值
#define TCP_DEFAULT_WINDOW      4096    ///< 默认窗口大小
#define TCP_DEFAULT_MSS         1460    ///< 默认最大段大小
#define TCP_DEFAULT_RTO         1000    ///< 默认重传超时（毫秒）
#define TCP_MAX_RETRIES         5       ///< 最大重传次数
#define TCP_TIME_WAIT_TIMEOUT   60000   ///< TIME_WAIT 超时（毫秒）

/**
 * @brief TCP 序列号比较宏（处理 32 位环绕）
 * 
 * 使用有符号比较来正确处理序列号环绕。
 * 当两个序列号之差在 2^31 范围内时比较有效。
 */
#define TCP_SEQ_LT(a, b)    ((int32_t)((a) - (b)) < 0)   ///< a < b
#define TCP_SEQ_LEQ(a, b)   ((int32_t)((a) - (b)) <= 0)  ///< a <= b
#define TCP_SEQ_GT(a, b)    ((int32_t)((a) - (b)) > 0)   ///< a > b
#define TCP_SEQ_GEQ(a, b)   ((int32_t)((a) - (b)) >= 0)  ///< a >= b
#define TCP_SEQ_BETWEEN(a, s, e)  (TCP_SEQ_GEQ(a, s) && TCP_SEQ_LEQ(a, e))

/**
 * @brief TCP 状态
 */
typedef enum {
    TCP_CLOSED,             ///< 初始状态
    TCP_LISTEN,             ///< 等待连接
    TCP_SYN_SENT,           ///< 已发送 SYN
    TCP_SYN_RECEIVED,       ///< 已收到 SYN
    TCP_ESTABLISHED,        ///< 连接已建立
    TCP_FIN_WAIT_1,         ///< 已发送 FIN，等待 ACK
    TCP_FIN_WAIT_2,         ///< 已收到 FIN 的 ACK
    TCP_CLOSE_WAIT,         ///< 已收到 FIN，等待关闭
    TCP_CLOSING,            ///< 双方同时关闭
    TCP_LAST_ACK,           ///< 等待最后的 ACK
    TCP_TIME_WAIT,          ///< 等待超时
} tcp_state_t;

/**
 * @brief TCP 头部
 */
typedef struct tcp_header {
    uint16_t src_port;          ///< 源端口（网络字节序）
    uint16_t dst_port;          ///< 目的端口（网络字节序）
    uint32_t seq_num;           ///< 序列号（网络字节序）
    uint32_t ack_num;           ///< 确认号（网络字节序）
    uint8_t  data_offset;       ///< 数据偏移（高4位）+ 保留（低4位）
    uint8_t  flags;             ///< 标志位
    uint16_t window;            ///< 窗口大小（网络字节序）
    uint16_t checksum;          ///< 校验和（网络字节序）
    uint16_t urgent_ptr;        ///< 紧急指针（网络字节序）
} __attribute__((packed)) tcp_header_t;

/**
 * @brief TCP 伪首部（用于计算校验和）
 */
typedef struct tcp_pseudo_header {
    uint32_t src_addr;          ///< 源 IP 地址
    uint32_t dst_addr;          ///< 目的 IP 地址
    uint8_t  zero;              ///< 保留（0）
    uint8_t  protocol;          ///< 协议号 (6)
    uint16_t tcp_length;        ///< TCP 长度
} __attribute__((packed)) tcp_pseudo_header_t;

/**
 * @brief TCP 控制块（PCB）- 表示一个 TCP 连接
 */
typedef struct tcp_pcb {
    // 连接标识
    uint32_t local_ip;          ///< 本地 IP 地址
    uint16_t local_port;        ///< 本地端口
    uint32_t remote_ip;         ///< 远程 IP 地址
    uint16_t remote_port;       ///< 远程端口
    
    tcp_state_t state;          ///< 连接状态
    
    // 发送序列号变量
    uint32_t snd_una;           ///< 已发送未确认的最小序列号
    uint32_t snd_nxt;           ///< 下一个要发送的序列号
    uint32_t snd_wnd;           ///< 发送窗口大小
    uint32_t iss;               ///< 初始发送序列号
    
    // 接收序列号变量
    uint32_t rcv_nxt;           ///< 期望接收的下一个序列号
    uint32_t rcv_wnd;           ///< 接收窗口大小
    uint32_t irs;               ///< 初始接收序列号
    
    // MSS
    uint16_t mss;               ///< 最大段大小
    
    // 重传相关
    uint32_t rto;               ///< 重传超时时间（毫秒）
    uint32_t retransmit_count;  ///< 重传次数
    uint32_t last_send_time;    ///< 最后发送时间
    
    // 缓冲区
    uint8_t *send_buf;          ///< 发送缓冲区
    uint32_t send_buf_size;     ///< 发送缓冲区大小
    uint32_t send_len;          ///< 待发送数据长度
    
    uint8_t *recv_buf;          ///< 接收缓冲区
    uint32_t recv_buf_size;     ///< 接收缓冲区大小
    uint32_t recv_len;          ///< 已接收数据长度
    uint32_t recv_read_pos;     ///< 读取位置
    
    // 监听队列（仅用于 LISTEN 状态）
    struct tcp_pcb *accept_queue;   ///< 等待 accept 的连接队列
    struct tcp_pcb *pending_queue;  ///< 正在握手的连接队列
    int backlog;                    ///< 最大等待连接数
    int pending_count;              ///< 当前等待连接数
    struct tcp_pcb *listen_pcb;     ///< 对应的监听 PCB
    
    // 回调函数
    void (*accept_callback)(struct tcp_pcb *new_pcb, void *arg);
    void (*recv_callback)(struct tcp_pcb *pcb, void *arg);
    void (*sent_callback)(struct tcp_pcb *pcb, uint16_t len, void *arg);
    void (*error_callback)(struct tcp_pcb *pcb, int err, void *arg);
    void *callback_arg;
    
    // 同步
    mutex_t lock;
    
    // 链表指针
    struct tcp_pcb *next;
} tcp_pcb_t;

/**
 * @brief 初始化 TCP 协议
 */
void tcp_init(void);

/**
 * @brief 处理接收到的 TCP 段
 * @param dev 网络设备
 * @param buf 接收缓冲区
 * @param src_ip 源 IP 地址（网络字节序）
 * @param dst_ip 目的 IP 地址（网络字节序）
 */
void tcp_input(netdev_t *dev, netbuf_t *buf, uint32_t src_ip, uint32_t dst_ip);

/**
 * @brief 创建新的 TCP PCB
 * @return TCP PCB，失败返回 NULL
 */
tcp_pcb_t *tcp_pcb_new(void);

/**
 * @brief 释放 TCP PCB
 * @param pcb TCP PCB
 */
void tcp_pcb_free(tcp_pcb_t *pcb);

/**
 * @brief 绑定本地地址和端口
 * @param pcb TCP PCB
 * @param local_ip 本地 IP（0 表示任意）
 * @param local_port 本地端口（主机字节序）
 * @return 0 成功，-1 失败
 */
int tcp_bind(tcp_pcb_t *pcb, uint32_t local_ip, uint16_t local_port);

/**
 * @brief 开始监听连接
 * @param pcb TCP PCB
 * @param backlog 等待连接队列长度
 * @return 0 成功，-1 失败
 */
int tcp_listen(tcp_pcb_t *pcb, int backlog);

/**
 * @brief 发起连接
 * @param pcb TCP PCB
 * @param remote_ip 远程 IP（网络字节序）
 * @param remote_port 远程端口（主机字节序）
 * @return 0 成功，-1 失败
 */
int tcp_connect(tcp_pcb_t *pcb, uint32_t remote_ip, uint16_t remote_port);

/**
 * @brief 接受连接
 * @param pcb 监听 TCP PCB
 * @return 新连接的 TCP PCB，无连接返回 NULL
 */
tcp_pcb_t *tcp_accept(tcp_pcb_t *pcb);

/**
 * @brief 发送数据
 * @param pcb TCP PCB
 * @param data 数据
 * @param len 长度
 * @return 实际发送的字节数，-1 失败
 */
int tcp_write(tcp_pcb_t *pcb, const void *data, uint32_t len);

/**
 * @brief 接收数据
 * @param pcb TCP PCB
 * @param buf 缓冲区
 * @param len 缓冲区大小
 * @return 实际接收的字节数，0 连接关闭，-1 失败
 */
int tcp_read(tcp_pcb_t *pcb, void *buf, uint32_t len);

/**
 * @brief 关闭连接
 * @param pcb TCP PCB
 * @return 0 成功，-1 失败
 */
int tcp_close(tcp_pcb_t *pcb);

/**
 * @brief 中止连接（发送 RST）
 * @param pcb TCP PCB
 */
void tcp_abort(tcp_pcb_t *pcb);

/**
 * @brief 设置接受连接回调
 */
void tcp_accept_callback(tcp_pcb_t *pcb,
                         void (*callback)(tcp_pcb_t *new_pcb, void *arg),
                         void *arg);

/**
 * @brief 设置接收数据回调
 */
void tcp_recv_callback(tcp_pcb_t *pcb,
                       void (*callback)(tcp_pcb_t *pcb, void *arg),
                       void *arg);

/**
 * @brief 计算 TCP 校验和
 */
uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, tcp_header_t *tcp, uint16_t len);

/**
 * @brief 获取 TCP 状态名称（调试用）
 */
const char *tcp_state_name(tcp_state_t state);

/**
 * @brief 获取 TCP 头部长度
 */
static inline uint8_t tcp_header_len(tcp_header_t *tcp) {
    return ((tcp->data_offset >> 4) & 0x0F) * 4;
}

/**
 * @brief 分配临时端口
 */
uint16_t tcp_alloc_port(void);

/**
 * @brief TCP 定时器处理（需要定期调用）
 */
void tcp_timer(void);

#endif // _NET_TCP_H_

