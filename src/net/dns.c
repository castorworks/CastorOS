/**
 * @file dns.c
 * @brief DNS 解析器实现
 * 
 * 实现 RFC 1035 DNS 协议客户端功能
 */

#include <net/dns.h>
#include <net/udp.h>
#include <net/ip.h>
#include <net/netbuf.h>
#include <net/socket.h>
#include <kernel/sync/spinlock.h>
#include <drivers/timer.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <lib/kprintf.h>

// ============================================================================
// 内部数据结构
// ============================================================================

// DNS 服务器配置
static uint32_t dns_server_primary = 0;
static uint32_t dns_server_secondary = 0;

// DNS 缓存
static dns_cache_entry_t dns_cache[DNS_CACHE_SIZE];
static spinlock_t dns_lock;

// DNS 查询 ID
static uint16_t dns_query_id = 0;

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 生成 DNS 查询 ID
 */
static uint16_t dns_generate_id(void) {
    return ++dns_query_id;
}

/**
 * @brief 将域名编码为 DNS 格式
 * 
 * 例如: "www.example.com" -> "\3www\7example\3com\0"
 */
static int dns_encode_name(const char *name, uint8_t *buf, size_t buf_size) {
    if (!name || !buf || buf_size < 2) return -1;
    
    const char *src = name;
    uint8_t *dst = buf;
    uint8_t *end = buf + buf_size - 1;
    
    while (*src && dst < end) {
        // 找到下一个点或字符串结尾
        const char *dot = strchr(src, '.');
        size_t label_len = dot ? (size_t)(dot - src) : strlen(src);
        
        if (label_len > 63 || label_len == 0) {
            return -1;  // 标签长度无效
        }
        
        if (dst + label_len + 1 >= end) {
            return -1;  // 缓冲区不足
        }
        
        // 写入长度
        *dst++ = (uint8_t)label_len;
        
        // 写入标签
        memcpy(dst, src, label_len);
        dst += label_len;
        
        // 移动到下一个标签
        src = dot ? dot + 1 : src + label_len;
    }
    
    // 结束符
    *dst++ = 0;
    
    return (int)(dst - buf);
}

/**
 * @brief 解码 DNS 名称（处理压缩）
 */
static int dns_decode_name(const uint8_t *packet, size_t packet_len,
                           const uint8_t *ptr, char *name, size_t name_size) {
    if (!packet || !ptr || !name || name_size < 2) return -1;
    
    char *dst = name;
    char *end = name + name_size - 1;
    const uint8_t *src = ptr;
    int total_len = 0;
    bool jumped = false;
    int jumps = 0;
    const int max_jumps = 10;  // 防止无限循环
    
    while (*src && dst < end) {
        // 检查是否是压缩指针
        if ((*src & 0xC0) == 0xC0) {
            if (src + 1 >= packet + packet_len) return -1;
            
            uint16_t offset = ((*src & 0x3F) << 8) | *(src + 1);
            if (offset >= packet_len) return -1;
            
            if (!jumped) {
                total_len += 2;
            }
            
            src = packet + offset;
            jumped = true;
            
            if (++jumps > max_jumps) return -1;
            continue;
        }
        
        uint8_t len = *src++;
        if (!jumped) total_len++;
        
        if (len > 63) return -1;
        if (src + len > packet + packet_len) return -1;
        
        // 添加点分隔符
        if (dst > name) {
            if (dst >= end) return -1;
            *dst++ = '.';
        }
        
        // 复制标签
        if (dst + len > end) return -1;
        memcpy(dst, src, len);
        dst += len;
        src += len;
        
        if (!jumped) total_len += len;
    }
    
    if (!jumped) total_len++;  // 结束符
    
    *dst = '\0';
    return total_len;
}

/**
 * @brief 检查是否是有效的域名字符
 */
static bool dns_is_valid_hostname(const char *hostname) {
    if (!hostname || *hostname == '\0') return false;
    
    size_t len = strlen(hostname);
    if (len > DNS_MAX_NAME_LEN - 1) return false;
    
    // 检查是否是 IP 地址（简单检查）
    int dots = 0;
    bool all_digits = true;
    for (const char *p = hostname; *p; p++) {
        if (*p == '.') {
            dots++;
        } else if (*p < '0' || *p > '9') {
            all_digits = false;
        }
    }
    
    // 如果看起来像 IP 地址（4 个数字用 . 分隔），则不是有效的主机名
    if (dots == 3 && all_digits) {
        return false;
    }
    
    return true;
}

