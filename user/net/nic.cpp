// nic.cpp - 网卡驱动：virtio-net
//
// 网络服务里碰硬件的只有这个文件：打开 init 许可给本进程的那块网卡，收发以太网帧。
// 收到的帧交给协议栈的 eth_input（ip.cpp），协议栈用 nic_send 发帧。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <net.h>
#include <virtio.h>
#include "net_internal.h"

#define PAGE_SIZE           4096
#define VIRTIO_NET_F_MAC    (1u << 5)       // 配置空间里有 MAC 地址

// 每个帧前面都有这个头（legacy，没有协商 MRG_RXBUF 时是 10 字节）
struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed));

#define NIC_BUF_SIZE    2048                // 一个缓冲区放一个帧（含 virtio 头）
#define RX_BUFS         32
#define TX_BUFS         16

static struct virtio_dev nic;
static struct virtq rxq, txq;               // 队列 0 收，队列 1 发
static char *rx_mem, *tx_mem;
static uint64_t rx_phys, tx_phys;
static uint16_t tx_free[TX_BUFS];           // 空闲的发送缓冲区
static int tx_free_count;

// 调试用：丢掉接下来发出/收到的若干个 TCP 帧（NET_DEBUG_DROP），用来验证重传
static uint32_t drop_tx = 0, drop_rx = 0;

static bool is_tcp_frame(const uint8_t *frame, size_t len) {
    return len >= 34 && frame[12] == 0x08 && frame[13] == 0x00 && frame[23] == IP_PROTO_TCP;
}

// 带 SYN 或数据的 TCP 帧：丢了会被重传的那种。发送方向只丢这种——纯确认谁都可能
// 随时发一个（比如确认上一个连接迟到的 FIN），让它把名额占掉，要验证的重传就不会发生
static bool is_tcp_frame_with_payload(const uint8_t *frame, size_t len) {
    if (!is_tcp_frame(frame, len)) {
        return false;
    }
    size_t ip_len = (size_t)(frame[14] & 0x0F) * 4;
    size_t total = ((size_t)frame[16] << 8) | frame[17];
    if (len < 14 + ip_len + 20) {
        return false;
    }
    const uint8_t *tcp = frame + 14 + ip_len;
    size_t tcp_len = (size_t)(tcp[12] >> 4) * 4;
    return (tcp[13] & 0x02) != 0 || total > ip_len + tcp_len;
}

bool nic_init(void) {
    if (!virtio_open(&nic, VIRTIO_ID_NET)) {
        return false;
    }
    if (!(virtio_get_features(&nic) & VIRTIO_NET_F_MAC)) {
        printf("net: device does not provide a MAC address\n");
        return false;
    }
    virtio_set_features(&nic, VIRTIO_NET_F_MAC);
    for (int i = 0; i < 6; i++) {
        my_mac[i] = virtio_config_read8(&nic, (uint32_t)i);
    }

    rx_mem = (char *)dma_alloc(RX_BUFS * NIC_BUF_SIZE, &rx_phys);
    tx_mem = (char *)dma_alloc(TX_BUFS * NIC_BUF_SIZE, &tx_phys);
    if (rx_mem == MAP_FAILED || tx_mem == MAP_FAILED ||
        !virtq_setup(&nic, &rxq, 0, 64) || !virtq_setup(&nic, &txq, 1, 64) ||
        rxq.num < RX_BUFS || txq.num < TX_BUFS) {
        printf("net: cannot set up the queues\n");
        return false;
    }

    // 把所有接收缓冲区交给设备：描述符 i 固定对应缓冲区 i
    for (uint16_t i = 0; i < RX_BUFS; i++) {
        rxq.desc[i].addr = rx_phys + (uint64_t)i * NIC_BUF_SIZE;
        rxq.desc[i].len = NIC_BUF_SIZE;
        rxq.desc[i].flags = VRING_DESC_F_WRITE;
        rxq.desc[i].next = 0;
        virtq_submit(&rxq, i);
    }
    // 发送完成不需要中断：下次发送时顺手回收
    txq.avail->flags = VRING_AVAIL_F_NO_INTERRUPT;
    for (uint16_t i = 0; i < TX_BUFS; i++) {
        tx_free[tx_free_count++] = i;
    }

    if (irq_claim(nic.irq) != 0) {
        printf("net: cannot claim IRQ %d\n", nic.irq);
        return false;
    }
    virtio_driver_ok(&nic);
    virtio_notify(&nic, &rxq);
    return true;
}

void nic_send(const uint8_t *frame, size_t len) {
    uint32_t done;
    while (virtq_pop_used(&txq, &done, NULL)) {
        tx_free[tx_free_count++] = (uint16_t)done;
    }
    if (tx_free_count == 0 || len > FRAME_MAX) {
        return;
    }
    if (drop_tx > 0 && is_tcp_frame_with_payload(frame, len)) {
        drop_tx--;
        return;
    }
    uint16_t i = tx_free[--tx_free_count];
    char *buf = tx_mem + (size_t)i * NIC_BUF_SIZE;
    memset(buf, 0, sizeof(struct virtio_net_hdr));
    memcpy(buf + sizeof(struct virtio_net_hdr), frame, len);

    txq.desc[i].addr = tx_phys + (uint64_t)i * NIC_BUF_SIZE;
    txq.desc[i].len = (uint32_t)(sizeof(struct virtio_net_hdr) + len);
    txq.desc[i].flags = 0;
    txq.desc[i].next = 0;
    virtq_submit(&txq, i);
    virtio_notify(&nic, &txq);
}

/** 处理设备交回来的接收缓冲区，再把它们还给设备 */
static void nic_poll(void) {
    uint32_t i, len;
    bool any = false;
    while (virtq_pop_used(&rxq, &i, &len)) {
        if (i < RX_BUFS && len > sizeof(struct virtio_net_hdr) && len <= NIC_BUF_SIZE) {
            uint8_t *frame = (uint8_t *)rx_mem + (size_t)i * NIC_BUF_SIZE + sizeof(struct virtio_net_hdr);
            size_t frame_len = len - sizeof(struct virtio_net_hdr);
            if (drop_rx > 0 && is_tcp_frame(frame, frame_len)) {
                drop_rx--;
            } else {
                eth_input(frame, frame_len);
            }
        }
        virtq_submit(&rxq, (uint16_t)i);
        any = true;
    }
    if (any) {
        virtio_notify(&nic, &rxq);
    }
}


int nic_irq_line(void) {
    return nic.irq;
}

void nic_interrupt(void) {
    virtio_irq_ack(&nic);
    nic_poll();
    irq_ack(nic.irq);
}

void nic_debug_drop(uint32_t tx, uint32_t rx) {
    drop_tx = tx;
    drop_rx = rx;
}
