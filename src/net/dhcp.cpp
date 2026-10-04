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
#include <kernel/sync/mutex.h>
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

/* 客户端表只在任务上下文访问（shell、接收线程、kworker 定时器），而且持锁期间
 * 要发包、要改接口配置（都会拿 Mutex），所以用 Mutex 而不是自旋锁。
 * Mutex 可重入：收到 OFFER 后在同一任务里发 REQUEST 不会自锁。 */
static sync::Mutex dhcp_mutex;

/* 所有客户端共用的 UDP PCB：绑定 0.0.0.0:68，接收回调把应答交给 Dhcp::input。
 * 第一个客户端启动时创建，最后一个停止时释放。 */
static udp_pcb_t *dhcp_pcb = NULL;

/* 续约/重绑定请求的重发间隔（毫秒） */
#define DHCP_RENEW_RETRY_INTERVAL   60000

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 查找设备对应的 DHCP 客户端
 */
static dhcp_client_t *dhcp_find_client(net::Netdev *dev) {
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
static dhcp_client_t *dhcp_alloc_client(net::Netdev *dev) {
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
    return seed ^ (uint32_t)drivers::Timer::get_uptime_ms();
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
 * @brief UDP 接收回调：68 端口收到的数据报交给状态机
 */
static void dhcp_udp_recv(udp_pcb_t *pcb, net::Netbuf *buf,
                          uint32_t src_ip, uint16_t src_port) {
    (void)pcb;
    (void)src_ip;
    (void)src_port;
    if (buf->dev) {
        net::Dhcp::input(buf->dev, buf->data, buf->len);
    }
    net::Netbuf::free(buf);  // 回调负责释放 buf
}

/**
 * @brief 确保共用的 DHCP PCB 已创建（调用者持有 dhcp_mutex）
 */
static int dhcp_open_pcb(void) {
    if (dhcp_pcb) {
        return 0;
    }
    udp_pcb_t *pcb = net::Udp::pcb_new();
    if (!pcb) {
        return -1;
    }
    if (net::Udp::bind(pcb, 0, DHCP_CLIENT_PORT) < 0) {
        net::Udp::pcb_free(pcb);
        return -1;
    }
    net::Udp::recv(pcb, dhcp_udp_recv, NULL);
    dhcp_pcb = pcb;
    return 0;
}

/**
 * @brief 没有客户端在用时释放共用 PCB（调用者持有 dhcp_mutex）
 */
static void dhcp_close_pcb_if_idle(void) {
    if (!dhcp_pcb) {
        return;
    }
    for (int i = 0; i < DHCP_MAX_CLIENTS; i++) {
        if (dhcp_clients[i].dev != NULL) {
            return;
        }
    }
    net::Udp::pcb_free(dhcp_pcb);
    dhcp_pcb = NULL;
}

/**
 * @brief 在客户端所在的设备上发送一个 DHCP 报文
 */
static int dhcp_send_packet(dhcp_client_t *client, dhcp_packet_t *pkt,
                            uint32_t pkt_len, uint32_t dst_ip) {
    if (!dhcp_pcb) {
        return -1;
    }

    net::Netbuf *buf = net::Netbuf::alloc(pkt_len);
    if (!buf) {
        return -1;
    }

    uint8_t *data = net::Netbuf::put(buf, pkt_len);
    if (!data) {
        net::Netbuf::free(buf);
        return -1;
    }
    memcpy(data, pkt, pkt_len);

    int ret = net::Udp::sendto(dhcp_pcb, buf, dst_ip, DHCP_SERVER_PORT, client->dev);
    net::Netbuf::free(buf);  // 发送只是借用 buf

    client->last_send = (uint32_t)drivers::Timer::get_uptime_ms();
    return ret;
}

/**
 * @brief 恢复启动 DHCP 之前的接口配置（获取地址失败或中途停止时）
 */
static void dhcp_restore_config(dhcp_client_t *client) {
    net::Netdev::set_ipaddr(client->dev, client->saved_ip);
}

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
    
    // 广播发送：源 IP 0.0.0.0，目的 IP 255.255.255.255
    if (dhcp_send_packet(client, &pkt, pkt_len, 0xFFFFFFFF) < 0) {
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
    
    // 目的地址
    uint32_t dst_ip;
    if (client->state == DHCP_STATE_RENEWING) {
        dst_ip = client->info.server_ip;  // 单播到服务器
    } else {
        dst_ip = 0xFFFFFFFF;  // 广播
    }

    if (dhcp_send_packet(client, &pkt, pkt_len, dst_ip) < 0) {
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
    
    return dhcp_send_packet(client, &pkt, pkt_len, client->info.server_ip);
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
    net::Ip::to_str(pkt->yiaddr, ip_str);
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
    client->info.lease_start = (uint32_t)drivers::Timer::get_uptime_ms();
    
    // 配置网络接口
    net::Netdev::set_ipaddr(client->dev, client->info.ip_addr);
    net::Netdev::set_netmask(client->dev, client->info.netmask);
    net::Netdev::set_gateway(client->dev, client->info.gateway);
    
    // 添加默认路由
    net::Ip::route_add(0, 0, client->info.gateway, client->dev, 1);
    
    client->state = DHCP_STATE_BOUND;
    
    char ip_str[16], gw_str[16], mask_str[16];
    net::Ip::to_str(client->info.ip_addr, ip_str);
    net::Ip::to_str(client->info.gateway, gw_str);
    net::Ip::to_str(client->info.netmask, mask_str);
    
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
    
    // 清除配置（被拒绝的地址不再有效，也不需要恢复）
    net::Netdev::set_ipaddr(client->dev, 0);
    client->saved_ip = 0;

    // 重新发送 DISCOVER
    client->state = DHCP_STATE_SELECTING;
    dhcp_send_discover(client);
}

/**
 * @brief 处理收到的 DHCP 数据包
 */
void net::Dhcp::input(net::Netdev *dev, uint8_t *data, uint32_t len) {
    if (len < sizeof(dhcp_packet_t) - 312) {  // 最小长度（无选项）
        return;
    }
    
    dhcp_packet_t *pkt = (dhcp_packet_t *)data;
    
    // 验证报文
    if (pkt->op != DHCP_OP_REPLY) return;
    if (ntohl(pkt->magic) != DHCP_MAGIC_COOKIE) return;
    
    sync::MutexGuard guard(dhcp_mutex);

    dhcp_client_t *client = dhcp_find_client(dev);
    if (!client) {
        return;
    }

    // 验证事务 ID
    if (ntohl(pkt->xid) != client->xid) {
        return;
    }
    
    // 验证 MAC 地址
    if (memcmp(pkt->chaddr, dev->mac, 6) != 0) {
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
}

// ============================================================================
// 公共接口
// ============================================================================

/**
 * @brief 启动 DHCP 客户端
 */
int net::Dhcp::start(net::Netdev *dev) {
    if (!dev) return -1;

    sync::MutexGuard guard(dhcp_mutex);

    // 已有客户端：只有空闲（已释放租约）或失败状态才允许重新开始
    dhcp_client_t *client = dhcp_find_client(dev);
    if (client && client->state != DHCP_STATE_INIT &&
        client->state != DHCP_STATE_ERROR) {
        return -1;  // 已经在运行
    }

    if (!client) {
        client = dhcp_alloc_client(dev);
        if (!client) {
            LOG_ERROR_MSG("dhcp: No available client slots\n");
            return -1;
        }
    }

    if (dhcp_open_pcb() < 0) {
        LOG_ERROR_MSG("dhcp: Failed to open UDP port %u\n", DHCP_CLIENT_PORT);
        client->dev = NULL;
        dhcp_close_pcb_if_idle();
        return -1;
    }

    // 初始化客户端
    client->xid = dhcp_generate_xid();
    client->retries = 0;

    // 记下当前地址后清除：DISCOVER 以 0.0.0.0 为源地址广播。
    // 拿不到租约（发送失败、超时、中途 stop）时恢复原地址。
    client->saved_ip = dev->ip_addr;
    net::Netdev::set_ipaddr(dev, 0);

    // 开始发现过程
    client->state = DHCP_STATE_SELECTING;
    if (dhcp_send_discover(client) < 0) {
        dhcp_restore_config(client);
        client->dev = NULL;
        client->state = DHCP_STATE_INIT;
        dhcp_close_pcb_if_idle();
        return -1;
    }

    return 0;
}

/**
 * @brief 停止 DHCP 客户端
 */
void net::Dhcp::stop(net::Netdev *dev) {
    if (!dev) return;

    sync::MutexGuard guard(dhcp_mutex);

    dhcp_client_t *client = dhcp_find_client(dev);
    if (client) {
        // 还没拿到租约就停止：把启动时清掉的地址放回去
        if (client->state == DHCP_STATE_SELECTING ||
            client->state == DHCP_STATE_REQUESTING) {
            dhcp_restore_config(client);
        }
        client->dev = NULL;
        client->state = DHCP_STATE_INIT;
        dhcp_close_pcb_if_idle();
    }
}

/**
 * @brief 释放 DHCP 租约
 */
int net::Dhcp::release(net::Netdev *dev) {
    if (!dev) return -1;

    {
        sync::MutexGuard guard(dhcp_mutex);
        dhcp_client_t *client = dhcp_find_client(dev);
        if (!client || client->state != DHCP_STATE_BOUND) {
            return -1;
        }

        // 发送 RELEASE
        dhcp_send_release(client);

        // 清除配置
        net::Netdev::set_ipaddr(dev, 0);
        client->saved_ip = 0;
        client->state = DHCP_STATE_INIT;
    }

    LOG_INFO_MSG("dhcp: Released lease\n");
    return 0;
}

/**
 * @brief 获取 DHCP 状态
 */
dhcp_state_t net::Dhcp::get_status(net::Netdev *dev, dhcp_info_t *info) {
    if (!dev) return DHCP_STATE_ERROR;

    sync::MutexGuard guard(dhcp_mutex);

    dhcp_client_t *client = dhcp_find_client(dev);
    if (!client) {
        return DHCP_STATE_ERROR;
    }

    dhcp_state_t state = client->state;
    if (info) {
        memcpy(info, &client->info, sizeof(dhcp_info_t));
    }

    return state;
}

/**
 * @brief DHCP 定时器处理（任务上下文，约每秒一次）
 */
void net::Dhcp::timer() {
    uint32_t now = (uint32_t)drivers::Timer::get_uptime_ms();

    sync::MutexGuard guard(dhcp_mutex);

    for (int i = 0; i < DHCP_MAX_CLIENTS; i++) {
        dhcp_client_t *client = &dhcp_clients[i];
        if (!client->dev) continue;

        switch (client->state) {
            case DHCP_STATE_SELECTING:
            case DHCP_STATE_REQUESTING:
                // 应答超时：重发，重试用尽则放弃并恢复原来的地址
                if (now - client->last_send < DHCP_DISCOVER_TIMEOUT) {
                    break;
                }
                if (client->retries >= DHCP_MAX_RETRIES) {
                    LOG_WARN_MSG("dhcp: No response from server, giving up\n");
                    dhcp_restore_config(client);
                    client->state = DHCP_STATE_ERROR;
                    break;
                }
                client->retries++;
                if (client->state == DHCP_STATE_SELECTING) {
                    dhcp_send_discover(client);
                } else {
                    dhcp_send_request(client);
                }
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
                    // 租约过期，重新开始
                    LOG_WARN_MSG("dhcp: Lease expired\n");
                    net::Netdev::set_ipaddr(client->dev, 0);
                    client->saved_ip = 0;
                    client->xid = dhcp_generate_xid();
                    client->retries = 0;
                    client->state = DHCP_STATE_SELECTING;
                    dhcp_send_discover(client);
                } else if (client->state == DHCP_STATE_RENEWING &&
                           elapsed >= client->info.rebind_time) {
                    // 续约一直没有应答，到 T2 改为广播重绑定
                    client->state = DHCP_STATE_REBINDING;
                    dhcp_send_request(client);
                } else if (now - client->last_send >= DHCP_RENEW_RETRY_INTERVAL) {
                    dhcp_send_request(client);
                }
                break;
            }

            default:
                break;
        }
    }
}
