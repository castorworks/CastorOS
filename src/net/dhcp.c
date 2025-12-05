/**
 * @file dhcp.c
 * @brief DHCP 客户端实现
 * 
 * 实现 RFC 2131 DHCP 协议客户端功能
 */

#include <net/dhcp.h>
#include <net/udp.h>
#include <net/ip.h>
#include <net/netdev.h>
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

#define DHCP_MAX_CLIENTS    4       // 最大 DHCP 客户端数量

static dhcp_client_t dhcp_clients[DHCP_MAX_CLIENTS];
static spinlock_t dhcp_lock;

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 查找设备对应的 DHCP 客户端
 */
static dhcp_client_t *dhcp_find_client(netdev_t *dev) {
    for (int i = 0; i < DHCP_MAX_CLIENTS; i++) {
        if (dhcp_clients[i].dev == dev) {
            return &dhcp_clients[i];
        }
    }
    return NULL;
}

/**
 * @brief 分配新的 DHCP 客户端
 */
static dhcp_client_t *dhcp_alloc_client(netdev_t *dev) {
    for (int i = 0; i < DHCP_MAX_CLIENTS; i++) {
        if (dhcp_clients[i].dev == NULL) {
            memset(&dhcp_clients[i], 0, sizeof(dhcp_client_t));
            dhcp_clients[i].dev = dev;
            return &dhcp_clients[i];
        }
    }
    return NULL;
}

/**
 * @brief 生成随机事务 ID
 */
static uint32_t dhcp_generate_xid(void) {
    static uint32_t seed = 0x12345678;
    seed = seed * 1103515245 + 12345;
    return seed ^ (uint32_t)timer_get_uptime_ms();
}

/**
 * @brief 添加 DHCP 选项
 */
static uint8_t *dhcp_add_option(uint8_t *opt, uint8_t code, uint8_t len, void *data) {
    *opt++ = code;
    if (code != DHCP_OPT_END && code != DHCP_OPT_PAD) {
        *opt++ = len;
        memcpy(opt, data, len);
        opt += len;
    }
    return opt;
}

/**
 * @brief 解析 DHCP 选项
 */
static int dhcp_parse_options(uint8_t *options, uint32_t len, dhcp_info_t *info, uint8_t *msg_type) {
    uint8_t *end = options + len;
    
    while (options < end) {
        uint8_t code = *options++;
        
        if (code == DHCP_OPT_PAD) continue;
        if (code == DHCP_OPT_END) break;
        
        if (options >= end) break;
        uint8_t opt_len = *options++;
        
        if (options + opt_len > end) break;
        
        switch (code) {
            case DHCP_OPT_SUBNET_MASK:
                if (opt_len >= 4) {
                    memcpy(&info->netmask, options, 4);
                }
                break;
                
            case DHCP_OPT_ROUTER:
                if (opt_len >= 4) {
                    memcpy(&info->gateway, options, 4);
                }
                break;
                
            case DHCP_OPT_DNS:
                if (opt_len >= 4) {
                    memcpy(&info->dns_primary, options, 4);
                }
                if (opt_len >= 8) {
                    memcpy(&info->dns_secondary, options + 4, 4);
                }
                break;
                
            case DHCP_OPT_LEASE_TIME:
                if (opt_len >= 4) {
                    uint32_t lease;
                    memcpy(&lease, options, 4);
                    info->lease_time = ntohl(lease);
                }
                break;
                
            case DHCP_OPT_MSG_TYPE:
                if (opt_len >= 1 && msg_type) {
                    *msg_type = *options;
                }
                break;
                
            case DHCP_OPT_SERVER_ID:
                if (opt_len >= 4) {
                    memcpy(&info->server_ip, options, 4);
                }
                break;
                
            case DHCP_OPT_RENEWAL_TIME:
                if (opt_len >= 4) {
                    uint32_t t1;
                    memcpy(&t1, options, 4);
                    info->renewal_time = ntohl(t1);
                }
                break;
                
            case DHCP_OPT_REBIND_TIME:
                if (opt_len >= 4) {
                    uint32_t t2;
                    memcpy(&t2, options, 4);
                    info->rebind_time = ntohl(t2);
                }
                break;
        }
        
        options += opt_len;
    }
    
    return 0;
}

