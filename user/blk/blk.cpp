// blk - virtio-blk 块设备驱动
//
// 特权的用户态驱动，以 "blk" 登记，实现 blk.h 里的协议。
// 设备用的是 virtio 的 legacy 接口，两种接入方式只是寄存器的访问方法不同：
//   - x86：virtio-blk-pci。在 PCI 配置空间里找到设备，寄存器在它的 I/O 端口 BAR 里
//   - arm64：virtio-mmio（QEMU virt 的 32 个槽位），寄存器用 map_device 映射
// 队列和请求用的内存来自 dma_alloc：设备只认物理地址。
// 一次只处理一个请求：提交给设备后等它的中断，再应答客户。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <names.h>
#include <blk.h>

#define PAGE_SIZE 4096

// ============================================================================
// virtio 公共定义
// ============================================================================

#define VIRTIO_STATUS_ACKNOWLEDGE   1
#define VIRTIO_STATUS_DRIVER        2
#define VIRTIO_STATUS_DRIVER_OK     4

#define VIRTIO_DEVICE_BLOCK         2

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};
#define VRING_DESC_F_NEXT   1       // 后面还有描述符（next 有效）
#define VRING_DESC_F_WRITE  2       // 设备往这块内存里写

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
};

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
};

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[];
};

struct virtio_blk_req {
    uint32_t type;                  // 0 读，1 写
    uint32_t reserved;
    uint64_t sector;
};
#define VIRTIO_BLK_T_IN     0
#define VIRTIO_BLK_T_OUT    1

// ============================================================================
// 传输层：找到设备、访问它的寄存器
// ============================================================================

static int device_irq = -1;

#if defined(ARCH_ARM64)

// virtio-mmio (legacy)：QEMU virt 上 32 个槽位，每个 0x200 字节，中断号 48 + 槽位号
#define MMIO_BASE           0x0a000000
#define MMIO_SLOT_SIZE      0x200
#define MMIO_SLOTS          32
#define MMIO_IRQ_BASE       48

#define MMIO_MAGIC          0x000   // 'virt'
#define MMIO_VERSION        0x004   // 1 = legacy
#define MMIO_DEVICE_ID      0x008
#define MMIO_GUEST_FEATURES 0x020
#define MMIO_GUEST_PAGE_SIZE 0x028
#define MMIO_QUEUE_SEL      0x030
#define MMIO_QUEUE_NUM_MAX  0x034
#define MMIO_QUEUE_NUM      0x038
#define MMIO_QUEUE_ALIGN    0x03c
#define MMIO_QUEUE_PFN      0x040
#define MMIO_QUEUE_NOTIFY   0x050
#define MMIO_INT_STATUS     0x060
#define MMIO_INT_ACK        0x064
#define MMIO_STATUS         0x070
#define MMIO_CONFIG         0x100

static volatile uint32_t *regs;     // 设备所在槽位的寄存器

static uint32_t reg_read(uint32_t off) { return regs[off / 4]; }
static void reg_write(uint32_t off, uint32_t value) { regs[off / 4] = value; }

static bool transport_find(void) {
    volatile uint32_t *slots = (volatile uint32_t *)map_device(MMIO_BASE, MMIO_SLOT_SIZE * MMIO_SLOTS);
    if (slots == MAP_FAILED) {
        return false;
    }
    for (int i = 0; i < MMIO_SLOTS; i++) {
        regs = slots + i * (MMIO_SLOT_SIZE / 4);
        if (reg_read(MMIO_MAGIC) != 0x74726976 || reg_read(MMIO_DEVICE_ID) != VIRTIO_DEVICE_BLOCK) {
            continue;
        }
        if (reg_read(MMIO_VERSION) != 1) {
            printf("blk: virtio-mmio version %u is not supported (need legacy)\n", reg_read(MMIO_VERSION));
            return false;
        }
        device_irq = MMIO_IRQ_BASE + i;
        return true;
    }
    return false;
}

static void transport_set_status(uint32_t status) { reg_write(MMIO_STATUS, status); }
static void transport_set_features(uint32_t features) { reg_write(MMIO_GUEST_FEATURES, features); }

