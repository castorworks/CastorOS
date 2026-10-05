// uart - 用户态串口输入驱动
//
// 内核只用串口做调试输出（kprintf / console_write）；接收方向完全在这里：
// 认领串口中断，把收到的字节存进缓冲区，通过 IPC 交给读者。
// x86 是 16550 (COM1)，arm64 是 PL011 (QEMU virt)。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <names.h>
#include "uart.h"

#if defined(ARCH_ARM64)

#define UART_BASE       0x09000000
#define UART_IRQ        33          // SPI 1

#define PL011_DR        0x00
#define PL011_FR        0x18
#define PL011_IMSC      0x38
#define PL011_ICR       0x44
#define PL011_FR_RXFE   (1 << 4)    // 接收 FIFO 空
#define PL011_INT_RX    (1 << 4)
#define PL011_INT_RT    (1 << 6)    // 接收超时

static uint32_t reg_read(uint32_t off) {
    uint32_t v = 0;
    io_read(UART_BASE + off, 4, &v);
    return v;
}

static void hw_init(void) {
    io_write(UART_BASE + PL011_ICR, 4, 0x7FF);
    io_write(UART_BASE + PL011_IMSC, 4, reg_read(PL011_IMSC) | PL011_INT_RX | PL011_INT_RT);
}

static void hw_irq_clear(void) {
    io_write(UART_BASE + PL011_ICR, 4, PL011_INT_RX | PL011_INT_RT);
}

static bool hw_rx_ready(void) {
    return !(reg_read(PL011_FR) & PL011_FR_RXFE);
}

static char hw_rx_byte(void) {
    return (char)(reg_read(PL011_DR) & 0xFF);
}

#else /* i686, x86_64 */

#define UART_BASE       0x3F8       // COM1
#define UART_IRQ        4

#define UART_RBR        0           // 接收缓冲
#define UART_IER        1           // 中断使能
#define UART_LSR        5           // 线路状态
#define UART_IER_RX     0x01
#define UART_LSR_DR     0x01        // 有数据可读

static uint32_t reg_read(uint32_t off) {
    uint32_t v = 0;
    io_read(UART_BASE + off, 1, &v);
    return v;
}

static void hw_init(void) {
    io_write(UART_BASE + UART_IER, 1, UART_IER_RX);
}

static void hw_irq_clear(void) {
    // 读走数据后 16550 自己撤销中断
}

static bool hw_rx_ready(void) {
    return reg_read(UART_LSR) & UART_LSR_DR;
}

static char hw_rx_byte(void) {
    return (char)reg_read(UART_RBR);
}

#endif

// 接收缓冲区（环形）
#define RX_BUF_SIZE 256
static char rx_buf[RX_BUF_SIZE];
static uint32_t rx_head = 0;    // 下一个写入位置
static uint32_t rx_tail = 0;    // 下一个读取位置

static int waiting_reader = 0;  // 在等输入的读者 PID，0 表示没有

static void drain_hw(void) {
    hw_irq_clear();
    while (hw_rx_ready()) {
        char c = hw_rx_byte();
        uint32_t next = (rx_head + 1) % RX_BUF_SIZE;
        if (next != rx_tail) {      // 满了就丢弃
            rx_buf[rx_head] = c;
            rx_head = next;
        }
    }
}

// 缓冲区里有数据时应答读者
static bool reply_reader(int reader) {
    if (rx_head == rx_tail) {
        return false;
    }
    struct ipc_msg m = {};
    m.label = UART_READ;
    char *out = (char *)&m.data[1];
    uint32_t n = 0;
    while (n < UART_READ_MAX && rx_tail != rx_head) {
        out[n++] = rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
    }
    m.data[0] = n;
    ipc_reply(reader, &m);
    return true;
}

int main() {
    if (irq_claim(UART_IRQ) != 0) {
        printf("uart: cannot claim IRQ %d\n", UART_IRQ);
        return 1;
    }
    hw_init();
    drain_hw();
    if (name_register("uart") != 0) {
        printf("uart: cannot register name\n");
        return 1;
    }
    printf("uart: driver ready (pid %d, irq %d)\n", getpid(), UART_IRQ);

    struct ipc_msg m;
    for (;;) {
        if (ipc_recv(IPC_ANY, &m) != 0) {
            continue;
        }

        if (m.sender == IPC_KERNEL) {
            if (m.label == IPC_LABEL_IRQ) {
                drain_hw();
                irq_ack(UART_IRQ);
            }
        } else if (m.label == UART_READ) {
            if (waiting_reader != 0) {
                struct ipc_msg busy = {};
                busy.label = UART_READ;
                ipc_reply(m.sender, &busy);
            } else {
                waiting_reader = (int)m.sender;
            }
        } else {
            continue;   // 不认识的请求：不应答
        }

        if (waiting_reader != 0 && reply_reader(waiting_reader)) {
            waiting_reader = 0;
        }
    }
}
