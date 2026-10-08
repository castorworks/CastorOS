// EHCI：USB 2.0 的主控制器
//
// USB 上设备从不主动说话，每一个包都是主机发起的。主机这边由控制器代劳：驱动把要做的
// 传输写成内存里的数据结构，控制器自己照着去收发，做完把结果写回同一块内存。
//
// 两种数据结构（都要放在设备看得到的内存里，互相用物理地址指着）：
//   - 队列头（QH）：一个设备的一个端点——地址、端点号、最大包长。所有队列头连成一个环，
//     控制器一圈一圈地走（“异步调度”）。
//   - 传输描述符（qTD）：一段要收或发的数据——方向、长度、缓冲区的物理地址。挂在队列头
//     下面排成一串；控制器做完一个，清掉它的 Active 位，接着做下一个。
// 所以一次传输就是：填好 qTD，挂到端点的队列头上，等最后一个 qTD 的 Active 位被清掉。
// 控制器做完时发中断；等的时候另外每 10ms 自己看一眼，中断没来也不会一直等下去。
//
// 寄存器在设备内存里（map_device），分两段：前面是只读的“能力”，CAPLENGTH 说后面的
// “操作寄存器”从哪里开始。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include "disk.h"
#include "ehci.h"

#define PAGE_SIZE 4096

// 能力寄存器
#define CAP_CAPLENGTH   0x00    // 8 位：操作寄存器的偏移
#define CAP_HCSPARAMS   0x04
#define HCSPARAMS_PORTS         0x0F
#define HCSPARAMS_PORT_POWER    (1u << 4)   // 端口的供电要驱动来开

// 操作寄存器（相对 CAPLENGTH）
#define OP_USBCMD       0x00
#define OP_USBSTS       0x04
#define OP_USBINTR      0x08
#define OP_CTRLDSSEGMENT 0x10   // 64 位控制器上，所有数据结构地址的高 32 位
#define OP_ASYNCLISTADDR 0x18   // 队列头的环从哪里开始
#define OP_CONFIGFLAG   0x40    // 写 1：所有端口归本控制器管（而不是低速的伙伴控制器）
#define OP_PORTSC       0x44    // 每个端口一个

#define CMD_RUN             (1u << 0)
#define CMD_RESET           (1u << 1)
#define CMD_ASYNC_ENABLE    (1u << 5)
#define CMD_IRQ_THRESHOLD_1 (1u << 16)  // 做完就发中断，最多攒一个微帧（125 微秒）

#define STS_INTERRUPT       (1u << 0)   // 一个要求中断的 qTD 做完了
#define STS_ERROR           (1u << 1)   // 一次传输出错了
#define STS_HALTED          (1u << 12)
#define STS_ASYNC_RUNNING   (1u << 15)
#define STS_ALL_INTERRUPTS  0x3Fu       // 写 1 清除

#define PORT_CONNECTED      (1u << 0)
#define PORT_ENABLED        (1u << 2)
#define PORT_RESET          (1u << 8)
#define PORT_POWER          (1u << 12)
#define PORT_OWNER          (1u << 13)  // 1：交给伙伴控制器（上面是低速/全速设备）
#define PORT_CHANGE_BITS    ((1u << 1) | (1u << 3) | (1u << 5))    // 写 1 清除：写回时要留心

// 指针字段的低位
#define LINK_TERMINATE  1u          // 后面没有了
#define LINK_QH         (1u << 1)   // 指着的是一个队列头

// qTD 的 token
#define QTD_ACTIVE          (1u << 7)
#define QTD_HALTED          (1u << 6)   // 出错了（或者设备把端点挂起了），队列停在这里
#define QTD_PID_OUT         (0u << 8)
#define QTD_PID_IN          (1u << 8)
#define QTD_PID_SETUP       (2u << 8)
#define QTD_ERRORS_3        (3u << 10)  // 一个包最多重试 3 次
#define QTD_IRQ             (1u << 15)  // 做完时发中断
#define QTD_LENGTH_SHIFT    16
#define QTD_LENGTH_MASK     0x7FFFu
#define QTD_TOGGLE          (1u << 31)  // 这一段的第一个包是 DATA0 还是 DATA1

