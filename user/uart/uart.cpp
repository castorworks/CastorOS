// uart - 用户态串口输入驱动
//
// 内核只用串口做调试输出（kprintf / console_write）；接收方向完全在这里：认领串口中断，
// 把收到的字节交给 console 服务（console_input，见 <console.h>）。终端的输入归谁由它管，
// 本进程只管把字节从硬件里读出来。
// x86 是 16550，通过 I/O 端口访问；arm64 是 PL011，寄存器用 map_device 映射进自己的
// 地址空间。设备在哪里、用哪个中断不写在这里：init 许可给本进程的端口（或设备内存）
// 和中断线就是它的设备（hw_find）。

#include <syscall.h>
#include <stdio.h>
#include <names.h>
#include <console.h>

#if defined(ARCH_ARM64)

#define PL011_DR        0x00
#define PL011_FR        0x18
#define PL011_IMSC      0x38
#define PL011_ICR       0x44
#define PL011_FR_RXFE   (1 << 4)    // 接收 FIFO 空
#define PL011_INT_RX    (1 << 4)
#define PL011_INT_RT    (1 << 6)    // 接收超时

#define UART_HW_KIND    HW_MEMORY

// 设备寄存器映射在自己的地址空间里
static volatile uint32_t *regs;

static uint32_t reg_read(uint32_t off) {
    return regs[off / 4];
}

static void reg_write(uint32_t off, uint32_t value) {
    regs[off / 4] = value;
}

static bool hw_init(const struct hw_range *where) {
    uint64_t page = where->start & ~(uint64_t)0xFFF;
    void *mapped = map_device((uintptr_t)page, 0x1000);
    if (mapped == MAP_FAILED) {
        return false;
    }
    regs = (volatile uint32_t *)((char *)mapped + (where->start - page));
    reg_write(PL011_ICR, 0x7FF);
    reg_write(PL011_IMSC, reg_read(PL011_IMSC) | PL011_INT_RX | PL011_INT_RT);
    return true;
}

static void hw_irq_clear(void) {
    reg_write(PL011_ICR, PL011_INT_RX | PL011_INT_RT);
}

static bool hw_rx_ready(void) {
    return !(reg_read(PL011_FR) & PL011_FR_RXFE);
}

static char hw_rx_byte(void) {
    return (char)(reg_read(PL011_DR) & 0xFF);
}

#else /* i686, x86_64 */

#define UART_RBR        0           // 接收缓冲
#define UART_IER        1           // 中断使能
#define UART_LSR        5           // 线路状态
#define UART_SCR        7           // 暂存寄存器：写什么读回什么，没有别的作用
#define UART_IER_RX     0x01
#define UART_LSR_DR     0x01        // 有数据可读

#define UART_HW_KIND    HW_PORTS

static uint32_t uart_base;          // 寄存器的端口基址

static uint32_t reg_read(uint32_t off) {
    uint32_t v = 0;
    io_read(uart_base + off, 1, &v);
    return v;
}

static bool hw_init(const struct hw_range *where) {
    uart_base = (uint32_t)where->start;
    // 这台机器有串口吗？没有的话端口读回来的不是写进去的（通常全是 1）
    if (io_write(uart_base + UART_SCR, 1, 0x5A) != 0 || reg_read(UART_SCR) != 0x5A ||
        io_write(uart_base + UART_SCR, 1, 0xA5) != 0 || reg_read(UART_SCR) != 0xA5) {
        return false;
    }
    return io_write(uart_base + UART_IER, 1, UART_IER_RX) == 0;
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

/** 读走硬件里所有的字节，交给 console */
static void drain(void) {
    char bytes[CONSOLE_READ_MAX];
    uint32_t n = 0;
    hw_irq_clear();
    while (hw_rx_ready()) {
        bytes[n++] = hw_rx_byte();
        if (n == CONSOLE_READ_MAX) {
            console_input(bytes, n);
            n = 0;
        }
    }
    if (n > 0) {
        console_input(bytes, n);
    }
}

int main() {
    struct hw_range where, irq;
    if (!hw_find(UART_HW_KIND, 0, &where) || !hw_find(HW_IRQ, 0, &irq) || !hw_init(&where)) {
        printf("uart: no serial port\n");
        return 1;
    }
    int uart_irq = (int)irq.start;
    if (irq_claim(uart_irq) != 0) {
        printf("uart: cannot claim IRQ %d\n", uart_irq);
        return 1;
    }
    if (name_register(UART_NAME) != 0) {
        printf("uart: cannot register name\n");
        return 1;
    }
    printf("uart: driver ready (pid %d, irq %d)\n", getpid(), uart_irq);
    drain();        // 认领之前就到了的字节

    struct ipc_msg m;
    for (;;) {
        if (ipc_recv(IPC_ANY, &m) != 0) {
            continue;
        }
        if (m.sender == IPC_KERNEL && m.label == IPC_LABEL_IRQ) {
            drain();
            irq_ack(uart_irq);
        }
        if (m.sender != IPC_KERNEL && m.label == CONSOLE_DEBUG_EXIT) {
            struct ipc_msg done = {};
            ipc_reply((int)m.sender, &done);
            printf("uart: exiting on request (CONSOLE_DEBUG_EXIT)\n");
            exit(1);
        }
        // 别的请求：没有这样的请求，不应答
    }
}
