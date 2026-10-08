// UHCI：USB 1.1 的主控制器
//
// 和 EHCI（user/blk/ehci.cpp）一样，驱动把要做的传输写成内存里的数据结构，控制器照着去
// 收发，做完把结果写回去。不同的是时间的安排：UHCI 把时间切成 1 毫秒一“帧”，有一张
// 1024 项的“帧表”，每一帧从表里的一项出发，沿着指针走一遍，走到的传输各做一次。
//
// 两种数据结构（都在设备看得到的内存里，互相用物理地址指着）：
//   - 传输描述符（TD）：一个包——方向、设备地址、端点号、长度、缓冲区的物理地址。
//     控制器做完一个，清掉它的 Active 位，写上实际传了多少。
//   - 队列头（QH）：一串 TD 的头。横着的指针指向下一个队列头，竖着的指针指向自己这一串里
//     下一个要做的 TD；控制器每做完一个 TD 就把竖着的指针往下挪一格。
//
// 这里的安排：每个端口一个队列头（定期问那个端口上的设备有没有数据），加上一个大家共用的
// 控制传输的队列头，横着连成一串。帧表里每 8 项有一项从串的开头出发，其余的直接从控制
// 传输的队列头出发：键盘每 8 毫秒被问一次，控制传输每毫秒都能往前走。
//
// 寄存器是一段 I/O 端口，init 许可给本进程的就是（每个控制器一段）。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include "uhci.h"

#define PAGE_SIZE 4096

// 寄存器（相对这段端口的开头）
#define REG_USBCMD      0x00    // 16 位
#define REG_USBSTS      0x02    // 16 位
#define REG_USBINTR     0x04    // 16 位
#define REG_FRNUM       0x06    // 16 位：现在是第几帧
#define REG_FRBASEADD   0x08    // 32 位：帧表的物理地址
#define REG_PORTSC      0x10    // 16 位，每个端口一个

#define CMD_RUN             (1u << 0)
#define CMD_RESET           (1u << 1)   // 复位控制器
#define CMD_GLOBAL_RESET    (1u << 2)   // 在总线上发复位信号：上面的设备全部回到初始状态
#define CMD_CONFIGURED      (1u << 6)
#define CMD_MAX_PACKET_64   (1u << 7)

#define STS_ALL             0x3Fu       // 写 1 清除

#define INTR_TIMEOUT        (1u << 0)   // 一次传输出错了
#define INTR_COMPLETE       (1u << 2)   // 一个要求中断的 TD 做完了
#define INTR_SHORT_PACKET   (1u << 3)

#define PORT_CONNECTED      (1u << 0)
#define PORT_CONNECT_CHANGE (1u << 1)   // 写 1 清除
#define PORT_ENABLED        (1u << 2)
#define PORT_ENABLE_CHANGE  (1u << 3)   // 写 1 清除
#define PORT_LOW_SPEED      (1u << 8)
#define PORT_RESET          (1u << 9)
#define PORT_SUSPEND        (1u << 12)
/** 写端口寄存器时要原样写回去的位（两个“有变化”位写回去会被清掉，所以不在里面） */
#define PORT_KEEP           (PORT_ENABLED | PORT_RESET | PORT_SUSPEND)

// 指针字段的低位
#define LINK_TERMINATE      1u          // 后面没有了
#define LINK_QH             (1u << 1)   // 指着的是一个队列头
#define LINK_DEPTH_FIRST    (1u << 2)   // 做完这个 TD 接着做它指着的，而不是去下一个队列头

// TD 的状态字
#define TD_ACTUAL_MASK      0x7FFu      // 实际传了多少字节，减 1（0x7FF 是 0 字节）
#define TD_ACTIVE           (1u << 23)
// 挂起、缓冲区错、包太长、超时或校验错、位填充错。中间空着的第 19 位是 NAK（设备说“现在
// 没有”）：那不是错，TD 保持 Active，控制器下次接着问
#define TD_ERRORS           ((0x7u << 20) | (0x3u << 17))
#define TD_IOC              (1u << 24)  // 做完时发中断
#define TD_LOW_SPEED        (1u << 26)
#define TD_ERRORS_3         (3u << 27)  // 一个包最多重试 3 次
#define TD_SHORT_PACKET     (1u << 29)  // 收到的比要的少时停在这里（见 uhci_control）

// TD 的 token
#define PID_SETUP           0x2Du
#define PID_IN              0x69u
#define PID_OUT             0xE1u
#define TOKEN_ADDRESS_SHIFT 8
#define TOKEN_ENDPOINT_SHIFT 15
#define TOKEN_TOGGLE        (1u << 19)  // 这个包是 DATA0 还是 DATA1
#define TOKEN_LENGTH_SHIFT  21          // 要传多少字节，减 1