// 队列头的端点特征
#define QH_ENDPOINT_SHIFT   8
#define QH_HIGH_SPEED       (2u << 12)
#define QH_TOGGLE_FROM_QTD  (1u << 14)  // DATA0/DATA1 由每个 qTD 说了算（控制传输），否则控制器自己接着数
#define QH_HEAD             (1u << 15)  // 环的头：控制器靠它知道走完了一圈
#define QH_MAX_PACKET_SHIFT 16
#define QH_MULT_1           (1u << 30)  // 每个微帧一次事务（高速端点必须非 0）

// 两种结构都按 64 位控制器的大小留着高 32 位的字段（恒为 0）：32 位的控制器不看它们
struct qtd {
    uint32_t next;          // 下一个 qTD
    uint32_t alt_next;      // 收到的比要的少（短包）时改走这里
    uint32_t token;
    uint32_t buffer[5];     // 数据所在的页，最多 5 页；第一项可以带页内偏移
    uint32_t buffer_high[5];
};

struct qh {
    uint32_t link;          // 环里的下一个队列头
    uint32_t characteristics;
    uint32_t capabilities;
    uint32_t current;       // 控制器正在做的那个 qTD
    struct qtd overlay;     // 控制器的工作区：正在做的 qTD 抄在这里
};

// 设备看得到的内存（dma_alloc）怎么用：第 0 页放队列头和 qTD，第 1 页是数据缓冲区，
// 第 2 页放控制传输的 8 字节请求
#define QH_SLOT         128
#define QTD_AREA        1024
#define QTD_SLOT        64
enum { QH_RING_HEAD, QH_CONTROL, QH_BULK_IN, QH_BULK_OUT };
#define MAX_QTDS        3       // 一次控制传输：请求、数据、状态

static char *dma;
static uint64_t dma_phys;

static volatile uint8_t *cap;       // 能力寄存器
static volatile uint32_t *op;       // 操作寄存器
static int port_count;
static int irq = -1;

static volatile struct qh *qh_at(int index) {
    return (volatile struct qh *)(dma + index * QH_SLOT);
}

static uint32_t qh_phys(int index) {
    return (uint32_t)(dma_phys + (uint64_t)index * QH_SLOT);
}

static volatile struct qtd *qtd_at(int index) {
    return (volatile struct qtd *)(dma + QTD_AREA + index * QTD_SLOT);
}

static uint32_t qtd_phys(int index) {
    return (uint32_t)(dma_phys + QTD_AREA + (uint64_t)index * QTD_SLOT);
}

static char *data_page(void) { return dma + PAGE_SIZE; }
static uint32_t data_phys(void) { return (uint32_t)(dma_phys + PAGE_SIZE); }
static char *setup_page(void) { return dma + 2 * PAGE_SIZE; }
static uint32_t setup_phys(void) { return (uint32_t)(dma_phys + 2 * PAGE_SIZE); }

static uint32_t op_read(uint32_t reg) { return op[reg / 4]; }
static void op_write(uint32_t reg, uint32_t value) { op[reg / 4] = value; }

/** 等到操作寄存器 reg 里 mask 这些位等于 value，最多 ms 毫秒 */
static bool wait_op(uint32_t reg, uint32_t mask, uint32_t value, uint32_t ms) {
    uint64_t deadline = uptime_ms() + ms;
    while ((op_read(reg) & mask) != value) {
        if (uptime_ms() >= deadline) {
            return false;
        }
        usleep(1000);
    }
    return true;
}

int ehci_irq(void) {
    return irq;
}

static void on_irq(void) {
    op_write(OP_USBSTS, op_read(OP_USBSTS) & STS_ALL_INTERRUPTS);
    irq_ack(irq);
}

void ehci_close(void) {
    op_write(OP_USBINTR, 0);
    op_write(OP_USBCMD, op_read(OP_USBCMD) & ~(CMD_RUN | CMD_ASYNC_ENABLE));
    op_write(OP_USBSTS, op_read(OP_USBSTS) & STS_ALL_INTERRUPTS);
}

// ============================================================================
// 队列头和传输
// ============================================================================