static uint32_t transport_queue_max(void) {
    reg_write(MMIO_GUEST_PAGE_SIZE, PAGE_SIZE);
    reg_write(MMIO_QUEUE_SEL, 0);
    return reg_read(MMIO_QUEUE_NUM_MAX);
}

// mmio 可以选一个比上限小的队列长度
static uint32_t transport_queue_pick(uint32_t max) { return max > 64 ? 64 : max; }

static void transport_queue_set(uint32_t num, uint64_t phys) {
    reg_write(MMIO_QUEUE_NUM, num);
    reg_write(MMIO_QUEUE_ALIGN, PAGE_SIZE);
    reg_write(MMIO_QUEUE_PFN, (uint32_t)(phys / PAGE_SIZE));
}

static void transport_notify(void) { reg_write(MMIO_QUEUE_NOTIFY, 0); }
static void transport_irq_ack(void) { reg_write(MMIO_INT_ACK, reg_read(MMIO_INT_STATUS)); }

static uint64_t transport_capacity(void) {
    return (uint64_t)reg_read(MMIO_CONFIG) | ((uint64_t)reg_read(MMIO_CONFIG + 4) << 32);
}

#else /* i686, x86_64 */

// virtio-pci (legacy)：寄存器在 BAR0 指向的 I/O 端口里
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC
#define PCI_VENDOR_VIRTIO   0x1AF4
#define PCI_DEVICE_VIRTIO_BLK 0x1001

#define VPCI_GUEST_FEATURES 0x04    // 32 位
#define VPCI_QUEUE_PFN      0x08    // 32 位
#define VPCI_QUEUE_SIZE     0x0C    // 16 位
#define VPCI_QUEUE_SEL      0x0E    // 16 位
#define VPCI_QUEUE_NOTIFY   0x10    // 16 位
#define VPCI_STATUS         0x12    // 8 位
#define VPCI_ISR            0x13    // 8 位，读它会撤销中断
#define VPCI_CONFIG         0x14

static uint32_t io_base;            // 设备寄存器的端口基址

static uint32_t port_read(uint32_t port, int width) {
    uint32_t v = 0;
    io_read(port, width, &v);
    return v;
}

static uint32_t pci_read(uint32_t dev, uint32_t off) {
    io_write(PCI_CONFIG_ADDRESS, 4, 0x80000000u | (dev << 11) | (off & 0xFC));
    return port_read(PCI_CONFIG_DATA, 4);
}

static void pci_write(uint32_t dev, uint32_t off, uint32_t value) {
    io_write(PCI_CONFIG_ADDRESS, 4, 0x80000000u | (dev << 11) | (off & 0xFC));
    io_write(PCI_CONFIG_DATA, 4, value);
}

static bool transport_find(void) {
    // 只扫描 0 号总线上各设备的 0 号功能：QEMU 把设备都放在这里
    for (uint32_t dev = 0; dev < 32; dev++) {
        uint32_t id = pci_read(dev, 0x00);
        if ((id & 0xFFFF) != PCI_VENDOR_VIRTIO || (id >> 16) != PCI_DEVICE_VIRTIO_BLK) {
            continue;
        }
        uint32_t bar0 = pci_read(dev, 0x10);
        if (!(bar0 & 1)) {
            printf("blk: virtio-blk-pci has no I/O port BAR (need a legacy/transitional device)\n");
            return false;
        }
        io_base = bar0 & ~3u;
        device_irq = (int)(pci_read(dev, 0x3C) & 0xFF);
        // 打开端口访问和总线主控（设备要自己读写内存）
        pci_write(dev, 0x04, pci_read(dev, 0x04) | 0x5);
        return true;
    }
    return false;
}

static void transport_set_status(uint32_t status) { io_write(io_base + VPCI_STATUS, 1, status); }
static void transport_set_features(uint32_t features) { io_write(io_base + VPCI_GUEST_FEATURES, 4, features); }

static uint32_t transport_queue_max(void) {
    io_write(io_base + VPCI_QUEUE_SEL, 2, 0);
    return port_read(io_base + VPCI_QUEUE_SIZE, 2);
}