// ============================================================================
// DHCP 报文发送
// ============================================================================

/**
 * @brief 发送 DHCP DISCOVER 报文
 */
static int dhcp_send_discover(dhcp_client_t *client) {
    dhcp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    
    // 填充固定头部
    pkt.op = DHCP_OP_REQUEST;
    pkt.htype = DHCP_HTYPE_ETH;
    pkt.hlen = 6;
    pkt.xid = htonl(client->xid);
    pkt.flags = htons(0x8000);  // 广播标志
    pkt.magic = htonl(DHCP_MAGIC_COOKIE);
    
    // 复制 MAC 地址
    memcpy(pkt.chaddr, client->dev->mac, 6);
    
    // 添加选项
    uint8_t *opt = pkt.options;
    uint8_t msg_type = DHCP_DISCOVER;
    opt = dhcp_add_option(opt, DHCP_OPT_MSG_TYPE, 1, &msg_type);
    
    // 参数请求列表
    uint8_t params[] = {DHCP_OPT_SUBNET_MASK, DHCP_OPT_ROUTER, DHCP_OPT_DNS};
    opt = dhcp_add_option(opt, DHCP_OPT_PARAM_REQ, sizeof(params), params);
    
    // 结束
    opt = dhcp_add_option(opt, DHCP_OPT_END, 0, NULL);
    
    // 计算报文大小
    uint32_t pkt_len = sizeof(dhcp_packet_t) - sizeof(pkt.options) + 
                       (uint32_t)(opt - pkt.options);
    
    // 发送 UDP 广播
    // 使用原始 UDP 发送，源 IP 0.0.0.0，目的 IP 255.255.255.255
    udp_pcb_t *pcb = udp_pcb_new();
    if (!pcb) {
        return -1;
    }
    
    udp_bind(pcb, 0, DHCP_CLIENT_PORT);
    
    netbuf_t *buf = netbuf_alloc(pkt_len);
    if (!buf) {
        udp_pcb_free(pcb);
        return -1;
    }
    
    uint8_t *data = netbuf_put(buf, pkt_len);
    memcpy(data, &pkt, pkt_len);
    
    // 发送到广播地址
    int ret = udp_sendto(pcb, buf, 0xFFFFFFFF, DHCP_SERVER_PORT);
    
    netbuf_free(buf);
    udp_pcb_free(pcb);
    
    if (ret < 0) {
        LOG_ERROR_MSG("dhcp: Failed to send DISCOVER\n");
        return -1;
    }
    
    LOG_INFO_MSG("dhcp: Sent DISCOVER (xid=%08x)\n", client->xid);
    return 0;
}

/**
 * @brief 发送 DHCP REQUEST 报文
 */
