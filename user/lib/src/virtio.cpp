/**
 * virtio 设备的公共部分（legacy 接口）：找设备、寄存器访问、队列
 */

#include <virtio.h>
#include <syscall.h>
#include <stdio.h>

#define PAGE_SIZE 4096

#define VIRTIO_STATUS_ACKNOWLEDGE   1
#define VIRTIO_STATUS_DRIVER        2
#define VIRTIO_STATUS_DRIVER_OK     4

// ============================================================================
// 传输层
// ============================================================================

#if defined(ARCH_ARM64)

// virtio-mmio (legacy)：QEMU virt 上 32 个槽位，每个 0x200 字节，中断号 48 + 槽位号
#define MMIO_BASE           0x0a000000
#define MMIO_SLOT_SIZE      0x200
#define MMIO_SLOTS          32
#define MMIO_IRQ_BASE       48

#define MMIO_MAGIC          0x000   // 'virt'
#define MMIO_VERSION        0x004   // 1 = legacy
#define MMIO_DEVICE_ID      0x008
#define MMIO_HOST_FEATURES  0x010
#define MMIO_HOST_FEATURES_SEL 0x014
#define MMIO_GUEST_FEATURES 0x020
#define MMIO_GUEST_FEATURES_SEL 0x024
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

static volatile uint32_t *mmio_slots = NULL;    // 全部槽位，映射一次

static uint32_t reg_read(struct virtio_dev *dev, uint32_t off) { return dev->regs[off / 4]; }
static void reg_write(struct virtio_dev *dev, uint32_t off, uint32_t value) { dev->regs[off / 4] = value; }

static bool transport_find(struct virtio_dev *dev, uint32_t device_id) {
    if (!mmio_slots) {
        void *p = map_device(MMIO_BASE, MMIO_SLOT_SIZE * MMIO_SLOTS);
        if (p == MAP_FAILED) {
            return false;
        }
        mmio_slots = (volatile uint32_t *)p;
    }
    for (int i = 0; i < MMIO_SLOTS; i++) {
        dev->regs = mmio_slots + i * (MMIO_SLOT_SIZE / 4);
        if (reg_read(dev, MMIO_MAGIC) != 0x74726976 || reg_read(dev, MMIO_DEVICE_ID) != device_id) {
            continue;
        }
        if (reg_read(dev, MMIO_VERSION) != 1) {
            printf("virtio: mmio version %u is not supported (need legacy)\n", reg_read(dev, MMIO_VERSION));
            return false;
        }
        dev->irq = MMIO_IRQ_BASE + i;
        reg_write(dev, MMIO_GUEST_PAGE_SIZE, PAGE_SIZE);
        return true;
    }
    return false;
}

static void transport_set_status(struct virtio_dev *dev, uint32_t status) { reg_write(dev, MMIO_STATUS, status); }

uint32_t virtio_get_features(struct virtio_dev *dev) {
    reg_write(dev, MMIO_HOST_FEATURES_SEL, 0);
    return reg_read(dev, MMIO_HOST_FEATURES);
}

void virtio_set_features(struct virtio_dev *dev, uint32_t features) {
    reg_write(dev, MMIO_GUEST_FEATURES_SEL, 0);
    reg_write(dev, MMIO_GUEST_FEATURES, features);
}

uint8_t virtio_config_read8(struct virtio_dev *dev, uint32_t offset) {
    return ((volatile uint8_t *)dev->regs)[MMIO_CONFIG + offset];
}

uint32_t virtio_config_read32(struct virtio_dev *dev, uint32_t offset) {
    return reg_read(dev, MMIO_CONFIG + offset);
}

static uint32_t transport_queue_max(struct virtio_dev *dev, uint32_t index) {
    reg_write(dev, MMIO_QUEUE_SEL, index);
    return reg_read(dev, MMIO_QUEUE_NUM_MAX);
}

// mmio 可以选一个比上限小的队列长度
static uint32_t transport_queue_pick(uint32_t device_max, uint32_t wanted) {
    return device_max > wanted ? wanted : device_max;
}

static void transport_queue_set(struct virtio_dev *dev, uint32_t index, uint32_t num, uint64_t phys) {
    reg_write(dev, MMIO_QUEUE_SEL, index);
    reg_write(dev, MMIO_QUEUE_NUM, num);
    reg_write(dev, MMIO_QUEUE_ALIGN, PAGE_SIZE);
    reg_write(dev, MMIO_QUEUE_PFN, (uint32_t)(phys / PAGE_SIZE));
}

void virtio_notify(struct virtio_dev *dev, struct virtq *q) { reg_write(dev, MMIO_QUEUE_NOTIFY, q->index); }
void virtio_irq_ack(struct virtio_dev *dev) { reg_write(dev, MMIO_INT_ACK, reg_read(dev, MMIO_INT_STATUS)); }

#else /* i686, x86_64 */

// virtio-pci (legacy)：寄存器在 BAR0 指向的 I/O 端口里
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC
#define PCI_VENDOR_VIRTIO   0x1AF4

#define VPCI_HOST_FEATURES  0x00    // 32 位
#define VPCI_GUEST_FEATURES 0x04    // 32 位
#define VPCI_QUEUE_PFN      0x08    // 32 位
#define VPCI_QUEUE_SIZE     0x0C    // 16 位
#define VPCI_QUEUE_SEL      0x0E    // 16 位
#define VPCI_QUEUE_NOTIFY   0x10    // 16 位
#define VPCI_STATUS         0x12    // 8 位
#define VPCI_ISR            0x13    // 8 位，读它会撤销中断
#define VPCI_CONFIG         0x14

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

