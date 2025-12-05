/**
 * @file dns.h
 * @brief DNS 解析器定义
 * 
 * 实现 RFC 1035 DNS 协议客户端功能
 */

#ifndef _NET_DNS_H_
#define _NET_DNS_H_

#include <types.h>

// ============================================================================
// DNS 常量定义
// ============================================================================

#define DNS_PORT                53      // DNS 服务器端口
#define DNS_MAX_NAME_LEN        256     // 最大域名长度
#define DNS_MAX_PACKET_SIZE     512     // 最大 DNS 包大小（UDP）
#define DNS_CACHE_SIZE          16      // DNS 缓存大小
#define DNS_CACHE_TTL           300000  // 缓存 TTL（5分钟，毫秒）
#define DNS_QUERY_TIMEOUT       5000    // 查询超时（5秒）
#define DNS_MAX_RETRIES         3       // 最大重试次数

// DNS 头部标志
#define DNS_FLAG_QR             0x8000  // 查询/响应标志
#define DNS_FLAG_OPCODE_MASK    0x7800  // 操作码掩码
#define DNS_FLAG_AA             0x0400  // 权威应答
#define DNS_FLAG_TC             0x0200  // 截断
#define DNS_FLAG_RD             0x0100  // 期望递归
#define DNS_FLAG_RA             0x0080  // 可用递归
#define DNS_FLAG_RCODE_MASK     0x000F  // 响应码掩码

// DNS 响应码
#define DNS_RCODE_NOERROR       0       // 无错误
#define DNS_RCODE_FORMERR       1       // 格式错误
#define DNS_RCODE_SERVFAIL      2       // 服务器失败
#define DNS_RCODE_NXDOMAIN      3       // 域名不存在
#define DNS_RCODE_NOTIMP        4       // 未实现
#define DNS_RCODE_REFUSED       5       // 拒绝

// DNS 记录类型
#define DNS_TYPE_A              1       // IPv4 地址
#define DNS_TYPE_NS             2       // 域名服务器
#define DNS_TYPE_CNAME          5       // 别名
#define DNS_TYPE_SOA            6       // 授权起始
#define DNS_TYPE_PTR            12      // 指针记录
#define DNS_TYPE_MX             15      // 邮件交换
#define DNS_TYPE_TXT            16      // 文本
#define DNS_TYPE_AAAA           28      // IPv6 地址

// DNS 记录类
#define DNS_CLASS_IN            1       // Internet

// ============================================================================
// DNS 数据结构
// ============================================================================

/**
 * DNS 头部
 */
typedef struct __attribute__((packed)) {
    uint16_t id;            // 标识
    uint16_t flags;         // 标志
    uint16_t qdcount;       // 问题数
    uint16_t ancount;       // 回答数
    uint16_t nscount;       // 授权记录数
    uint16_t arcount;       // 附加记录数
} dns_header_t;

/**
 * DNS 问题记录（固定部分）
 */
typedef struct __attribute__((packed)) {
    uint16_t qtype;         // 查询类型
    uint16_t qclass;        // 查询类
} dns_question_t;

/**
 * DNS 资源记录（固定部分）
 */
typedef struct __attribute__((packed)) {
    uint16_t type;          // 类型
    uint16_t class;         // 类
    uint32_t ttl;           // 生存时间
    uint16_t rdlength;      // 数据长度
} dns_rr_fixed_t;

/**
 * DNS 缓存条目
 */
typedef struct {
    char name[DNS_MAX_NAME_LEN];    // 域名
    uint32_t ip;                     // IP 地址
    uint32_t expire_time;            // 过期时间（系统 tick）
    bool valid;                      // 是否有效
} dns_cache_entry_t;

// ============================================================================
// DNS 函数接口
// ============================================================================

/**
 * @brief 初始化 DNS 解析器
 */
void dns_init(void);

/**
 * @brief 设置 DNS 服务器
 * @param primary 主 DNS 服务器 IP
 * @param secondary 备用 DNS 服务器 IP（0 表示不使用）
 */
void dns_set_server(uint32_t primary, uint32_t secondary);

/**
 * @brief 获取当前 DNS 服务器
 * @param primary 输出主 DNS 服务器 IP
 * @param secondary 输出备用 DNS 服务器 IP
 */
void dns_get_server(uint32_t *primary, uint32_t *secondary);

/**
 * @brief 解析域名（同步）
 * @param hostname 域名字符串
 * @param ip 输出 IP 地址
 * @return 0 成功，-1 失败
 * 
 * 注意：这是一个阻塞调用，会等待 DNS 响应或超时
 */
int dns_resolve(const char *hostname, uint32_t *ip);

/**
 * @brief 解析域名（从缓存）
 * @param hostname 域名字符串
 * @param ip 输出 IP 地址
 * @return 0 成功（缓存命中），-1 失败（需要查询）
 */
int dns_cache_lookup(const char *hostname, uint32_t *ip);

/**
 * @brief 添加 DNS 缓存条目
 * @param hostname 域名
 * @param ip IP 地址
 * @param ttl 生存时间（秒）
 */
void dns_cache_add(const char *hostname, uint32_t ip, uint32_t ttl);

/**
 * @brief 清除 DNS 缓存
 */
void dns_cache_clear(void);

/**
 * @brief 打印 DNS 缓存内容
 * @param buf 输出缓冲区，NULL 则打印到控制台
 * @param size 缓冲区大小
 * @return 输出的字节数
 */
int dns_cache_dump(char *buf, size_t size);

/**
 * @brief 将 IP 地址解析为域名（反向解析）
 * @param ip IP 地址
 * @param hostname 输出域名
 * @param hostname_len 域名缓冲区长度
 * @return 0 成功，-1 失败
 * 
 * 注意：当前未实现
 */
int dns_reverse_resolve(uint32_t ip, char *hostname, size_t hostname_len);

#endif // _NET_DNS_H_