/**
 * @brief 解析点分十进制 IP 地址
 */
static int dns_parse_ip(const char *str, uint32_t *ip) {
    uint32_t parts[4];
    int count = 0;
    const char *p = str;
    
    while (*p && count < 4) {
        uint32_t val = 0;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            p++;
        }
        if (val > 255) return -1;
        parts[count++] = val;
        if (*p == '.') p++;
        else if (*p != '\0') return -1;
    }
    
    if (count != 4 || *p != '\0') return -1;
    
    *ip = parts[0] | (parts[1] << 8) | (parts[2] << 16) | (parts[3] << 24);
    return 0;
}

// ============================================================================
// DNS 缓存
// ============================================================================

/**
 * @brief 从缓存查找
 */
int dns_cache_lookup(const char *hostname, uint32_t *ip) {
    if (!hostname || !ip) return -1;
    
    uint32_t now = (uint32_t)timer_get_uptime_ms();
    
    bool irq_state;
    spinlock_lock_irqsave(&dns_lock, &irq_state);
    
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        dns_cache_entry_t *entry = &dns_cache[i];
        if (entry->valid && strcmp(entry->name, hostname) == 0) {
            // 检查是否过期
            if (now < entry->expire_time) {
                *ip = entry->ip;
                spinlock_unlock_irqrestore(&dns_lock, irq_state);
                return 0;
            } else {
                // 过期，标记为无效
                entry->valid = false;
            }
        }
    }
    
    spinlock_unlock_irqrestore(&dns_lock, irq_state);
    return -1;
}

/**
 * @brief 添加缓存条目
 */
void dns_cache_add(const char *hostname, uint32_t ip, uint32_t ttl) {
    if (!hostname || ip == 0) return;
    
    uint32_t now = (uint32_t)timer_get_uptime_ms();
    uint32_t expire = now + (ttl * 1000);
    if (expire < now) expire = 0xFFFFFFFF;  // 防止溢出
    
    bool irq_state;
    spinlock_lock_irqsave(&dns_lock, &irq_state);
    
    // 查找现有条目或空闲条目
    dns_cache_entry_t *target = NULL;
    dns_cache_entry_t *oldest = NULL;
    uint32_t oldest_time = 0xFFFFFFFF;
    
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        dns_cache_entry_t *entry = &dns_cache[i];
        
        if (entry->valid && strcmp(entry->name, hostname) == 0) {
            // 更新现有条目
            target = entry;
            break;
        }
        
        if (!entry->valid) {
            target = entry;
            break;
        }
        
        // 找最旧的条目
        if (entry->expire_time < oldest_time) {
            oldest_time = entry->expire_time;
            oldest = entry;
        }
    }
    
    // 如果没有空闲条目，替换最旧的
    if (!target) {
        target = oldest;
    }
    
    if (target) {
        strncpy(target->name, hostname, DNS_MAX_NAME_LEN - 1);
        target->name[DNS_MAX_NAME_LEN - 1] = '\0';
        target->ip = ip;
        target->expire_time = expire;
        target->valid = true;
    }
    
    spinlock_unlock_irqrestore(&dns_lock, irq_state);
}

/**
 * @brief 清除缓存
 */
void dns_cache_clear(void) {
    bool irq_state;
    spinlock_lock_irqsave(&dns_lock, &irq_state);
    
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        dns_cache[i].valid = false;
    }
    
    spinlock_unlock_irqrestore(&dns_lock, irq_state);
}

/**
 * @brief 打印缓存内容
 */
int dns_cache_dump(char *buf, size_t size) {
    int len = 0;
    bool to_buf = (buf != NULL && size > 0);
    
    #define OUTPUT(fmt, ...) do { \
        if (to_buf) { \
            len += ksnprintf(buf + len, size - (size_t)len, fmt, ##__VA_ARGS__); \
        } else { \
            kprintf(fmt, ##__VA_ARGS__); \
        } \
    } while(0)
    
    uint32_t now = (uint32_t)timer_get_uptime_ms();
    
    bool irq_state;
    spinlock_lock_irqsave(&dns_lock, &irq_state);
    
    OUTPUT("DNS Cache:\n");
    OUTPUT("%-32s %-16s TTL\n", "Hostname", "IP Address");
    OUTPUT("--------------------------------------------------------------------------------\n");
    
    int count = 0;
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (to_buf && len >= (int)size - 100) break;
        
        dns_cache_entry_t *entry = &dns_cache[i];
        if (!entry->valid) continue;
        
        char ip_str[16];
        ip_to_str(entry->ip, ip_str);
        
        uint32_t remaining = 0;
        if (entry->expire_time > now) {
            remaining = (entry->expire_time - now) / 1000;
        }
        
        OUTPUT("%-32s %-16s %us\n", entry->name, ip_str, remaining);
        count++;
    }
    
    if (count == 0) {
        OUTPUT("(empty)\n");
    }
    
    spinlock_unlock_irqrestore(&dns_lock, irq_state);
    
    #undef OUTPUT
    return len;
}