/** 让控制器停下 / 接着走队列头的环。改一个队列头的端点特征之前要先停：控制器可能正读着它 */
static void async_enable(bool on) {
    uint32_t command = op_read(OP_USBCMD);
    op_write(OP_USBCMD, on ? command | CMD_ASYNC_ENABLE : command & ~CMD_ASYNC_ENABLE);
    wait_op(OP_USBSTS, STS_ASYNC_RUNNING, on ? STS_ASYNC_RUNNING : 0, 100);
}

/** 把一个队列头设成“地址 address 的设备的 endpoint 号端点，上面没有要做的事” */
static void qh_setup(int index, uint8_t address, uint8_t endpoint, uint16_t max_packet, bool toggle_from_qtd) {
    volatile struct qh *q = qh_at(index);
    q->characteristics = address | ((uint32_t)endpoint << QH_ENDPOINT_SHIFT) | QH_HIGH_SPEED |
                         (toggle_from_qtd ? QH_TOGGLE_FROM_QTD : 0) |
                         ((uint32_t)max_packet << QH_MAX_PACKET_SHIFT);
    q->capabilities = QH_MULT_1;
    q->current = 0;
    q->overlay.next = LINK_TERMINATE;
    q->overlay.alt_next = LINK_TERMINATE;
    q->overlay.token = 0;
}

/** 填一个 qTD：收或发 length 字节，数据在物理地址 buffer（不跨页）；它后面接第 next 个，-1 是最后一个 */
static void qtd_fill(int index, uint32_t pid, bool toggle, uint32_t buffer, uint32_t length, int next) {
    volatile struct qtd *t = qtd_at(index);
    memset((void *)t, 0, sizeof(*t));
    t->next = next >= 0 ? qtd_phys(next) : LINK_TERMINATE;
    t->alt_next = LINK_TERMINATE;
    t->buffer[0] = buffer;
    t->token = QTD_ACTIVE | pid | QTD_ERRORS_3 | (length << QTD_LENGTH_SHIFT) |
               (toggle ? QTD_TOGGLE : 0) | (next < 0 ? QTD_IRQ : 0);
}

/** 第 index 个 qTD 实际传了多少字节：要的减去控制器写回来的“还剩多少” */
static uint32_t qtd_done(int index, uint32_t length) {
    return length - ((qtd_at(index)->token >> QTD_LENGTH_SHIFT) & QTD_LENGTH_MASK);
}

/**
 * 把填好的前 count 个 qTD 挂到队列头上，等控制器做完。
 * @return 全部做完而且没有出错。出错或超时的话队列头被清理干净，留着“挂起”的样子等上面处理
 */
static bool run(int qh_index, int count) {
    volatile struct qh *q = qh_at(qh_index);
    // 控制器走到这个队列头时发现它闲着，就从 overlay.next 取下一个 qTD 开始做
    q->overlay.next = qtd_phys(0);

    uint64_t deadline = uptime_ms() + 5000;
    bool failed = false;
    for (;;) {
        for (int i = 0; i < count; i++) {
            failed = failed || (qtd_at(i)->token & QTD_HALTED);
        }
        if (failed || !(qtd_at(count - 1)->token & QTD_ACTIVE) || uptime_ms() >= deadline) {
            break;
        }
        disk_wait(10);
    }
    if (!failed && !(qtd_at(count - 1)->token & QTD_ACTIVE)) {
        return true;
    }

    // 没做完：把队列头从这几个 qTD 上摘下来。数据的 DATA0/DATA1 留着控制器数到的地方
    async_enable(false);
    q->overlay.next = LINK_TERMINATE;
    q->overlay.alt_next = LINK_TERMINATE;
    q->overlay.token &= QTD_TOGGLE;
    q->current = 0;
    async_enable(true);
    return false;
}

void ehci_set_address(uint8_t address) {
    async_enable(false);
    qh_setup(QH_CONTROL, address, 0, USB_EP0_MAX_PACKET, true);
    async_enable(true);
}