// 控制器只看前 16 个字节（TD）或前 8 个字节（QH），后面是为了对齐到 16 字节的倍数
struct td {
    uint32_t link;
    uint32_t status;
    uint32_t token;
    uint32_t buffer;
    uint32_t unused[4];
};

struct qh {
    uint32_t link;          // 横着的：下一个队列头
    uint32_t element;       // 竖着的：这一串里下一个要做的 TD
    uint32_t unused[2];
};

// 每个控制器两页设备看得到的内存（dma_alloc）：第 0 页是帧表，第 1 页放其余的东西
#define FRAMES              1024
#define INTERRUPT_EVERY     8           // 每 8 帧问一次中断端点
enum { QH_CONTROL, QH_INTERRUPT };      // 之后每个端口一个
#define TD_AREA             64          // 第 1 页里 TD 从这里开始
enum { TD_INTERRUPT = 0, TD_CONTROL = UHCI_PORTS };     // 每个端口一个，之后是控制传输的
#define CONTROL_TDS         (2 + UHCI_CONTROL_MAX / 8)  // 请求、数据（每个包至少 8 字节）、状态
#define SETUP_AREA          1280
#define INTERRUPT_AREA      1344        // 每个端口 UHCI_INTERRUPT_MAX 字节
#define CONTROL_AREA        2048
static_assert(TD_AREA + (TD_CONTROL + CONTROL_TDS) * sizeof(struct td) <= SETUP_AREA);
static_assert(INTERRUPT_AREA + UHCI_PORTS * UHCI_INTERRUPT_MAX <= CONTROL_AREA);
static_assert(CONTROL_AREA + UHCI_CONTROL_MAX <= PAGE_SIZE);

struct controller {
    uint32_t io;            // 寄存器的第一个端口
    char *page;             // 第 1 页
    uint32_t page_phys;
    // 每个端口上定期问的那个端点
    struct {
        bool on;
        bool toggle;        // 下一个包是 DATA0 还是 DATA1：设备每给一次数据换一次
        uint32_t flags;
        uint32_t token;
        uint32_t length;
    } interrupt[UHCI_PORTS];
};

static struct controller controllers[UHCI_MAX_CONTROLLERS];
static int controller_count;

static int irq_count;       // 认领了几条中断线（几个控制器可以共用一条）

static uint16_t reg_read(const struct controller *c, uint32_t reg) {
    uint32_t v = 0;
    io_read(c->io + reg, 2, &v);
    return (uint16_t)v;
}

static void reg_write(const struct controller *c, uint32_t reg, uint16_t value) {
    io_write(c->io + reg, 2, value);
}

static volatile struct qh *qh_at(const struct controller *c, int index) {
    return (volatile struct qh *)(c->page + index * sizeof(struct qh));
}

static uint32_t qh_phys(const struct controller *c, int index) {
    return c->page_phys + (uint32_t)index * sizeof(struct qh);
}

static volatile struct td *td_at(const struct controller *c, int index) {
    return (volatile struct td *)(c->page + TD_AREA + index * sizeof(struct td));
}

static uint32_t td_phys(const struct controller *c, int index) {
    return c->page_phys + TD_AREA + (uint32_t)index * sizeof(struct td);
}

/** 填一个 TD：传 length 字节，数据在物理地址 buffer。它后面没有别的 TD（要接的话再改 link） */
static void td_fill(const struct controller *c, int index, uint32_t flags, uint32_t token, bool toggle,
                    uint32_t buffer, uint32_t length) {
    volatile struct td *t = td_at(c, index);
    t->link = LINK_TERMINATE;
    t->token = token | (toggle ? TOKEN_TOGGLE : 0) | (((length - 1) & 0x7FF) << TOKEN_LENGTH_SHIFT);
    t->buffer = buffer;
    t->status = TD_ACTIVE | TD_ERRORS_3 | flags;
}

/** 状态字里写着的“实际传了多少字节” */
static uint32_t td_actual(uint32_t status) {
    return (status + 1) & TD_ACTUAL_MASK;
}

// ============================================================================
// 中断和等待
// ============================================================================

void uhci_irq(int irq) {
    // 这条线上是哪个控制器发的不用分辨：都看一眼，有事的把它的状态位清掉（中断就撤销了）
    for (int i = 0; i < controller_count; i++) {
        uint16_t status = reg_read(&controllers[i], REG_USBSTS) & STS_ALL;
        if (status != 0) {
            reg_write(&controllers[i], REG_USBSTS, status);
        }
    }
    irq_ack(irq);
}

