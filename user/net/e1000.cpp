// Intel 千兆网卡（82540 一族，Linux 里叫 e1000）
//
// 驱动和网卡共用几块内存（dma_alloc），靠寄存器告诉对方“我放到哪儿了”：
//   - 收和发各有一个环：一排 16 字节的“描述符”，每个描述符指着一个缓冲区。
//   - 每个环有头、尾两个寄存器。头由网卡推进，尾由驱动推进；头和尾之间的描述符归网卡。
//   - 收：驱动把空缓冲区都交给网卡。网卡收到一帧就填进头上的那个缓冲区，在描述符上打上
//     “完成”，把头往前推，发中断。驱动取走数据，再把这个描述符还回去（推进尾）。
//   - 发：驱动把帧拷进尾上的缓冲区，推进尾；网卡发出去之后在描述符上打上“完成”。
// 寄存器在设备内存里（map_device）。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <net.h>
#include "net_internal.h"
#include "nic.h"

#define PAGE_SIZE 4096

// 寄存器（设备内存里的偏移）
#define REG_CTRL    0x0000
#define REG_STATUS  0x0008
#define REG_EERD    0x0014      // 读网卡上那块小存储器（里面有 MAC 地址）
#define REG_ICR     0x00C0      // 中断原因：读它就撤销了中断
#define REG_IMS     0x00D0      // 写 1 打开对应的中断
#define REG_IMC     0x00D8      // 写 1 关掉对应的中断
#define REG_RCTL    0x0100
#define REG_TCTL    0x0400
#define REG_TIPG    0x0410      // 两帧之间的间隔
#define REG_RDBAL   0x2800      // 接收环：描述符的物理地址、总字节数、头、尾
#define REG_RDBAH   0x2804
#define REG_RDLEN   0x2808
#define REG_RDH     0x2810
#define REG_RDT     0x2818
#define REG_TDBAL   0x3800      // 发送环
#define REG_TDBAH   0x3804
#define REG_TDLEN   0x3808
#define REG_TDH     0x3810
#define REG_TDT     0x3818
#define REG_MTA     0x5200      // 组播地址过滤表，128 项
#define REG_RAL     0x5400      // 本机的 MAC 地址：低 4 个字节，和高 2 个字节加“有效”位
#define REG_RAH     0x5404

#define CTRL_LINK_RESET     (1u << 3)
#define CTRL_AUTO_SPEED     (1u << 5)   // 速度由网卡和对方协商
#define CTRL_SET_LINK_UP    (1u << 6)
#define CTRL_INVERT_LOS     (1u << 7)
#define CTRL_FORCE_SPEED    (1u << 11)
#define CTRL_FORCE_DUPLEX   (1u << 12)
#define CTRL_RESET          (1u << 26)
#define CTRL_PHY_RESET      (1u << 31)

#define STATUS_LINK_UP      (1u << 1)

#define EERD_START          (1u << 0)
#define EERD_DONE           (1u << 4)
#define EERD_ADDR_SHIFT     8
#define EERD_DATA_SHIFT     16

#define INT_LINK_CHANGE     (1u << 2)
#define INT_RX_LOW          (1u << 4)   // 归网卡的接收描述符快用完了
#define INT_RX_OVERRUN      (1u << 6)
#define INT_RX              (1u << 7)

#define RCTL_ENABLE         (1u << 1)
#define RCTL_BROADCAST      (1u << 15)  // 收广播帧（ARP、DHCP 要用）
#define RCTL_STRIP_CRC      (1u << 26)  // 帧尾的 4 字节校验不交给我们
                                        // 缓冲区大小的那两位留 0：2048 字节

#define TCTL_ENABLE         (1u << 1)
#define TCTL_PAD_SHORT      (1u << 3)   // 不够 60 字节的帧由网卡补齐
#define TCTL_COLLISION      ((0x10u << 4) | (0x40u << 12))     // 手册推荐的冲突处理参数
#define TIPG_DEFAULT        (10u | (8u << 10) | (6u << 20))    // 手册推荐的帧间隔

#define RAH_VALID           (1u << 31)

// 接收描述符。网卡填好一帧之后写 length 和 status
struct rx_desc {
    uint64_t addr;          // 缓冲区的物理地址
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));
#define RX_DONE             (1u << 0)
#define RX_END_OF_PACKET    (1u << 1)   // 一帧完整地在这一个缓冲区里（缓冲区够大，总是这样）

// 发送描述符。驱动填 addr、length、cmd；网卡发完之后写 status
struct tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t cmd;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} __attribute__((packed));
#define TX_CMD_END_OF_PACKET (1u << 0)
#define TX_CMD_ADD_CRC       (1u << 1)  // 帧尾的校验由网卡算
#define TX_CMD_REPORT        (1u << 3)  // 发完之后在 status 里打上“完成”
#define TX_DONE              (1u << 0)

