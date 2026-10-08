// virtio-net 网卡
//
// 访问寄存器、队列这些 virtio 的公共部分在 user/lib 的 virtio 里；这里只有网卡自己的部分：
// 两个队列，一个收一个发，每个帧前面带一个 10 字节的头。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <net.h>
#include <virtio.h>
#include "net_internal.h"
#include "nic.h"

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

static bool device_init(void) {
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

static void virtio_net_send(const uint8_t *frame, size_t len) {
    uint32_t done;
    while (virtq_pop_used(&txq, &done, NULL)) {
        tx_free[tx_free_count++] = (uint16_t)done;
    }
    if (tx_free_count == 0) {
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
            nic_receive(frame, frame_len);
        }
        virtq_submit(&rxq, (uint16_t)i);
        any = true;
    }
    if (any) {
        virtio_notify(&nic, &rxq);
    }
}

static void virtio_net_interrupt(void) {
    virtio_irq_ack(&nic);
    nic_poll();
    irq_ack(nic.irq);
}

bool virtio_net_open(struct nic *out) {
    if (!device_init()) {
        return false;
    }
    out->kind = "virtio-net";
    out->irq = nic.irq;
    out->send = virtio_net_send;
    out->interrupt = virtio_net_interrupt;
    return true;
}