// ============================================================================
// DNS 查询
// ============================================================================

/**
 * @brief 发送 DNS 查询并等待响应
 */
static int dns_do_query(uint32_t server_ip, const char *hostname, uint32_t *ip) {
    if (!server_ip || !hostname || !ip) return -1;
    
    // 创建 UDP PCB
    udp_pcb_t *pcb = udp_pcb_new();
    if (!pcb) {
        LOG_ERROR_MSG("dns: Failed to create UDP PCB\n");
        return -1;
    }
    
    // 绑定到临时端口
    udp_bind(pcb, 0, 0);
    
    // 构建 DNS 查询包
    uint8_t packet[DNS_MAX_PACKET_SIZE];
    memset(packet, 0, sizeof(packet));
    
    dns_header_t *hdr = (dns_header_t *)packet;
    uint16_t query_id = dns_generate_id();
    hdr->id = htons(query_id);
    hdr->flags = htons(DNS_FLAG_RD);  // 递归查询
    hdr->qdcount = htons(1);
    
    // 编码域名
    uint8_t *qname = packet + sizeof(dns_header_t);
    int name_len = dns_encode_name(hostname, qname, sizeof(packet) - sizeof(dns_header_t));
    if (name_len < 0) {
        udp_pcb_free(pcb);
        LOG_ERROR_MSG("dns: Failed to encode hostname\n");
        return -1;
    }
    
    // 问题记录
    dns_question_t *question = (dns_question_t *)(qname + name_len);
    question->qtype = htons(DNS_TYPE_A);
    question->qclass = htons(DNS_CLASS_IN);
    
    uint32_t pkt_len = sizeof(dns_header_t) + (uint32_t)name_len + sizeof(dns_question_t);
    
    // 发送查询
    netbuf_t *buf = netbuf_alloc(pkt_len);
    if (!buf) {
        udp_pcb_free(pcb);
        return -1;
    }
    
    uint8_t *data = netbuf_put(buf, pkt_len);
    memcpy(data, packet, pkt_len);
    
    int ret = udp_sendto(pcb, buf, server_ip, DNS_PORT);
    netbuf_free(buf);
    
    if (ret < 0) {
        udp_pcb_free(pcb);
        LOG_ERROR_MSG("dns: Failed to send query\n");
        return -1;
    }
    
    // 等待响应（轮询方式，因为当前没有阻塞机制）
    uint32_t start = (uint32_t)timer_get_uptime_ms();
    int result = -1;
    
    while ((uint32_t)timer_get_uptime_ms() - start < DNS_QUERY_TIMEOUT) {
        // 检查是否收到响应
        netbuf_t *resp = udp_recv_poll(pcb);
        if (!resp) {
            // 短暂延迟
            for (volatile int i = 0; i < 10000; i++);
            continue;
        }
        
        // 解析响应
        if (resp->len < sizeof(dns_header_t)) {
            netbuf_free(resp);
            continue;
        }
        
        dns_header_t *resp_hdr = (dns_header_t *)resp->data;
        
        // 验证响应
        if (ntohs(resp_hdr->id) != query_id) {
            netbuf_free(resp);
            continue;
        }
        
        uint16_t flags = ntohs(resp_hdr->flags);
        if (!(flags & DNS_FLAG_QR)) {
            // 不是响应
            netbuf_free(resp);
            continue;
        }
        
        uint8_t rcode = flags & DNS_FLAG_RCODE_MASK;
        if (rcode != DNS_RCODE_NOERROR) {
            LOG_WARN_MSG("dns: Query failed, rcode=%d\n", rcode);
            netbuf_free(resp);
            result = -1;
            break;
        }
        
        uint16_t ancount = ntohs(resp_hdr->ancount);
        if (ancount == 0) {
            LOG_WARN_MSG("dns: No answers\n");
            netbuf_free(resp);
            result = -1;
            break;
        }
        
        // 跳过问题部分
        uint8_t *ptr = resp->data + sizeof(dns_header_t);
        uint8_t *end = resp->data + resp->len;
        
        // 跳过问题（QNAME + QTYPE + QCLASS）
        char skip_name[DNS_MAX_NAME_LEN];
        int skip_len = dns_decode_name(resp->data, resp->len, ptr, 
                                        skip_name, sizeof(skip_name));
        if (skip_len < 0) {
            netbuf_free(resp);
            continue;
        }
        ptr += skip_len + 4;  // +4 for QTYPE and QCLASS
        
        // 解析回答部分
        for (uint16_t i = 0; i < ancount && ptr < end; i++) {
            // 解析名称
            char ans_name[DNS_MAX_NAME_LEN];
            int ans_name_len = dns_decode_name(resp->data, resp->len, ptr,
                                                ans_name, sizeof(ans_name));
            if (ans_name_len < 0) break;
            ptr += ans_name_len;
            
            if (ptr + sizeof(dns_rr_fixed_t) > end) break;
            
            dns_rr_fixed_t *rr = (dns_rr_fixed_t *)ptr;
            ptr += sizeof(dns_rr_fixed_t);
            
            uint16_t rr_type = ntohs(rr->type);
            uint16_t rdlength = ntohs(rr->rdlength);
            uint32_t ttl = ntohl(rr->ttl);
            
            if (ptr + rdlength > end) break;
            
            if (rr_type == DNS_TYPE_A && rdlength == 4) {
                // 找到 A 记录
                memcpy(ip, ptr, 4);
                
                // 添加到缓存
                dns_cache_add(hostname, *ip, ttl);
                
                char ip_str[16];
                ip_to_str(*ip, ip_str);
                LOG_DEBUG_MSG("dns: Resolved %s -> %s (ttl=%u)\n", hostname, ip_str, ttl);
                
                result = 0;
                break;
            }
            
            ptr += rdlength;
        }
        
        netbuf_free(resp);
        break;
    }
    
    udp_pcb_free(pcb);
    return result;
}