long ehci_control(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                  void *data, uint16_t length) {
    if (length > PAGE_SIZE) {
        return -1;
    }
    bool in = (request_type & 0x80) != 0;
    // 8 字节的请求，小端
    uint8_t *setup = (uint8_t *)setup_page();
    setup[0] = request_type;
    setup[1] = request;
    setup[2] = (uint8_t)value;
    setup[3] = (uint8_t)(value >> 8);
    setup[4] = (uint8_t)index;
    setup[5] = (uint8_t)(index >> 8);
    setup[6] = (uint8_t)length;
    setup[7] = (uint8_t)(length >> 8);
    if (!in && length > 0) {
        memcpy(data_page(), data, length);
    }

    // 请求总是 DATA0；数据阶段从 DATA1 开始；状态阶段是一个反方向的空包，DATA1
    int count = 0;
    int data_qtd = -1;
    qtd_fill(count++, QTD_PID_SETUP, false, setup_phys(), 8, 1);
    if (length > 0) {
        data_qtd = count;
        qtd_fill(count, in ? QTD_PID_IN : QTD_PID_OUT, true, data_phys(), length, count + 1);
        // 设备给的比要的少也是正常的（描述符比猜的短）：那时直接去做状态阶段
        qtd_at(count)->alt_next = qtd_phys(count + 1);
        count++;
    }
    qtd_fill(count++, length > 0 && in ? QTD_PID_OUT : QTD_PID_IN, true, 0, 0, -1);

    if (!run(QH_CONTROL, count)) {
        return -1;
    }
    uint32_t done = data_qtd >= 0 ? qtd_done(data_qtd, length) : 0;
    if (in && done > 0) {
        memcpy(data, data_page(), done);
    }
    return (long)done;
}

void ehci_bulk_setup(uint8_t address, uint8_t endpoint_in, uint8_t endpoint_out, uint16_t max_packet) {
    async_enable(false);
    qh_setup(QH_BULK_IN, address, endpoint_in, max_packet, false);
    qh_setup(QH_BULK_OUT, address, endpoint_out, max_packet, false);
    async_enable(true);
}

long ehci_bulk(bool in, void *data, uint32_t length) {
    if (length == 0 || length > PAGE_SIZE) {
        return -1;
    }
    if (!in) {
        memcpy(data_page(), data, length);
    }
    // DATA0/DATA1 由控制器接着上一次数（队列头里记着），这里填的那一位不起作用
    qtd_fill(0, in ? QTD_PID_IN : QTD_PID_OUT, false, data_phys(), length, -1);
    if (!run(in ? QH_BULK_IN : QH_BULK_OUT, 1)) {
        return -1;
    }
    uint32_t done = qtd_done(0, length);
    if (in && done > 0) {
        memcpy(data, data_page(), done);
    }
    return (long)done;
}

void ehci_bulk_reset(bool in) {
    volatile struct qh *q = qh_at(in ? QH_BULK_IN : QH_BULK_OUT);
    async_enable(false);
    q->overlay.next = LINK_TERMINATE;
    q->overlay.alt_next = LINK_TERMINATE;
    q->overlay.token = 0;       // 解除挂起之后设备从 DATA0 重新开始，这边也是
    q->current = 0;
    async_enable(true);
}

// ============================================================================
// 端口
// ============================================================================

int ehci_ports(void) {
    return port_count;
}

static uint32_t port_read(int port) {
    return op_read(OP_PORTSC + (uint32_t)port * 4);
}

/** 写端口寄存器。几个“有变化”位是写 1 清除的：写回读到的值会把它们清掉，所以先屏蔽 */
static void port_write(int port, uint32_t value) {
    op_write(OP_PORTSC + (uint32_t)port * 4, value & ~PORT_CHANGE_BITS);
}

void ehci_port_release(int port) {
    port_write(port, port_read(port) | PORT_OWNER);
}

bool ehci_port_reset(int port) {
    if (!(port_read(port) & PORT_CONNECTED)) {
        return false;
    }
    // 复位信号要保持至少 50ms；撤掉之后控制器和设备协商速度，是高速设备的话端口就被打开
    port_write(port, (port_read(port) & ~PORT_ENABLED) | PORT_RESET);
    usleep(50000);
    port_write(port, port_read(port) & ~PORT_RESET);
    if (!wait_op(OP_PORTSC + (uint32_t)port * 4, PORT_RESET, 0, 100)) {
        return false;
    }
    usleep(20000);          // 设备复位之后要缓一下才能回答
    uint32_t status = port_read(port);
    if (!(status & PORT_CONNECTED)) {
        return false;
    }
    if (!(status & PORT_ENABLED)) {
        // 低速或全速设备（键盘、鼠标）：本控制器不会说它们的话，交给伙伴控制器
        ehci_port_release(port);
        return false;
    }
    return true;
}