static int dhcp_send_request(dhcp_client_t *client) {
    dhcp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    
    // 填充固定头部
    pkt.op = DHCP_OP_REQUEST;
    pkt.htype = DHCP_HTYPE_ETH;
    pkt.hlen = 6;
    pkt.xid = htonl(client->xid);
    
    if (client->state == DHCP_STATE_RENEWING || 
        client->state == DHCP_STATE_REBINDING) {
        // 续约时使用已有 IP
        pkt.ciaddr = client->info.ip_addr;
    } else {
        pkt.flags = htons(0x8000);  // 广播标志
    }
    
    pkt.magic = htonl(DHCP_MAGIC_COOKIE);
    memcpy(pkt.chaddr, client->dev->mac, 6);
    
    // 添加选项
    uint8_t *opt = pkt.options;
    uint8_t msg_type = DHCP_REQUEST;
    opt = dhcp_add_option(opt, DHCP_OPT_MSG_TYPE, 1, &msg_type);
    
    // 请求的 IP 地址（初始请求时需要）
    if (client->state == DHCP_STATE_REQUESTING) {
        opt = dhcp_add_option(opt, DHCP_OPT_REQ_IP, 4, &client->info.ip_addr);
        opt = dhcp_add_option(opt, DHCP_OPT_SERVER_ID, 4, &client->info.server_ip);
    }
    
    // 参数请求列表
    uint8_t params[] = {DHCP_OPT_SUBNET_MASK, DHCP_OPT_ROUTER, DHCP_OPT_DNS};
    opt = dhcp_add_option(opt, DHCP_OPT_PARAM_REQ, sizeof(params), params);
    
    // 结束
    opt = dhcp_add_option(opt, DHCP_OPT_END, 0, NULL);
    
    uint32_t pkt_len = sizeof(dhcp_packet_t) - sizeof(pkt.options) + 
                       (uint32_t)(opt - pkt.options);
    
    // 发送
    udp_pcb_t *pcb = udp_pcb_new();
    if (!pcb) {
        return -1;
    }
    
    udp_bind(pcb, 0, DHCP_CLIENT_PORT);
    
    netbuf_t *buf = netbuf_alloc(pkt_len);
    if (!buf) {
        udp_pcb_free(pcb);
        return -1;
    }
    
    uint8_t *data = netbuf_put(buf, pkt_len);
    memcpy(data, &pkt, pkt_len);
    
    // 目的地址
    uint32_t dst_ip;
    if (client->state == DHCP_STATE_RENEWING) {
        dst_ip = client->info.server_ip;  // 单播到服务器
    } else {
        dst_ip = 0xFFFFFFFF;  // 广播
    }
    
    int ret = udp_sendto(pcb, buf, dst_ip, DHCP_SERVER_PORT);
    
    netbuf_free(buf);
    udp_pcb_free(pcb);
    
    if (ret < 0) {
        LOG_ERROR_MSG("dhcp: Failed to send REQUEST\n");
        return -1;
    }
    
    LOG_INFO_MSG("dhcp: Sent REQUEST (xid=%08x)\n", client->xid);
    return 0;
}

/**
 * @brief 发送 DHCP RELEASE 报文
 */
static int dhcp_send_release(dhcp_client_t *client) {
    dhcp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    
    pkt.op = DHCP_OP_REQUEST;
    pkt.htype = DHCP_HTYPE_ETH;
    pkt.hlen = 6;
    pkt.xid = htonl(dhcp_generate_xid());
    pkt.ciaddr = client->info.ip_addr;
    pkt.magic = htonl(DHCP_MAGIC_COOKIE);
    memcpy(pkt.chaddr, client->dev->mac, 6);
    
    uint8_t *opt = pkt.options;
    uint8_t msg_type = DHCP_RELEASE;
    opt = dhcp_add_option(opt, DHCP_OPT_MSG_TYPE, 1, &msg_type);
    opt = dhcp_add_option(opt, DHCP_OPT_SERVER_ID, 4, &client->info.server_ip);
    opt = dhcp_add_option(opt, DHCP_OPT_END, 0, NULL);
    
    uint32_t pkt_len = sizeof(dhcp_packet_t) - sizeof(pkt.options) + 
                       (uint32_t)(opt - pkt.options);
    
    udp_pcb_t *pcb = udp_pcb_new();
    if (!pcb) return -1;
    
    udp_bind(pcb, client->info.ip_addr, DHCP_CLIENT_PORT);
    
    netbuf_t *buf = netbuf_alloc(pkt_len);
    if (!buf) {
        udp_pcb_free(pcb);
        return -1;
    }
    
    uint8_t *data = netbuf_put(buf, pkt_len);
    memcpy(data, &pkt, pkt_len);
    
    int ret = udp_sendto(pcb, buf, client->info.server_ip, DHCP_SERVER_PORT);
    
    netbuf_free(buf);
    udp_pcb_free(pcb);
    
    return ret;
}

