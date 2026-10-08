// net - 网络服务
//
// 用户态服务，以 "net" 登记，实现 net.h 里的协议。没有特权：只碰得到 init 许可给它的
// 那块网卡。它把两样东西放在同一个进程里：virtio-net 网卡驱动，和一个很小的协议栈。
// 放在一起是因为收包是由中断驱动的：一个主循环同时等中断、定时器和客户请求，
// 帧到了直接交给协议栈，不需要驱动和协议栈之间再来回传一次。
//
// 这个文件是服务本身：地址配置、主循环、把请求分给各个协议。其余的按层分开：
//   nic.cpp   网卡驱动（virtio-net）
//   ip.cpp    以太网、ARP、IPv4、ICMP 回显（ping）
//   udp.cpp   UDP 套接字
//   tcp.cpp   TCP
//   dhcp.cpp  DHCP 客户端
// 它们之间的接口都在 net_internal.h 里。
//
// 地址在启动时用 DHCP 获取；等不到应答就退回 QEMU 用户网络（-netdev user）的固定配置
// 10.0.2.15/24，网关 10.0.2.2。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <names.h>
#include <net.h>
#include <clients.h>
#include "net_internal.h"

// ============================================================================
// 配置
// ============================================================================

// 地址配置：DHCP 完成（或放弃）之前 my_ip 是 0
uint32_t my_ip = 0;
uint32_t netmask = 0;
uint32_t gateway = 0;
uint32_t dns_server = 0;
bool from_dhcp = false;

uint8_t my_mac[6];

void reply_client(int pid, uint32_t label, int64_t result, uint64_t d1, uint64_t d2) {
    struct ipc_msg m = {};
    m.label = label;
    m.data[0] = (uint64_t)result;
    m.data[1] = d1;
    m.data[2] = d2;
    ipc_reply(pid, &m);
}


// ============================================================================
// 超时
// ============================================================================

#define TICK_MS 50

/** 处理到期的等待，返回是否还有东西在等（还需要定时器） */
static bool expire_waiters(void) {
    uint64_t now = uptime_ms();
    bool waiting = arp_tick(now);
    waiting = dhcp_tick(now) || waiting;
    waiting = tcp_tick(now) || waiting;

    waiting = ping_tick(now) || waiting;
    waiting = udp_tick(now) || waiting;
    return waiting;
}

// ============================================================================
// 主循环
// ============================================================================

static void handle_request(struct ipc_msg *m) {
    int pid = (int)m->sender;
    struct ipc_msg reply = {};
    reply.label = m->label;
    int64_t result = -1;

    if (m->label >= NET_TCP_CONNECT && m->label <= NET_TCP_ACCEPT) {
        tcp_request(m);
        return;
    }
    if (m->label >= NET_UDP_OPEN && m->label <= NET_UDP_RECV) {
        udp_request(m);
        return;
    }
    if (m->label == NET_DEBUG_DROP) {
        nic_debug_drop((uint32_t)m->data[0], (uint32_t)m->data[1]);
        reply_client(pid, NET_DEBUG_DROP, 0, tcp_retransmits, 0);
        return;
    }
    if (m->label == NET_DEBUG_FRAGMENT) {
        ip_debug_fragment((uint32_t)m->data[0]);
        reply_client(pid, NET_DEBUG_FRAGMENT, 0, 0, 0);
        return;
    }
    if (m->label == NET_DEBUG_EXIT) {
        reply_client(pid, NET_DEBUG_EXIT, 0, 0, 0);
        printf("net: exiting on request (NET_DEBUG_EXIT)\n");
        exit(1);
    }
    if (m->label == NET_DEBUG_RENEW) {
        reply_client(pid, NET_DEBUG_RENEW, from_dhcp ? 0 : -1, dhcp_debug_renew(), 0);
        return;
    }

    switch (m->label) {
        case NET_INFO:
            result = 0;
            for (int i = 0; i < 6; i++) {
                reply.data[1] |= (uint64_t)my_mac[i] << (8 * i);
            }
            reply.data[1] |= (uint64_t)from_dhcp << 48;
            reply.data[2] = my_ip;
            reply.data[3] = netmask;
            reply.data[4] = gateway;
            reply.data[5] = dns_server;
            break;

        case NET_PING:
            if (my_ip == 0) {
                break;      // 还没有地址
            }
            // 应答在收到回显、或者超时的时候发
            ping_start(pid, (uint32_t)m->data[0], (uint32_t)m->data[1]);
            return;
    }

    reply.data[0] = (uint64_t)result;
    ipc_reply(pid, &reply);
}

/** 一个客户退出了：收回它的套接字和连接 */
static void client_gone(int pid) {
    udp_drop_owner(pid);
    tcp_drop_owner(pid);
}

int main() {
    if (!nic_init()) {
        printf("net: no usable network card\n");
        return 1;
    }
    clients_init(NET_BUF_SIZE, client_gone);
    if (name_register(NET_SERVICE_NAME) != 0) {
        printf("net: cannot register name\n");
        return 1;
    }
    printf("net: ready (pid %d, %s, irq %d), %02x:%02x:%02x:%02x:%02x:%02x\n", getpid(), nic_kind(), nic_irq_line(),
           my_mac[0], my_mac[1], my_mac[2], my_mac[3], my_mac[4], my_mac[5]);
    dhcp_start();
    timer_set(TICK_MS);

    struct ipc_msg m;
    for (;;) {
        if (ipc_recv(IPC_ANY, &m) != 0) {
            continue;
        }

        if (m.sender == IPC_KERNEL) {
            if (m.label == IPC_LABEL_IRQ) {
                nic_interrupt();
            }
            // IPC_LABEL_TIMER：下面统一处理
        } else if (m.label == IPC_LABEL_GRANT) {
            clients_attach(&m);
        } else {
            handle_request(&m);
        }
        loopback_drain();

        // 只要还有事在等（ping、recv、ARP、DHCP），就保持一个周期性的定时器来处理超时
        bool waiting = expire_waiters();
        loopback_drain();       // 超时处理里也可能发出回环包（重传）
        if (waiting) {
            timer_set(TICK_MS);
        } else if (dhcp_next_deadline() != 0) {
            // 没有别的事在等：睡到该续租的时候。这之前来了请求或者中断，回到这里会重新设
            uint64_t now = uptime_ms(), at = dhcp_next_deadline();
            uint64_t wait = at > now ? at - now : 1;
            timer_set(wait > 0x7FFFFFFF ? 0x7FFFFFFF : (uint32_t)wait);
        }
    }
}