static bool transport_find(struct virtio_dev *dev, uint32_t device_id) {
    // 只扫描 0 号总线上各设备的 0 号功能：QEMU 把设备都放在这里
    for (uint32_t slot = 0; slot < 32; slot++) {
        uint32_t id = pci_read(slot, 0x00);
        uint32_t pci_device = id >> 16;
        // legacy/transitional 设备：设备号 0x1000-0x103F，子系统号才是 virtio 的设备类型
        if ((id & 0xFFFF) != PCI_VENDOR_VIRTIO || pci_device < 0x1000 || pci_device > 0x103F ||
            (pci_read(slot, 0x2C) >> 16) != device_id) {
            continue;
        }
        uint32_t bar0 = pci_read(slot, 0x10);
        if (!(bar0 & 1)) {
            printf("virtio: pci device has no I/O port BAR (need a legacy/transitional device)\n");
            return false;
        }
        dev->io_base = bar0 & ~3u;
        dev->irq = (int)(pci_read(slot, 0x3C) & 0xFF);
        // 打开端口访问和总线主控（设备要自己读写内存）
        pci_write(slot, 0x04, pci_read(slot, 0x04) | 0x5);
        return true;
    }
    return false;
}

static void transport_set_status(struct virtio_dev *dev, uint32_t status) {
    io_write(dev->io_base + VPCI_STATUS, 1, status);
}

uint32_t virtio_get_features(struct virtio_dev *dev) {
    return port_read(dev->io_base + VPCI_HOST_FEATURES, 4);
}

void virtio_set_features(struct virtio_dev *dev, uint32_t features) {
    io_write(dev->io_base + VPCI_GUEST_FEATURES, 4, features);
}

uint8_t virtio_config_read8(struct virtio_dev *dev, uint32_t offset) {
    return (uint8_t)port_read(dev->io_base + VPCI_CONFIG + offset, 1);
}

uint32_t virtio_config_read32(struct virtio_dev *dev, uint32_t offset) {
    return port_read(dev->io_base + VPCI_CONFIG + offset, 4);
}

static uint32_t transport_queue_max(struct virtio_dev *dev, uint32_t index) {
    io_write(dev->io_base + VPCI_QUEUE_SEL, 2, index);
    return port_read(dev->io_base + VPCI_QUEUE_SIZE, 2);
}

// legacy pci 的队列长度由设备决定，不能改
static uint32_t transport_queue_pick(uint32_t device_max, uint32_t wanted) {
    (void)wanted;
    return device_max;
}

static void transport_queue_set(struct virtio_dev *dev, uint32_t index, uint32_t num, uint64_t phys) {
    (void)num;
    io_write(dev->io_base + VPCI_QUEUE_SEL, 2, index);
    io_write(dev->io_base + VPCI_QUEUE_PFN, 4, (uint32_t)(phys / PAGE_SIZE));
}

void virtio_notify(struct virtio_dev *dev, struct virtq *q) {
    io_write(dev->io_base + VPCI_QUEUE_NOTIFY, 2, q->index);
}

void virtio_irq_ack(struct virtio_dev *dev) { port_read(dev->io_base + VPCI_ISR, 1); }

#endif

// ============================================================================
// 公共部分
// ============================================================================

bool virtio_find(struct virtio_dev *dev, uint32_t device_id) {
    if (!transport_find(dev, device_id)) {
        return false;
    }
    transport_set_status(dev, 0);   // 复位
    transport_set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    return true;
}

void virtio_driver_ok(struct virtio_dev *dev) {
    transport_set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
}

static size_t align_page(size_t n) { return (n + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1); }

bool virtq_setup(struct virtio_dev *dev, struct virtq *q, uint32_t index, uint32_t max_num) {
    uint32_t device_max = transport_queue_max(dev, index);
    if (device_max == 0) {
        return false;
    }
    uint32_t num = transport_queue_pick(device_max, max_num);

    // legacy 布局：描述符表、avail 环，对齐到页之后是 used 环；整段物理连续
    size_t avail_off = sizeof(struct vring_desc) * num;
    size_t used_off = align_page(avail_off + sizeof(uint16_t) * (3 + num));
    size_t bytes = used_off + align_page(sizeof(uint16_t) * 3 + sizeof(struct vring_used_elem) * num);

    uint64_t phys = 0;
    char *mem = (char *)dma_alloc(bytes, &phys);
    if (mem == MAP_FAILED) {
        return false;
    }

    q->index = index;
    q->num = num;
    q->desc = (volatile struct vring_desc *)mem;
    q->avail = (volatile struct vring_avail *)(mem + avail_off);
    q->used = (volatile struct vring_used *)(mem + used_off);
    q->last_used = 0;
    transport_queue_set(dev, index, num, phys);
    return true;
}

void virtq_submit(struct virtq *q, uint16_t head) {
    q->avail->ring[q->avail->idx % q->num] = head;
    __sync_synchronize();           // 设备看到 idx 变化时，ring 里的内容必须已经就位
    q->avail->idx = q->avail->idx + 1;
    __sync_synchronize();
}

bool virtq_pop_used(struct virtq *q, uint32_t *head, uint32_t *len) {
    if (q->used->idx == q->last_used) {
        return false;
    }
    volatile struct vring_used_elem *e = &q->used->ring[q->last_used % q->num];
    if (head) {
        *head = e->id;
    }
    if (len) {
        *len = e->len;
    }
    q->last_used++;
    return true;
}
