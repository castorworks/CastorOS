// nic.cpp - 网卡
//
// 网络服务里碰硬件的是这一层：打开 init 许可给本进程的那块网卡，收发以太网帧。收到的帧
// 交给协议栈的 eth_input（ip.cpp），协议栈用 nic_send 发帧。网卡可以是 virtio-net
// （virtio_net.cpp），PC 上也可以是 Intel 的千兆网卡（e1000.cpp）；这里是它们共用的部分。

#include <syscall.h>
#include <stdio.h>
#include <net.h>
#include "net_internal.h"
#include "nic.h"

static struct nic nic;

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
#if !defined(ARCH_ARM64)
    if (e1000_allowed()) {
        return e1000_open(&nic);
    }
#endif
    return virtio_net_open(&nic);
}

const char *nic_kind(void) {
    return nic.kind;
}

int nic_irq_line(void) {
    return nic.irq;
}

void nic_interrupt(void) {
    nic.interrupt();
}

void nic_send(const uint8_t *frame, size_t len) {
    if (len > FRAME_MAX) {
        return;
    }
    if (drop_tx > 0 && is_tcp_frame_with_payload(frame, len)) {
        drop_tx--;
        return;
    }
    nic.send(frame, len);
}

void nic_receive(uint8_t *frame, size_t len) {
    if (drop_rx > 0 && is_tcp_frame(frame, len)) {
        drop_rx--;
        return;
    }
    eth_input(frame, len);
}

void nic_debug_drop(uint32_t tx, uint32_t rx) {
    drop_tx = tx;
    drop_rx = rx;
}