// ============================================================================
// DHCP 报文处理
// ============================================================================

/**
 * @brief 处理 DHCP OFFER 报文
 */
static void dhcp_handle_offer(dhcp_client_t *client, dhcp_packet_t *pkt, 
                               dhcp_info_t *offer_info) {
    if (client->state != DHCP_STATE_SELECTING) {
        return;
    }
    
    // 保存提供的信息
    client->info.ip_addr = pkt->yiaddr;
    client->info.netmask = offer_info->netmask;
    client->info.gateway = offer_info->gateway;
    client->info.dns_primary = offer_info->dns_primary;
    client->info.dns_secondary = offer_info->dns_secondary;
    client->info.server_ip = offer_info->server_ip;
    client->info.lease_time = offer_info->lease_time;
    
    char ip_str[16];
    ip_to_str(pkt->yiaddr, ip_str);
    LOG_INFO_MSG("dhcp: Received OFFER: %s\n", ip_str);
    
    // 转换到请求状态
    client->state = DHCP_STATE_REQUESTING;
    client->retries = 0;
    
    // 发送 REQUEST
    dhcp_send_request(client);
}

/**
 * @brief 处理 DHCP ACK 报文
 */
static void dhcp_handle_ack(dhcp_client_t *client, dhcp_packet_t *pkt,
                             dhcp_info_t *ack_info) {
    if (client->state != DHCP_STATE_REQUESTING &&
        client->state != DHCP_STATE_RENEWING &&
        client->state != DHCP_STATE_REBINDING) {
        return;
    }
    
    // 更新配置信息
    client->info.ip_addr = pkt->yiaddr;
    if (ack_info->netmask) client->info.netmask = ack_info->netmask;
    if (ack_info->gateway) client->info.gateway = ack_info->gateway;
    if (ack_info->dns_primary) client->info.dns_primary = ack_info->dns_primary;
    if (ack_info->dns_secondary) client->info.dns_secondary = ack_info->dns_secondary;
    if (ack_info->lease_time) client->info.lease_time = ack_info->lease_time;
    if (ack_info->renewal_time) client->info.renewal_time = ack_info->renewal_time;
    if (ack_info->rebind_time) client->info.rebind_time = ack_info->rebind_time;
    
    // 计算默认的 T1 和 T2
    if (client->info.renewal_time == 0) {
        client->info.renewal_time = client->info.lease_time / 2;
    }
    if (client->info.rebind_time == 0) {
        client->info.rebind_time = client->info.lease_time * 7 / 8;
    }
    
    // 记录租约开始时间
    client->info.lease_start = (uint32_t)timer_get_uptime_ms();
    
    // 配置网络接口
    netdev_set_ipaddr(client->dev, client->info.ip_addr);
    netdev_set_netmask(client->dev, client->info.netmask);
    netdev_set_gateway(client->dev, client->info.gateway);
    
    // 添加默认路由
    ip_route_add(0, 0, client->info.gateway, client->dev, 1);
    
    client->state = DHCP_STATE_BOUND;
    
    char ip_str[16], gw_str[16], mask_str[16];
    ip_to_str(client->info.ip_addr, ip_str);
    ip_to_str(client->info.gateway, gw_str);
    ip_to_str(client->info.netmask, mask_str);
    
    LOG_INFO_MSG("dhcp: Bound to %s (netmask %s, gateway %s, lease %us)\n",
                 ip_str, mask_str, gw_str, client->info.lease_time);
}

/**
 * @brief 处理 DHCP NAK 报文
 */
static void dhcp_handle_nak(dhcp_client_t *client) {
    LOG_WARN_MSG("dhcp: Received NAK, restarting discovery\n");
    
    // 重新开始发现过程
    client->state = DHCP_STATE_INIT;
    client->xid = dhcp_generate_xid();
    client->retries = 0;
    
    // 清除配置
    netdev_set_ipaddr(client->dev, 0);
    
    // 重新发送 DISCOVER
    client->state = DHCP_STATE_SELECTING;
    dhcp_send_discover(client);
}