// ============================================================================
// 启动
// ============================================================================

bool ehci_open(void) {
    struct hw_range mem, line;
    if (!hw_find(HW_MEMORY, 0, &mem)) {
        return false;
    }
    void *regs = map_device((uintptr_t)(mem.start & ~(uint64_t)(PAGE_SIZE - 1)), PAGE_SIZE);
    if (regs == MAP_FAILED) {
        return false;
    }
    cap = (volatile uint8_t *)regs + (mem.start & (PAGE_SIZE - 1));
    op = (volatile uint32_t *)(cap + cap[CAP_CAPLENGTH]);
    uint32_t hcsparams = *(volatile uint32_t *)(cap + CAP_HCSPARAMS);
    port_count = (int)(hcsparams & HCSPARAMS_PORTS);

    dma = (char *)dma_alloc(3 * PAGE_SIZE, &dma_phys);
    if (dma == MAP_FAILED || dma_phys + 3 * PAGE_SIZE > 0xFFFFFFFFull) {
        printf("blk: no memory the USB controller can reach\n");
        return false;
    }

    // 停下来，复位：不管固件把它留成了什么样
    op_write(OP_USBCMD, op_read(OP_USBCMD) & ~CMD_RUN);
    wait_op(OP_USBSTS, STS_HALTED, STS_HALTED, 100);
    op_write(OP_USBCMD, CMD_RESET);
    if (!wait_op(OP_USBCMD, CMD_RESET, 0, 500)) {
        printf("blk: the USB controller does not come out of reset\n");
        return false;
    }

    // 队列头的环：一个空的头，后面是控制端点和两个批量端点。它们一直在环里，没有事做时
    // 控制器只是路过
    qh_setup(QH_RING_HEAD, 0, 0, USB_EP0_MAX_PACKET, false);
    qh_at(QH_RING_HEAD)->characteristics |= QH_HEAD;
    qh_setup(QH_CONTROL, 0, 0, USB_EP0_MAX_PACKET, true);
    qh_setup(QH_BULK_IN, 0, 0, USB_EP0_MAX_PACKET, false);
    qh_setup(QH_BULK_OUT, 0, 0, USB_EP0_MAX_PACKET, false);
    for (int i = QH_RING_HEAD; i <= QH_BULK_OUT; i++) {
        qh_at(i)->link = qh_phys(i == QH_BULK_OUT ? QH_RING_HEAD : i + 1) | LINK_QH;
    }

    // 中断线在许可表里排在 IDE 通道的后面（如果有 IDE 通道的话）
    if (hw_find(HW_IRQ, ata_allowed() ? 1 : 0, &line) && irq_claim((int)line.start) == 0) {
        irq = (int)line.start;
        disk_on_irq(irq, on_irq);
    }

    op_write(OP_CTRLDSSEGMENT, 0);
    op_write(OP_ASYNCLISTADDR, qh_phys(QH_RING_HEAD));
    op_write(OP_USBINTR, irq >= 0 ? STS_INTERRUPT | STS_ERROR : 0);
    op_write(OP_USBCMD, CMD_RUN | CMD_ASYNC_ENABLE | CMD_IRQ_THRESHOLD_1);
    op_write(OP_CONFIGFLAG, 1);
    if (!wait_op(OP_USBSTS, STS_HALTED | STS_ASYNC_RUNNING, STS_ASYNC_RUNNING, 100)) {
        printf("blk: the USB controller does not start\n");
        return false;
    }

    if (hcsparams & HCSPARAMS_PORT_POWER) {
        for (int p = 0; p < port_count; p++) {
            port_write(p, port_read(p) | PORT_POWER);
        }
    }
    usleep(100000);         // 上电之后设备要一点时间才出现在端口上
    return true;
}