// ============================================================================
// 公共接口
// ============================================================================

/**
 * @brief 初始化 DNS 解析器
 */
void dns_init(void) {
    memset(dns_cache, 0, sizeof(dns_cache));
    dns_server_primary = 0;
    dns_server_secondary = 0;
    dns_query_id = (uint16_t)(timer_get_uptime_ms() & 0xFFFF);
}

/**
 * @brief 设置 DNS 服务器
 */
void dns_set_server(uint32_t primary, uint32_t secondary) {
    dns_server_primary = primary;
    dns_server_secondary = secondary;
    
    if (primary) {
        char ip_str[16];
        ip_to_str(primary, ip_str);
        LOG_INFO_MSG("dns: Primary server set to %s\n", ip_str);
    }
}

/**
 * @brief 获取 DNS 服务器
 */
void dns_get_server(uint32_t *primary, uint32_t *secondary) {
    if (primary) *primary = dns_server_primary;
    if (secondary) *secondary = dns_server_secondary;
}

/**
 * @brief 解析域名
 */
int dns_resolve(const char *hostname, uint32_t *ip) {
    if (!hostname || !ip) return -1;
    
    // 检查是否已经是 IP 地址
    if (dns_parse_ip(hostname, ip) == 0) {
        return 0;
    }
    
    // 检查是否是有效的主机名
    if (!dns_is_valid_hostname(hostname)) {
        return -1;
    }
    
    // 先检查缓存
    if (dns_cache_lookup(hostname, ip) == 0) {
        return 0;
    }
    
    // 检查是否配置了 DNS 服务器
    if (dns_server_primary == 0) {
        LOG_WARN_MSG("dns: No DNS server configured\n");
        return -1;
    }
    
    // 尝试主 DNS 服务器
    int ret = dns_do_query(dns_server_primary, hostname, ip);
    
    // 如果失败，尝试备用服务器
    if (ret < 0 && dns_server_secondary != 0) {
        LOG_INFO_MSG("dns: Trying secondary server\n");
        ret = dns_do_query(dns_server_secondary, hostname, ip);
    }
    
    return ret;
}

/**
 * @brief 反向解析（未实现）
 */
int dns_reverse_resolve(uint32_t ip, char *hostname, size_t hostname_len) {
    (void)ip;
    (void)hostname;
    (void)hostname_len;
    LOG_WARN_MSG("dns: Reverse lookup not implemented\n");
    return -1;
}