// legacy pci 的队列长度由设备决定，不能改
static uint32_t transport_queue_pick(uint32_t max) { return max; }

static void transport_queue_set(uint32_t num, uint64_t phys) {
    (void)num;
    io_write(io_base + VPCI_QUEUE_PFN, 4, (uint32_t)(phys / PAGE_SIZE));
}

static void transport_notify(void) { io_write(io_base + VPCI_QUEUE_NOTIFY, 2, 0); }
static void transport_irq_ack(void) { port_read(io_base + VPCI_ISR, 1); }

static uint64_t transport_capacity(void) {
    return (uint64_t)port_read(io_base + VPCI_CONFIG, 4) |
           ((uint64_t)port_read(io_base + VPCI_CONFIG + 4, 4) << 32);
}

#endif

// ============================================================================
// 队列和请求
// ============================================================================

static uint32_t queue_num;
static volatile struct vring_desc *desc;
static volatile struct vring_avail *avail;
static volatile struct vring_used *used;
static uint16_t last_used;

// 请求用的 DMA 内存：一页放请求头和状态字节，一页放数据
static volatile struct virtio_blk_req *req_hdr;
static volatile uint8_t *req_status;
static char *req_data;
static uint64_t req_hdr_phys, req_status_phys, req_data_phys;

static uint64_t capacity;       // 扇区数

static size_t align_page(size_t n) { return (n + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1); }

static bool device_init(void) {
    if (!transport_find()) {
        return false;
    }

    transport_set_status(0);    // 复位
    transport_set_status(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    transport_set_features(0);  // 不需要任何可选特性

    uint32_t max = transport_queue_max();
    if (max == 0) {
        return false;
    }
    queue_num = transport_queue_pick(max);

    // legacy 布局：描述符表、avail 环，对齐到页之后是 used 环；整段物理连续
    size_t avail_off = sizeof(struct vring_desc) * queue_num;
    size_t used_off = align_page(avail_off + sizeof(uint16_t) * (3 + queue_num));
    size_t queue_bytes = used_off + align_page(sizeof(uint16_t) * 3 + sizeof(struct vring_used_elem) * queue_num);

    uint64_t phys = 0;
    char *mem = (char *)dma_alloc(queue_bytes + 2 * PAGE_SIZE, &phys);
    if (mem == MAP_FAILED) {
        printf("blk: cannot allocate DMA memory\n");
        return false;
    }
    desc = (volatile struct vring_desc *)mem;
    avail = (volatile struct vring_avail *)(mem + avail_off);
    used = (volatile struct vring_used *)(mem + used_off);

    char *req_page = mem + queue_bytes;
    req_hdr = (volatile struct virtio_blk_req *)req_page;
    req_status = (volatile uint8_t *)(req_page + sizeof(struct virtio_blk_req));
    req_data = req_page + PAGE_SIZE;
    req_hdr_phys = phys + queue_bytes;
    req_status_phys = req_hdr_phys + sizeof(struct virtio_blk_req);
    req_data_phys = req_hdr_phys + PAGE_SIZE;

    if (irq_claim(device_irq) != 0) {
        printf("blk: cannot claim IRQ %d\n", device_irq);
        return false;
    }

    transport_queue_set(queue_num, phys);
    transport_set_status(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
    capacity = transport_capacity();
    return true;
}

// 读或写 count 个扇区（数据在 req_data 里），等设备完成。返回是否成功
static bool do_request(uint32_t type, uint64_t sector, uint32_t count) {
    uint32_t bytes = count * BLK_SECTOR_SIZE;

    req_hdr->type = type;
    req_hdr->reserved = 0;
    req_hdr->sector = sector;
    *req_status = 0xFF;

    // 一条三段的描述符链：请求头（设备读）、数据（读盘时设备写）、状态（设备写）
    desc[0].addr = req_hdr_phys;
    desc[0].len = sizeof(struct virtio_blk_req);
    desc[0].flags = VRING_DESC_F_NEXT;
    desc[0].next = 1;
    desc[1].addr = req_data_phys;
    desc[1].len = bytes;
    desc[1].flags = VRING_DESC_F_NEXT | (type == VIRTIO_BLK_T_IN ? VRING_DESC_F_WRITE : 0);
    desc[1].next = 2;
    desc[2].addr = req_status_phys;
    desc[2].len = 1;
    desc[2].flags = VRING_DESC_F_WRITE;
    desc[2].next = 0;

    avail->ring[avail->idx % queue_num] = 0;
    __sync_synchronize();
    avail->idx = avail->idx + 1;
    __sync_synchronize();
    transport_notify();

    // 等设备的完成中断（只收内核消息；这期间客户的请求留在各自的 call 里排队）
    while (used->idx == last_used) {
        struct ipc_msg m;
        if (ipc_recv(IPC_FROM_KERNEL, &m) != 0) {
            continue;
        }
        transport_irq_ack();
        irq_ack(device_irq);
    }
    last_used = used->idx;
    return *req_status == 0;
}

// ============================================================================
// 客户（每个客户一块共享缓冲区，由授予通知送来）
// ============================================================================

#define MAX_CLIENTS 16

static struct {
    int pid;            // 0 表示空闲
    char *buf;
} clients[MAX_CLIENTS];

static int find_client(int pid) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].pid == pid) {
            return i;
        }
    }
    return -1;
}

