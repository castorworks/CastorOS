// uart - 用户态串口输入驱动
//
// 内核只用串口做调试输出（kprintf / console_write）；接收方向完全在这里：
// 认领串口中断，把收到的字节存进缓冲区，通过 IPC 交给读者。
// 读者有两个：终端的主人（命令行）和它指定的前台进程，协议见 <console.h>。
// x86 是 16550 (COM1)，通过 I/O 端口访问；arm64 是 PL011 (QEMU virt)，
// 寄存器用 map_device 映射进自己的地址空间。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <names.h>
#include <console.h>

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

// 设备寄存器映射在自己的地址空间里
static volatile uint32_t *regs;

static uint32_t reg_read(uint32_t off) {
    return regs[off / 4];
}

static void reg_write(uint32_t off, uint32_t value) {
    regs[off / 4] = value;
}

static bool hw_init(void) {
    regs = (volatile uint32_t *)map_device(UART_BASE, 0x1000);
    if (regs == MAP_FAILED) {
        return false;
    }
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

static bool hw_init(void) {
    return io_write(UART_BASE + UART_IER, 1, UART_IER_RX) == 0;
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

// 环形缓冲区
#define RING_SIZE 256
struct ring {
    char buf[RING_SIZE];
    uint32_t head;      // 下一个写入位置
    uint32_t tail;      // 下一个读取位置
};

static bool ring_empty(const struct ring *r) {
    return r->head == r->tail;
}

static void ring_put(struct ring *r, char c) {
    uint32_t next = (r->head + 1) % RING_SIZE;
    if (next != r->tail) {      // 满了就丢弃
        r->buf[r->head] = c;
        r->head = next;
    }
}

static char ring_get(struct ring *r) {
    char c = r->buf[r->tail];
    r->tail = (r->tail + 1) % RING_SIZE;
    return c;
}

// 在等输入的读者
struct reader {
    int pid;            // 0 表示没有人在等
    uint64_t deadline;  // 最晚等到什么时候（开机以来的毫秒数），0 表示一直等
};

static int owner = 0;           // 终端的主人（命令行）
static int foreground = 0;      // 主人指定的前台进程，0 表示没有

static struct ring owner_input;         // 给主人的输入
static struct ring program_input;       // 给前台进程的输入
static struct reader owner_reader;
static struct reader program_reader;

// 一个新到的字节该给谁：有前台进程时归它，但 Ctrl-C 总是给主人
static void route(char c) {
    ring_put(foreground != 0 && c != CONSOLE_CTRL_C ? &program_input : &owner_input, c);
}

static void drain_hw(void) {
    hw_irq_clear();
    while (hw_rx_ready()) {
        route(hw_rx_byte());
    }
}

static void reply_value(int pid, long value) {
    struct ipc_msg m = {};
    m.data[0] = (uint64_t)value;
    ipc_reply(pid, &m);
}

// 有数据时应答在等的读者。by_line：一次最多给到一行的结尾（换行或 Ctrl-D）为止，
// 这样程序读完自己要的那几行就退出时，后面的输入还在我们这里，可以还给主人
static void serve(struct reader *reader, struct ring *input, bool by_line) {
    if (reader->pid == 0 || ring_empty(input)) {
        return;
    }
    struct ipc_msg m = {};
    m.label = UART_READ;
    char *out = (char *)&m.data[1];
    uint32_t n = 0;
    while (n < UART_READ_MAX && !ring_empty(input)) {
        char c = ring_get(input);
        out[n++] = c;
        if (by_line && (c == '\n' || c == '\r' || c == CONSOLE_CTRL_D)) {
            break;
        }
    }
    m.data[0] = n;
    ipc_reply(reader->pid, &m);
    reader->pid = 0;
}

// 读者等到时间了就告诉它没有输入。@return 它还要等多少毫秒，0 表示不用为它定时
static uint64_t check_deadline(struct reader *reader, uint64_t now) {
    if (reader->pid == 0 || reader->deadline == 0) {
        return 0;
    }
    if (now >= reader->deadline) {
        reply_value(reader->pid, 0);
        reader->pid = 0;
        return 0;
    }
    return reader->deadline - now;
}

static void set_foreground(int pid) {
    if (pid != 0) {
        // 主人还没读走的输入是敲给这个程序的；Ctrl-C 留给主人
        struct ring rest = {};
        while (!ring_empty(&owner_input)) {
            char c = ring_get(&owner_input);
            ring_put(c == CONSOLE_CTRL_C ? &rest : &program_input, c);
        }
        owner_input = rest;
    } else {
        // 前台进程结束了：它没读完的输入还给主人，在等的读者（多半已经不在了）不再等
        if (program_reader.pid != 0) {
            reply_value(program_reader.pid, -1);
            program_reader.pid = 0;
        }
        owner_input = program_input;
        program_input = {};
    }
    foreground = pid;
}

static void handle_request(const struct ipc_msg *m) {
    int sender = (int)m->sender;
    switch (m->label) {
    case UART_READ: {
        struct reader *reader = sender == owner ? &owner_reader
                              : sender == foreground ? &program_reader : NULL;
        if (!reader) {
            reply_value(sender, -1);
            return;
        }
        reader->pid = sender;
        reader->deadline = m->data[0] ? uptime_ms() + m->data[0] : 0;
        return;
    }
    case UART_ATTACH:
        if (owner != 0 && owner != sender && kill(owner, 0) == 0) {
            reply_value(sender, -1);
            return;
        }
        owner = sender;
        owner_reader.pid = 0;
        set_foreground(0);
        reply_value(sender, 0);
        return;
    case UART_SET_FOREGROUND:
        if (sender != owner) {
            reply_value(sender, -1);
            return;
        }
        set_foreground((int)m->data[0]);
        reply_value(sender, 0);
        return;
    case UART_UNREAD: {
        if (sender != owner || m->data[0] > UART_READ_MAX) {
            reply_value(sender, -1);
            return;
        }
        const char *in = (const char *)&m->data[1];
        for (uint32_t i = 0; i < (uint32_t)m->data[0]; i++) {
            ring_put(in[i] == CONSOLE_CTRL_C ? &owner_input : &program_input, in[i]);
        }
        reply_value(sender, 0);
        return;
    }
    default:
        return;     // 不认识的请求：不应答
    }
}

int main() {
    if (irq_claim(UART_IRQ) != 0) {
        printf("uart: cannot claim IRQ %d\n", UART_IRQ);
        return 1;
    }
    if (!hw_init()) {
        printf("uart: cannot access the device\n");
        return 1;
    }
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
            // IPC_LABEL_TIMER：下面统一检查读者是否超时
        } else {
            handle_request(&m);
        }

        serve(&owner_reader, &owner_input, false);
        serve(&program_reader, &program_input, true);

        // 带着超时在等的读者：到时间了就应答，否则让定时器到时候叫醒我们
        uint64_t now = uptime_ms();
        uint64_t a = check_deadline(&owner_reader, now);
        uint64_t b = check_deadline(&program_reader, now);
        uint64_t next = a != 0 && (b == 0 || a < b) ? a : b;
        if (next != 0) {
            timer_set((uint32_t)next);
        }
    }
}