bool uhci_has_irq(void) {
    return irq_count > 0;
}

/** 等一条内核消息（中断或定时器），最多 ms 毫秒；是中断就应答 */
static void wait(uint32_t ms) {
    timer_set(ms);
    struct ipc_msg m;
    if (ipc_recv(IPC_FROM_KERNEL, &m) == 0 && m.label == IPC_LABEL_IRQ) {
        uhci_irq((int)m.data[0]);
    }
    timer_set(0);
}

void uhci_sleep(uint32_t ms) {
    uint64_t deadline = uptime_ms() + ms;
    for (uint64_t now = uptime_ms(); now < deadline; now = uptime_ms()) {
        wait((uint32_t)(deadline - now));
    }
}

// ============================================================================
// 端口
// ============================================================================

static uint16_t port_read(const struct controller *c, int port) {
    return reg_read(c, REG_PORTSC + (uint32_t)port * 2);
}

/** 改端口寄存器：清掉 clear 里的位，置上 set 里的位（“有变化”位放在 set 里就是清除它） */
static void port_update(const struct controller *c, int port, uint16_t clear, uint16_t set) {
    uint16_t value = (uint16_t)((port_read(c, port) & PORT_KEEP & ~clear) | set);
    reg_write(c, REG_PORTSC + (uint32_t)port * 2, value);
}

bool uhci_port_connected(int controller, int port) {
    return port_read(&controllers[controller], port) & PORT_CONNECTED;
}

bool uhci_port_changed(int controller, int port) {
    const struct controller *c = &controllers[controller];
    if (!(port_read(c, port) & PORT_CONNECT_CHANGE)) {
        return false;
    }
    port_update(c, port, 0, PORT_CONNECT_CHANGE);
    return true;
}

bool uhci_port_reset(int controller, int port, bool *low_speed) {
    const struct controller *c = &controllers[controller];
    if (!(port_read(c, port) & PORT_CONNECTED)) {
        return false;
    }
    // 复位信号要保持至少 50ms。撤掉之后端口不会自己打开，要写一次；设备太久听不到主机的
    // 动静会睡过去，所以撤掉之后马上写，然后才是等
    port_update(c, port, 0, PORT_RESET);
    uhci_sleep(50);
    port_update(c, port, PORT_RESET, 0);
    for (int i = 0; i < 10; i++) {
        port_update(c, port, 0, PORT_ENABLED | PORT_CONNECT_CHANGE | PORT_ENABLE_CHANGE);
        if (port_read(c, port) & PORT_ENABLED) {
            break;
        }
        uhci_sleep(1);
    }
    uhci_sleep(20);         // 设备复位之后要缓一下才能回答
    // 复位本身可能让端口记下一次“有变化”：那不是插拔，清掉
    port_update(c, port, 0, PORT_CONNECT_CHANGE | PORT_ENABLE_CHANGE);
    uint16_t status = port_read(c, port);
    if (!(status & PORT_CONNECTED) || !(status & PORT_ENABLED)) {
        return false;
    }
    *low_speed = status & PORT_LOW_SPEED;
    return true;
}

void uhci_port_disable(int controller, int port) {
    port_update(&controllers[controller], port, PORT_ENABLED, 0);
}

// ============================================================================
// 控制传输
// ============================================================================