static void attach_client(int pid, char *buf, size_t size) {
    if (size != BLK_BUF_SIZE) {
        munmap(buf, size);
        return;
    }
    int i = find_client(pid);
    if (i >= 0) {
        munmap(clients[i].buf, BLK_BUF_SIZE);
        clients[i].buf = buf;
        return;
    }
    // 顺便清掉已经退出的客户，再找空位
    for (int j = 0; j < MAX_CLIENTS; j++) {
        if (clients[j].pid != 0 && kill(clients[j].pid, 0) != 0) {
            munmap(clients[j].buf, BLK_BUF_SIZE);
            clients[j].pid = 0;
        }
    }
    i = find_client(0);
    if (i < 0) {
        munmap(buf, size);
        return;
    }
    clients[i].pid = pid;
    clients[i].buf = buf;
}

int main() {
    if (!device_init()) {
        printf("blk: no usable virtio-blk device\n");
        return 1;
    }
    if (name_register(BLK_SERVICE_NAME) != 0) {
        printf("blk: cannot register name\n");
        return 1;
    }
    printf("blk: driver ready (pid %d, irq %d), %u sectors (%u MB)\n",
           getpid(), device_irq, (uint32_t)capacity, (uint32_t)(capacity / 2048));

    struct ipc_msg m;
    for (;;) {
        if (ipc_recv(IPC_ANY, &m) != 0) {
            continue;
        }
        if (m.sender == IPC_KERNEL) {
            // 没有请求在途时到来的中断：应答掉即可
            transport_irq_ack();
            irq_ack(device_irq);
            continue;
        }
        if (m.label == IPC_LABEL_GRANT) {
            attach_client((int)m.sender, (char *)(uintptr_t)m.data[0], (size_t)m.data[1]);
            continue;
        }

        struct ipc_msg reply = {};
        reply.label = m.label;
        int64_t result = -1;

        int c = find_client((int)m.sender);
        if (m.label == BLK_INFO) {
            result = 0;
            reply.data[1] = capacity;
        } else if ((m.label == BLK_READ || m.label == BLK_WRITE) && c >= 0) {
            uint64_t sector = m.data[0];
            uint64_t count = m.data[1];
            if (count >= 1 && count <= BLK_BUF_SIZE / BLK_SECTOR_SIZE &&
                sector < capacity && count <= capacity - sector) {
                size_t bytes = (size_t)count * BLK_SECTOR_SIZE;
                if (m.label == BLK_WRITE) {
                    memcpy(req_data, clients[c].buf, bytes);
                    result = do_request(VIRTIO_BLK_T_OUT, sector, (uint32_t)count) ? 0 : -1;
                } else if (do_request(VIRTIO_BLK_T_IN, sector, (uint32_t)count)) {
                    memcpy(clients[c].buf, req_data, bytes);
                    result = 0;
                }
            }
        }

        reply.data[0] = (uint64_t)result;
        ipc_reply(m.sender, &reply);
    }
}