// 环的字节数必须是 128 的倍数：32 和 16 个描述符正好
#define RX_COUNT        32
#define TX_COUNT        16
#define BUF_SIZE        2048

static volatile uint32_t *regs;
static int irq;

static volatile struct rx_desc *rx_ring;
static volatile struct tx_desc *tx_ring;
static uint8_t *rx_bufs, *tx_bufs;
static uint32_t rx_next;        // 网卡下一个要填的接收描述符
static uint32_t tx_tail;        // 下一个用来发送的描述符

static uint32_t reg_read(uint32_t reg) { return regs[reg / 4]; }
static void reg_write(uint32_t reg, uint32_t value) { regs[reg / 4] = value; }

/** 读网卡上那块小存储器里的第 word 个 16 位字。@return 网卡一直不应答返回 false */
static bool eeprom_read(uint32_t word, uint16_t *value) {
    reg_write(REG_EERD, (word << EERD_ADDR_SHIFT) | EERD_START);
    for (int i = 0; i < 1000; i++) {
        uint32_t v = reg_read(REG_EERD);
        if (v & EERD_DONE) {
            *value = (uint16_t)(v >> EERD_DATA_SHIFT);
            return true;
        }
        usleep(1000);
    }
    return false;
}

/** MAC 地址：复位之后网卡自己把它装进了地址寄存器；没有的话到小存储器里去读 */
static bool read_mac(void) {
    uint32_t low = reg_read(REG_RAL), high = reg_read(REG_RAH);
    for (int i = 0; i < 4; i++) {
        my_mac[i] = (uint8_t)(low >> (8 * i));
    }
    my_mac[4] = (uint8_t)high;
    my_mac[5] = (uint8_t)(high >> 8);
    bool valid = (low != 0 || (high & 0xFFFF) != 0) && !(low == 0xFFFFFFFFu && (high & 0xFFFF) == 0xFFFF);
    if (!valid) {
        for (uint32_t w = 0; w < 3; w++) {
            uint16_t value;
            if (!eeprom_read(w, &value)) {
                return false;
            }
            my_mac[w * 2] = (uint8_t)value;
            my_mac[w * 2 + 1] = (uint8_t)(value >> 8);
        }
    }
    // 只收发给这个地址的帧（和广播）
    reg_write(REG_RAL, (uint32_t)my_mac[0] | ((uint32_t)my_mac[1] << 8) | ((uint32_t)my_mac[2] << 16) |
                       ((uint32_t)my_mac[3] << 24));
    reg_write(REG_RAH, (uint32_t)my_mac[4] | ((uint32_t)my_mac[5] << 8) | RAH_VALID);
    return true;
}

static void e1000_send(const uint8_t *frame, size_t len) {
    volatile struct tx_desc *d = &tx_ring[tx_tail];
    if (!(d->status & TX_DONE)) {
        return;             // 网卡还没发完这一个：环满了，丢掉
    }
    memcpy(tx_bufs + (size_t)tx_tail * BUF_SIZE, frame, len);
    d->length = (uint16_t)len;
    d->cmd = TX_CMD_END_OF_PACKET | TX_CMD_ADD_CRC | TX_CMD_REPORT;
    d->status = 0;
    tx_tail = (tx_tail + 1) % TX_COUNT;
    reg_write(REG_TDT, tx_tail);
}

static void e1000_interrupt(void) {
    reg_read(REG_ICR);      // 撤销中断。不看原因：不管为什么来的，把收到的帧都处理掉
    for (;;) {
        volatile struct rx_desc *d = &rx_ring[rx_next];
        if (!(d->status & RX_DONE)) {
            break;
        }
        uint32_t len = d->length;
        if ((d->status & RX_END_OF_PACKET) && d->errors == 0 && len > 0 && len <= BUF_SIZE) {
            nic_receive(rx_bufs + (size_t)rx_next * BUF_SIZE, len);
        }
        // 把这个描述符还给网卡：尾指着它，表示它之前的都可以用了
        d->status = 0;
        reg_write(REG_RDT, rx_next);
        rx_next = (rx_next + 1) % RX_COUNT;
    }
    irq_ack(irq);
}

bool e1000_allowed(void) {
    struct hw_range mem;
    return hw_find(HW_MEMORY, 0, &mem);
}