long uhci_control(const struct uhci_device *dev, uint8_t request_type, uint8_t request, uint16_t value,
                  uint16_t index, void *data, uint16_t length) {
    const struct controller *c = &controllers[dev->controller];
    if (length > UHCI_CONTROL_MAX) {
        return -1;
    }
    bool in = (request_type & 0x80) != 0;
    // 8 字节的请求，小端
    uint8_t *setup = (uint8_t *)c->page + SETUP_AREA;
    setup[0] = request_type;
    setup[1] = request;
    setup[2] = (uint8_t)value;
    setup[3] = (uint8_t)(value >> 8);
    setup[4] = (uint8_t)index;
    setup[5] = (uint8_t)(index >> 8);
    setup[6] = (uint8_t)length;
    setup[7] = (uint8_t)(length >> 8);
    if (!in && length > 0) {
        memcpy(c->page + CONTROL_AREA, data, length);
    }

    // 一个包一个 TD。请求总是 DATA0；数据一包一包地传，从 DATA1 开始交替；状态阶段是一个
    // 反方向的空包，DATA1
    uint32_t speed = dev->low_speed ? TD_LOW_SPEED : 0;
    uint32_t target = (uint32_t)dev->address << TOKEN_ADDRESS_SHIFT;
    uint32_t max_packet = dev->max_packet >= 8 ? dev->max_packet : 8;
    int count = 0;
    td_fill(c, TD_CONTROL + count++, speed, PID_SETUP | target, false, c->page_phys + SETUP_AREA, 8);
    bool toggle = true;
    for (uint32_t off = 0; off < length; off += max_packet) {
        uint32_t chunk = length - off < max_packet ? length - off : max_packet;
        td_fill(c, TD_CONTROL + count++, speed | (in ? TD_SHORT_PACKET : 0), (in ? PID_IN : PID_OUT) | target,
                toggle, c->page_phys + CONTROL_AREA + off, chunk);
        toggle = !toggle;
    }
    int status_td = count;
    td_fill(c, TD_CONTROL + count++, speed | TD_IOC, (length > 0 && in ? PID_OUT : PID_IN) | target, true, 0, 0);
    for (int i = 0; i + 1 < count; i++) {
        td_at(c, TD_CONTROL + i)->link = td_phys(c, TD_CONTROL + i + 1) | LINK_DEPTH_FIRST;
    }
    volatile struct qh *q = qh_at(c, QH_CONTROL);
    q->element = td_phys(c, TD_CONTROL);

    // 等控制器一个一个做完。做完时它发中断；另外每 10ms 自己看一眼
    uint64_t deadline = uptime_ms() + 1000;
    bool skipped = false;
    for (;;) {
        uint32_t done = 0;
        bool failed = false;
        int i = 0;
        while (i < count) {
            uint32_t status = td_at(c, TD_CONTROL + i)->status;
            if (status & TD_ACTIVE) {
                break;
            }
            if (status & TD_ERRORS) {
                failed = true;
                break;
            }
            if (i > 0 && i < status_td) {
                uint32_t wanted = ((td_at(c, TD_CONTROL + i)->token >> TOKEN_LENGTH_SHIFT) + 1) & 0x7FF;
                done += td_actual(status);
                if (td_actual(status) < wanted) {
                    // 设备给的比要的少（描述符比猜的短）：数据到此为止。控制器停在了这个 TD
                    // 上等我们发话，让它直接去做状态阶段
                    if (!skipped) {
                        q->element = td_phys(c, TD_CONTROL + status_td);
                        skipped = true;
                    }
                    i = status_td;
                    continue;
                }
            }
            i++;
        }
        if (i == count) {
            if (in && done > 0) {
                memcpy(data, c->page + CONTROL_AREA, done);
            }
            return (long)done;
        }
        if (failed || uptime_ms() >= deadline) {
            break;
        }
        wait(10);
    }
    // 没做完：把队列头从这几个 TD 上摘下来，等控制器走过这一帧再回去（它可能正做着其中一个）
    q->element = LINK_TERMINATE;
    uhci_sleep(2);
    return -1;
}

// ============================================================================
// 中断传输
// ============================================================================

/** 把问一次的 TD 填好，挂到这个端口的队列头上 */
static void interrupt_arm(struct controller *c, int port) {
    td_fill(c, TD_INTERRUPT + port, c->interrupt[port].flags | TD_IOC, c->interrupt[port].token,
            c->interrupt[port].toggle, c->page_phys + INTERRUPT_AREA + (uint32_t)port * UHCI_INTERRUPT_MAX,
            c->interrupt[port].length);
    qh_at(c, QH_INTERRUPT + port)->element = td_phys(c, TD_INTERRUPT + port);
}

void uhci_interrupt_start(const struct uhci_device *dev, int port, uint8_t endpoint, uint16_t max_packet) {
    struct controller *c = &controllers[dev->controller];
    c->interrupt[port].on = true;
    c->interrupt[port].toggle = false;      // 配置好的端点从 DATA0 开始
    c->interrupt[port].flags = dev->low_speed ? TD_LOW_SPEED : 0;
    c->interrupt[port].token = PID_IN | ((uint32_t)dev->address << TOKEN_ADDRESS_SHIFT) |
                               ((uint32_t)endpoint << TOKEN_ENDPOINT_SHIFT);
    c->interrupt[port].length = max_packet == 0 ? 8 : max_packet > UHCI_INTERRUPT_MAX ? UHCI_INTERRUPT_MAX : max_packet;
    interrupt_arm(c, port);
}