/**
 * @brief 处理收到的 DHCP 数据包
 */
void dhcp_input(netdev_t *dev, uint8_t *data, uint32_t len) {
    if (len < sizeof(dhcp_packet_t) - 312) {  // 最小长度（无选项）
        return;
    }
    
    dhcp_packet_t *pkt = (dhcp_packet_t *)data;
    
    // 验证报文
    if (pkt->op != DHCP_OP_REPLY) return;
    if (ntohl(pkt->magic) != DHCP_MAGIC_COOKIE) return;
    
    bool irq_state;
    spinlock_lock_irqsave(&dhcp_lock, &irq_state);
    
    dhcp_client_t *client = dhcp_find_client(dev);
    if (!client) {
        spinlock_unlock_irqrestore(&dhcp_lock, irq_state);
        return;
    }
    
    // 验证事务 ID
    if (ntohl(pkt->xid) != client->xid) {
        spinlock_unlock_irqrestore(&dhcp_lock, irq_state);
        return;
    }
    
    // 验证 MAC 地址
    if (memcmp(pkt->chaddr, dev->mac, 6) != 0) {
        spinlock_unlock_irqrestore(&dhcp_lock, irq_state);
        return;
    }
    
    // 解析选项
    dhcp_info_t info;
    uint8_t msg_type = 0;
    memset(&info, 0, sizeof(info));
    
    uint32_t opt_len = len - (sizeof(dhcp_packet_t) - 312);
    if (opt_len > 312) opt_len = 312;
    
    dhcp_parse_options(pkt->options, opt_len, &info, &msg_type);
    
    // 根据消息类型处理
    switch (msg_type) {
        case DHCP_OFFER:
            dhcp_handle_offer(client, pkt, &info);
            break;
            
        case DHCP_ACK:
            dhcp_handle_ack(client, pkt, &info);
            break;
            
        case DHCP_NAK:
            dhcp_handle_nak(client);
            break;
    }
    
    spinlock_unlock_irqrestore(&dhcp_lock, irq_state);
}

// ============================================================================
// 公共接口
// ============================================================================

/**
 * @brief 启动 DHCP 客户端
 */
int dhcp_start(netdev_t *dev) {
    if (!dev) return -1;
    
    bool irq_state;
    spinlock_lock_irqsave(&dhcp_lock, &irq_state);
    
    // 检查是否已存在客户端
    dhcp_client_t *client = dhcp_find_client(dev);
    if (client) {
        spinlock_unlock_irqrestore(&dhcp_lock, irq_state);
        return -1;  // 已经在运行
    }
    
    // 分配新客户端
    client = dhcp_alloc_client(dev);
    if (!client) {
        spinlock_unlock_irqrestore(&dhcp_lock, irq_state);
        LOG_ERROR_MSG("dhcp: No available client slots\n");
        return -1;
    }
    
    // 初始化客户端
    client->state = DHCP_STATE_INIT;
    client->xid = dhcp_generate_xid();
    client->retries = 0;
    
    // 清除当前 IP 配置
    netdev_set_ipaddr(dev, 0);
    
    // 开始发现过程
    client->state = DHCP_STATE_SELECTING;
    int ret = dhcp_send_discover(client);
    
    spinlock_unlock_irqrestore(&dhcp_lock, irq_state);
    
    return ret;
}

/**
 * @brief 停止 DHCP 客户端
 */
void dhcp_stop(netdev_t *dev) {
    if (!dev) return;
    
    bool irq_state;
    spinlock_lock_irqsave(&dhcp_lock, &irq_state);
    
    dhcp_client_t *client = dhcp_find_client(dev);
    if (client) {
        client->dev = NULL;
        client->state = DHCP_STATE_INIT;
    }
    
    spinlock_unlock_irqrestore(&dhcp_lock, irq_state);
}

