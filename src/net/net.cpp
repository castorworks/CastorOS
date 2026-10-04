/**
 * @file net.c
 * @brief 网络栈初始化
 */

#include <kernel/deferred.h>
#include <net/net.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <drivers/timer.h>

// TCP 定时器回调 ID
static uint32_t tcp_timer_id = 0;

/**
 * @brief TCP 定时器回调函数
 */
static int tcp_timer_work = -1;

/* 在 kworker 线程里运行：重传会走驱动的发送路径（Mutex），不能在中断里做 */
static void net_tcp_timer_work(void) {
    net::Tcp::timer();
}

/* 定时器中断里只置位，实际工作交给 kworker */
static void net_tcp_timer_callback(void *data) {
    (void)data;
    kernel::Deferred::raise(tcp_timer_work);
}

void net::Stack::init() {
    LOG_INFO_MSG("net: Initializing network stack...\n");
    
    // 1. 初始化网络设备层
    net::Netdev::init();
    
    // 2. 初始化以太网层
    net::Ethernet::init();
    
    // 3. 初始化 ARP 协议
    net::Arp::init();
    
    // 4. 初始化 IP 协议
    net::Ip::init();
    
    // 5. 初始化 ICMP 协议
    net::Icmp::init();
    
    // 6. 初始化 UDP 协议
    net::Udp::init();
    
    // 7. 初始化 TCP 协议
    net::Tcp::init();
    
    // 8. 初始化 Socket 子系统
    net::Socket::init();
    
    // 9. 注册 TCP 定时器（每 100ms 调用一次）
    tcp_timer_work = kernel::Deferred::add(net_tcp_timer_work, "tcp_timer");
    tcp_timer_id = drivers::Timer::register_callback(net_tcp_timer_callback, NULL, 100, true);
    if (tcp_timer_id == 0) {
        LOG_WARN_MSG("net: Failed to register TCP timer\n");
    }
    
    LOG_INFO_MSG("net: Network stack initialized\n");
}

int net::Stack::configure(const char *ip, const char *netmask, const char *gateway) {
    net::Netdev *dev = net::Netdev::get_default();
    if (!dev) {
        LOG_ERROR_MSG("net: No network device available\n");
        return -1;
    }
    
    uint32_t ip_addr, mask_addr, gw_addr;
    
    if (str_to_ip(ip, &ip_addr) < 0) {
        LOG_ERROR_MSG("net: Invalid IP address: %s\n", ip);
        return -1;
    }
    
    if (str_to_ip(netmask, &mask_addr) < 0) {
        LOG_ERROR_MSG("net: Invalid netmask: %s\n", netmask);
        return -1;
    }
    
    if (str_to_ip(gateway, &gw_addr) < 0) {
        LOG_ERROR_MSG("net: Invalid gateway: %s\n", gateway);
        return -1;
    }
    
    net::Netdev::set_ipaddr(dev, ip_addr);
    net::Netdev::set_netmask(dev, mask_addr);
    net::Netdev::set_gateway(dev, gw_addr);
    
    char ip_str[16], mask_str[16], gw_str[16];
    net::Ip::to_str(ip_addr, ip_str);
    net::Ip::to_str(mask_addr, mask_str);
    net::Ip::to_str(gw_addr, gw_str);
    
    LOG_INFO_MSG("net: Configured %s: ip=%s netmask=%s gateway=%s\n",
                 dev->name, ip_str, mask_str, gw_str);
    
    return 0;
}

// Ping 回调数据
static struct {
    bool received;
    uint32_t rtt;
    uint32_t src_ip;
} ping_result;

static void ping_callback(uint32_t src_ip, uint16_t seq, uint32_t rtt_ms, bool success) {
    (void)seq;
    if (success) {
        ping_result.received = true;
        ping_result.rtt = rtt_ms;
        ping_result.src_ip = src_ip;
    }
}

int net::Stack::ping(const char *ip_str, int count) {
    uint32_t dst_ip;
    if (str_to_ip(ip_str, &dst_ip) < 0) {
        kprintf("ping: Invalid IP address: %s\n", ip_str);
        return -1;
    }
    
    // 注册回调
    net::Icmp::register_ping_callback(ping_callback);
    
    kprintf("PING %s: 56 data bytes\n", ip_str);
    
    int sent = 0, received = 0;
    uint32_t min_rtt = 0xFFFFFFFF, max_rtt = 0, total_rtt = 0;
    
    for (int i = 0; i < count; i++) {
        ping_result.received = false;
        
        // 发送 ping
        if (net::Icmp::send_echo_request(dst_ip, 1, i + 1, NULL, 56) < 0) {
            kprintf("ping: send failed\n");
            continue;
        }
        sent++;
        
        // 等待响应（最多 1 秒）
        uint32_t start = (uint32_t)drivers::Timer::get_uptime_ms();
        while (!ping_result.received && 
               (uint32_t)drivers::Timer::get_uptime_ms() - start < 1000) {
            // 处理接收队列并让出 CPU，回应由接收路径填入 ping_result
            net::Netdev::wait_tick();
        }
        
        if (ping_result.received) {
            received++;
            uint32_t rtt = ping_result.rtt;
            
            if (rtt < min_rtt) min_rtt = rtt;
            if (rtt > max_rtt) max_rtt = rtt;
            total_rtt += rtt;
            
            char src_str[16];
            net::Ip::to_str(ping_result.src_ip, src_str);
            kprintf("64 bytes from %s: icmp_seq=%d time=%u ms\n",
                    src_str, i + 1, rtt);
        } else {
            kprintf("Request timeout for icmp_seq %d\n", i + 1);
        }
        
        // 等待 1 秒再发送下一个
        if (i < count - 1) {
            drivers::Timer::wait(1000);
        }
    }
    
    // 打印统计
    kprintf("\n--- %s ping statistics ---\n", ip_str);
    kprintf("%d packets transmitted, %d received, %d%% packet loss\n",
            sent, received, sent > 0 ? (sent - received) * 100 / sent : 0);
    
    if (received > 0) {
        kprintf("rtt min/avg/max = %u/%u/%u ms\n",
                min_rtt, total_rtt / received, max_rtt);
    }
    
    net::Icmp::register_ping_callback(NULL);
    
    return received > 0 ? 0 : -1;
}