long uhci_interrupt_poll(int controller, int port, void *data) {
    struct controller *c = &controllers[controller];
    if (!c->interrupt[port].on) {
        return -1;
    }
    // 设备没有数据时回答 NAK，TD 保持 Active，控制器过 8 毫秒再问：这里什么都不用做
    uint32_t status = td_at(c, TD_INTERRUPT + port)->status;
    if (status & TD_ACTIVE) {
        return -1;
    }
    if (status & TD_ERRORS) {
        interrupt_arm(c, port);
        return -2;
    }
    uint32_t n = td_actual(status);
    memcpy(data, c->page + INTERRUPT_AREA + port * UHCI_INTERRUPT_MAX, n);
    c->interrupt[port].toggle = !c->interrupt[port].toggle;
    interrupt_arm(c, port);
    return (long)n;
}

void uhci_interrupt_stop(int controller, int port) {
    struct controller *c = &controllers[controller];
    if (!c->interrupt[port].on) {
        return;
    }
    c->interrupt[port].on = false;
    qh_at(c, QH_INTERRUPT + port)->element = LINK_TERMINATE;
    uhci_sleep(2);          // 控制器可能正做着那个 TD：等它走过这一帧
}

// ============================================================================
// 启动
// ============================================================================

/** 复位并启动一个控制器，寄存器在端口 io 开始的地方（中断先关着）。@return 能用 */
static bool controller_start(struct controller *c, uint32_t io) {
    c->io = io;
    if (reg_read(c, REG_USBCMD) == 0xFFFF) {
        return false;           // 端口后面什么都没有
    }
    uint64_t phys;
    char *dma = (char *)dma_alloc(2 * PAGE_SIZE, &phys);
    if (dma == MAP_FAILED || phys + 2 * PAGE_SIZE > 0xFFFFFFFFull) {
        printf("usbkbd: no memory the USB controller can reach\n");
        return false;
    }
    c->page = dma + PAGE_SIZE;
    c->page_phys = (uint32_t)phys + PAGE_SIZE;

    // 不管固件把它留成了什么样：在总线上发复位信号（上面的设备忘掉固件给的地址），再复位
    // 控制器自己
    reg_write(c, REG_USBINTR, 0);
    reg_write(c, REG_USBCMD, CMD_GLOBAL_RESET);
    uhci_sleep(50);
    reg_write(c, REG_USBCMD, 0);
    uhci_sleep(10);
    reg_write(c, REG_USBCMD, CMD_RESET);
    for (int i = 0; i < 10 && (reg_read(c, REG_USBCMD) & CMD_RESET); i++) {
        uhci_sleep(1);
    }
    if (reg_read(c, REG_USBCMD) & CMD_RESET) {
        printf("usbkbd: the USB controller at port 0x%x does not come out of reset\n", io);
        return false;
    }

    // 队列头横着连成一串：每个端口的，最后是控制传输的。都还没有要做的事
    for (int port = 0; port < UHCI_PORTS; port++) {
        int next = port + 1 < UHCI_PORTS ? QH_INTERRUPT + port + 1 : QH_CONTROL;
        qh_at(c, QH_INTERRUPT + port)->link = qh_phys(c, next) | LINK_QH;
        qh_at(c, QH_INTERRUPT + port)->element = LINK_TERMINATE;
    }
    qh_at(c, QH_CONTROL)->link = LINK_TERMINATE;
    qh_at(c, QH_CONTROL)->element = LINK_TERMINATE;
    volatile uint32_t *frames = (volatile uint32_t *)dma;
    for (int i = 0; i < FRAMES; i++) {
        frames[i] = qh_phys(c, i % INTERRUPT_EVERY == 0 ? QH_INTERRUPT : QH_CONTROL) | LINK_QH;
    }

    reg_write(c, REG_FRNUM, 0);
    io_write(c->io + REG_FRBASEADD, 4, (uint32_t)phys);
    reg_write(c, REG_USBSTS, STS_ALL);
    reg_write(c, REG_USBCMD, CMD_RUN | CMD_CONFIGURED | CMD_MAX_PACKET_64);
    return true;
}

int uhci_open(void) {
    struct hw_range range;
    for (uint32_t n = 0; controller_count < UHCI_MAX_CONTROLLERS && hw_find(HW_PORTS, n, &range); n++) {
        if (controller_start(&controllers[controller_count], (uint32_t)range.start)) {
            controller_count++;
        }
    }
    if (controller_count == 0) {
        return 0;
    }
    // 中断线：许可表里有的都认领（几个控制器共用一条线时表里只有一条）。认领之后才让
    // 控制器发中断
    for (uint32_t n = 0; hw_find(HW_IRQ, n, &range); n++) {
        irq_count += irq_claim((int)range.start) == 0;
    }
    if (irq_count > 0) {
        for (int i = 0; i < controller_count; i++) {
            reg_write(&controllers[i], REG_USBINTR, INTR_TIMEOUT | INTR_COMPLETE | INTR_SHORT_PACKET);
        }
    }
    return controller_count;
}
