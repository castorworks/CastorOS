/**
 * virtio 设备的公共部分（legacy 接口）：找设备并许可给驱动、寄存器访问、队列
 */

#include <virtio.h>
#include <pci.h>
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

// virtio-mmio (legacy)。每个设备是设备树里一个 "virtio,mmio" 节点：寄存器在哪里、
// 用哪个中断向内核查（device_find）。QEMU virt 上有 32 个这样的槽位，大多数是空的
#define MMIO_MAGIC_VALUE    0x74726976

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

static uint32_t reg_read(struct virtio_dev *dev, uint32_t off) { return dev->regs[off / 4]; }
static void reg_write(struct virtio_dev *dev, uint32_t off, uint32_t value) { dev->regs[off / 4] = value; }

/** 把 base 所在的那一页映射进来，*regs 指向 base 处的寄存器。用完 unmap_slot */
static void *map_slot(uint64_t base, volatile uint32_t **regs) {
    uint64_t page = base & ~(uint64_t)(PAGE_SIZE - 1);
    void *mapped = map_device((uintptr_t)page, PAGE_SIZE);
    if (mapped != MAP_FAILED) {
        *regs = (volatile uint32_t *)((char *)mapped + (base - page));
    }
    return mapped;
}

static bool transport_allow(uint32_t device_id) {
    struct device_info slot;
    for (uint32_t i = 0; device_find("virtio,mmio", i, &slot) == 0; i++) {
        if (!slot.has_irq) {
            continue;
        }
        // 把这个槽位映射进来看看里面是什么设备；看完就撤掉，许可记的是物理地址
        struct virtio_dev probe;
        void *mapped = map_slot(slot.base, &probe.regs);
        if (mapped == MAP_FAILED) {
            continue;
        }
        bool found = reg_read(&probe, MMIO_MAGIC) == MMIO_MAGIC_VALUE &&
                     reg_read(&probe, MMIO_DEVICE_ID) == device_id;
        if (found) {
            // 复位设备。上一个驱动如果是崩溃的，设备还在往它的（已经被收回的）内存里
            // 读写：在新驱动启动之前先让它停下来
            reg_write(&probe, MMIO_STATUS, 0);
        }
        munmap(mapped, PAGE_SIZE);
        if (found) {
            return hw_allow(HW_MEMORY, (uintptr_t)slot.base, (uintptr_t)(slot.size ? slot.size : PAGE_SIZE)) == 0 &&
                   hw_allow(HW_IRQ, slot.irq, 1) == 0;
        }
    }
    return false;
}

static bool transport_open(struct virtio_dev *dev, uint32_t device_id) {
    struct hw_range mem, irq;
    if (!hw_find(HW_MEMORY, 0, &mem) || !hw_find(HW_IRQ, 0, &irq)) {
        return false;
    }
    void *mapped = map_slot(mem.start, &dev->regs);
    if (mapped == MAP_FAILED) {
        return false;
    }
    if (reg_read(dev, MMIO_MAGIC) != MMIO_MAGIC_VALUE || reg_read(dev, MMIO_DEVICE_ID) != device_id) {
        munmap(mapped, PAGE_SIZE);
        return false;
    }
    if (reg_read(dev, MMIO_VERSION) != 1) {
        printf("virtio: mmio version %u is not supported (need legacy)\n", reg_read(dev, MMIO_VERSION));
        munmap(mapped, PAGE_SIZE);
        return false;
    }
    dev->irq = (int)irq.start;
    reg_write(dev, MMIO_GUEST_PAGE_SIZE, PAGE_SIZE);
    return true;
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

static bool transport_allow(uint32_t device_id) {
    // 只扫描 0 号总线上各设备的 0 号功能：QEMU 把 virtio 设备都放在这里
    for (uint32_t device = 0; device < 32; device++) {
        pci_dev_t slot = PCI_DEV(device, 0);
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
        uint32_t io_size = pci_bar_size(slot, 0);
        // 打开端口访问和总线主控（设备要自己读写内存）：驱动碰不到配置空间，这一步得在这里做
        pci_write(slot, PCI_COMMAND, pci_read(slot, PCI_COMMAND) | PCI_COMMAND_IO | PCI_COMMAND_MASTER);
        // 复位设备。上一个驱动如果是崩溃的，设备还在往它的（已经被收回的）内存里读写：
        // 在新驱动启动之前先让它停下来
        io_write((bar0 & ~3u) + VPCI_STATUS, 1, 0);
        if (io_size == 0) {
            return false;
        }
        return hw_allow(HW_PORTS, bar0 & ~3u, io_size) == 0 &&
               hw_allow(HW_IRQ, pci_read(slot, 0x3C) & 0xFF, 1) == 0;
    }
    return false;
}

// legacy 的端口寄存器里没有设备类型可查：许可给本进程的就是它的设备
static bool transport_open(struct virtio_dev *dev, uint32_t device_id) {
    (void)device_id;
    struct hw_range ports, irq;
    if (!hw_find(HW_PORTS, 0, &ports) || !hw_find(HW_IRQ, 0, &irq)) {
        return false;
    }
    dev->io_base = (uint32_t)ports.start;
    dev->irq = (int)irq.start;
    return true;
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

bool virtio_allow(uint32_t device_id) {
    return transport_allow(device_id);
}

bool virtio_open(struct virtio_dev *dev, uint32_t device_id) {
    if (!transport_open(dev, device_id)) {
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