bool e1000_open(struct nic *nic) {
    struct hw_range mem, line;
    if (!hw_find(HW_MEMORY, 0, &mem) || !hw_find(HW_IRQ, 0, &line) || (mem.start & (PAGE_SIZE - 1))) {
        return false;
    }
    size_t reg_bytes = ((size_t)mem.count + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
    void *mapped = map_device((uintptr_t)mem.start, reg_bytes);
    if (mapped == MAP_FAILED) {
        return false;
    }
    regs = (volatile uint32_t *)mapped;
    irq = (int)line.start;

    // 复位，不管固件把它留成了什么样。复位之后中断是关着的，再关一次以防万一
    reg_write(REG_IMC, 0xFFFFFFFFu);
    reg_write(REG_CTRL, reg_read(REG_CTRL) | CTRL_RESET);
    usleep(20000);
    for (int i = 0; i < 100 && (reg_read(REG_CTRL) & CTRL_RESET); i++) {
        usleep(1000);
    }
    reg_write(REG_IMC, 0xFFFFFFFFu);
    reg_read(REG_ICR);

    if (!read_mac()) {
        printf("net: cannot read the card's MAC address\n");
        return false;
    }

    // 一段内存：第 0 页放两个环，后面是接收缓冲区，再后面是发送缓冲区
    uint64_t phys = 0;
    size_t bytes = PAGE_SIZE + (RX_COUNT + TX_COUNT) * BUF_SIZE;
    char *mem_base = (char *)dma_alloc(bytes, &phys);
    if (mem_base == MAP_FAILED) {
        printf("net: no memory for the card's buffers\n");
        return false;
    }
    rx_ring = (volatile struct rx_desc *)mem_base;
    tx_ring = (volatile struct tx_desc *)(mem_base + PAGE_SIZE / 2);
    rx_bufs = (uint8_t *)mem_base + PAGE_SIZE;
    tx_bufs = rx_bufs + RX_COUNT * BUF_SIZE;
    uint64_t rx_ring_phys = phys, tx_ring_phys = phys + PAGE_SIZE / 2;
    uint64_t rx_bufs_phys = phys + PAGE_SIZE, tx_bufs_phys = rx_bufs_phys + RX_COUNT * BUF_SIZE;

    for (uint32_t i = 0; i < RX_COUNT; i++) {
        rx_ring[i].addr = rx_bufs_phys + (uint64_t)i * BUF_SIZE;
        rx_ring[i].status = 0;
    }
    for (uint32_t i = 0; i < TX_COUNT; i++) {
        tx_ring[i].addr = tx_bufs_phys + (uint64_t)i * BUF_SIZE;
        tx_ring[i].status = TX_DONE;        // 还没用过的描述符算“发完了”：可以用
    }

    if (irq_claim(irq) != 0) {
        printf("net: cannot claim IRQ %d\n", irq);
        return false;
    }

    // 让链路起来，速度和双工由网卡自己和对方协商
    uint32_t ctrl = reg_read(REG_CTRL);
    ctrl &= ~(CTRL_LINK_RESET | CTRL_PHY_RESET | CTRL_INVERT_LOS | CTRL_FORCE_SPEED | CTRL_FORCE_DUPLEX);
    reg_write(REG_CTRL, ctrl | CTRL_SET_LINK_UP | CTRL_AUTO_SPEED);
    for (uint32_t i = 0; i < 128; i++) {
        reg_write(REG_MTA + i * 4, 0);      // 不收组播
    }

    // 接收：所有描述符都交给网卡（尾在头的前一个：环里总留一个空位来区分“空”和“满”）
    reg_write(REG_RDBAL, (uint32_t)rx_ring_phys);
    reg_write(REG_RDBAH, (uint32_t)(rx_ring_phys >> 32));
    reg_write(REG_RDLEN, RX_COUNT * sizeof(struct rx_desc));
    reg_write(REG_RDH, 0);
    reg_write(REG_RDT, RX_COUNT - 1);
    reg_write(REG_RCTL, RCTL_ENABLE | RCTL_BROADCAST | RCTL_STRIP_CRC);

    // 发送：环是空的（头等于尾）
    reg_write(REG_TDBAL, (uint32_t)tx_ring_phys);
    reg_write(REG_TDBAH, (uint32_t)(tx_ring_phys >> 32));
    reg_write(REG_TDLEN, TX_COUNT * sizeof(struct tx_desc));
    reg_write(REG_TDH, 0);
    reg_write(REG_TDT, 0);
    reg_write(REG_TIPG, TIPG_DEFAULT);
    reg_write(REG_TCTL, TCTL_ENABLE | TCTL_PAD_SHORT | TCTL_COLLISION);

    reg_write(REG_IMS, INT_RX | INT_RX_LOW | INT_RX_OVERRUN | INT_LINK_CHANGE);

    // 等链路起来：网卡和交换机要协商一两秒。等不到（没插网线）也照样往下走，
    // 之后插上网线网卡自己会接通
    bool link = false;
    for (int i = 0; i < 300 && !(link = (reg_read(REG_STATUS) & STATUS_LINK_UP) != 0); i++) {
        usleep(10000);
    }
    if (!link) {
        printf("net: no link (is the cable plugged in?)\n");
    }

    nic->kind = "Intel gigabit";
    nic->irq = irq;
    nic->send = e1000_send;
    nic->interrupt = e1000_interrupt;
    return true;
}