/**
 * @brief 释放 DHCP 租约
 */
int dhcp_release(netdev_t *dev) {
    if (!dev) return -1;
    
    bool irq_state;
    spinlock_lock_irqsave(&dhcp_lock, &irq_state);
    
    dhcp_client_t *client = dhcp_find_client(dev);
    if (!client || client->state != DHCP_STATE_BOUND) {
        spinlock_unlock_irqrestore(&dhcp_lock, irq_state);
        return -1;
    }
    
    // 发送 RELEASE
    dhcp_send_release(client);
    
    // 清除配置
    netdev_set_ipaddr(dev, 0);
    client->state = DHCP_STATE_INIT;
    
    spinlock_unlock_irqrestore(&dhcp_lock, irq_state);
    
    LOG_INFO_MSG("dhcp: Released lease\n");
    return 0;
}

/**
 * @brief 获取 DHCP 状态
 */
dhcp_state_t dhcp_get_status(netdev_t *dev, dhcp_info_t *info) {
    if (!dev) return DHCP_STATE_ERROR;
    
    bool irq_state;
    spinlock_lock_irqsave(&dhcp_lock, &irq_state);
    
    dhcp_client_t *client = dhcp_find_client(dev);
    if (!client) {
        spinlock_unlock_irqrestore(&dhcp_lock, irq_state);
        return DHCP_STATE_ERROR;
    }
    
    dhcp_state_t state = client->state;
    if (info) {
        memcpy(info, &client->info, sizeof(dhcp_info_t));
    }
    
    spinlock_unlock_irqrestore(&dhcp_lock, irq_state);
    return state;
}

/**
 * @brief DHCP 定时器处理
 */
void dhcp_timer(void) {
    uint32_t now = (uint32_t)timer_get_uptime_ms();
    
    bool irq_state;
    spinlock_lock_irqsave(&dhcp_lock, &irq_state);
    
    for (int i = 0; i < DHCP_MAX_CLIENTS; i++) {
        dhcp_client_t *client = &dhcp_clients[i];
        if (!client->dev) continue;
        
        switch (client->state) {
            case DHCP_STATE_SELECTING:
            case DHCP_STATE_REQUESTING:
                // 检查超时和重试
                // TODO: 实现超时重试逻辑
                break;
                
            case DHCP_STATE_BOUND: {
                // 检查是否需要续约
                uint32_t elapsed = (now - client->info.lease_start) / 1000;
                
                if (elapsed >= client->info.rebind_time) {
                    // T2 超时，进入重绑定状态
                    client->state = DHCP_STATE_REBINDING;
                    client->xid = dhcp_generate_xid();
                    dhcp_send_request(client);
                    LOG_INFO_MSG("dhcp: Starting rebinding\n");
                } else if (elapsed >= client->info.renewal_time) {
                    // T1 超时，进入续约状态
                    client->state = DHCP_STATE_RENEWING;
                    client->xid = dhcp_generate_xid();
                    dhcp_send_request(client);
                    LOG_INFO_MSG("dhcp: Starting renewal\n");
                }
                break;
            }
            
            case DHCP_STATE_RENEWING:
            case DHCP_STATE_REBINDING: {
                // 检查租约是否过期
                uint32_t elapsed = (now - client->info.lease_start) / 1000;
                if (elapsed >= client->info.lease_time) {
                    // 租约过期
                    LOG_WARN_MSG("dhcp: Lease expired\n");
                    netdev_set_ipaddr(client->dev, 0);
                    client->state = DHCP_STATE_INIT;
                    // 重新开始
                    client->xid = dhcp_generate_xid();
                    client->state = DHCP_STATE_SELECTING;
                    dhcp_send_discover(client);
                }
                break;
            }
            
            default:
                break;
        }
    }
    
    spinlock_unlock_irqrestore(&dhcp_lock, irq_state);
}

