#ifndef _USERLAND_LIB_VIRTIO_H_
#define _USERLAND_LIB_VIRTIO_H_

#include <types.h>

// virtio 设备的公共部分（legacy 接口），给用户态驱动用。
//
// 两种接入方式只是寄存器的访问方法不同，这里把差别包起来：
//   - x86：virtio-pci，寄存器在设备的 I/O 端口 BAR 里
//   - arm64：virtio-mmio，寄存器用 map_device 映射
// 队列内存来自 dma_alloc：设备只认物理地址。
//
// 找设备和用设备是两个进程的事。init（有特权）用 virtio_allow 找到设备，把它的寄存器和
// 中断线许可给即将成为驱动的子进程；驱动用 virtio_open 打开许可给自己的那个设备。
// 驱动自己不找设备，也碰不到别的设备。

#define VIRTIO_ID_NET       1
#define VIRTIO_ID_BLOCK     2

struct virtio_dev {
    int irq;                        // 设备的中断号（交给 irq_claim / irq_ack）
#if defined(ARCH_ARM64)
    volatile uint32_t *regs;        // 设备所在槽位的寄存器
#else
    uint32_t io_base;               // 设备寄存器的端口基址
#endif
};

struct vring_desc {
    uint64_t addr;                  // 缓冲区的物理地址
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
#define VRING_AVAIL_F_NO_INTERRUPT 1    // 设备用完缓冲区时不必发中断

struct vring_used_elem {
    uint32_t id;                    // 描述符链的头
    uint32_t len;                   // 设备写入的字节数
};

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[];
};

struct virtq {
    uint32_t index;                 // 队列在设备里的编号
    uint32_t num;                   // 描述符个数
    volatile struct vring_desc *desc;
    volatile struct vring_avail *avail;
    volatile struct vring_used *used;
    uint16_t last_used;             // 已经处理到 used 环的哪里
};

/**
 * 找到类型为 device_id 的设备，把它的寄存器和中断线加进当前进程的许可表（hw_allow）。
 * 需要特权：x86 上要扫 PCI 配置空间并打开设备的端口访问和总线主控，arm64 上要向内核
 * 查设备树里的槽位并逐个看里面是什么设备。
 * @return 没有这种设备返回 false
 */
bool virtio_allow(uint32_t device_id);

/**
 * 打开许可给当前进程的设备（类型应当是 device_id），复位并声明“有驱动了”。
 * 之后依次：协商特性、建立队列、virtio_driver_ok。
 * @return 没有被许可任何设备，或者那不是一个能用的 device_id 设备，返回 false
 */
bool virtio_open(struct virtio_dev *dev, uint32_t device_id);

/** 设备提供的特性位（低 32 位）/ 告诉设备驱动要用哪些 */
uint32_t virtio_get_features(struct virtio_dev *dev);
void virtio_set_features(struct virtio_dev *dev, uint32_t features);

/** 读设备专属的配置空间 */
uint8_t virtio_config_read8(struct virtio_dev *dev, uint32_t offset);
uint32_t virtio_config_read32(struct virtio_dev *dev, uint32_t offset);

/**
 * 建立第 index 个队列。队列长度由设备决定，能选的时候不超过 max_num。
 * @return 失败（设备没有这个队列、内存不够）返回 false
 */
bool virtq_setup(struct virtio_dev *dev, struct virtq *q, uint32_t index, uint32_t max_num);

/** 队列都建好之后：告诉设备可以开始工作 */
void virtio_driver_ok(struct virtio_dev *dev);

/** 把以 head 开头的描述符链交给设备（还需要 virtio_notify 才会被处理） */
void virtq_submit(struct virtq *q, uint16_t head);

/** 通知设备队列里有新东西 */
void virtio_notify(struct virtio_dev *dev, struct virtq *q);

/** 取一条设备已经用完的描述符链。@return 没有了返回 false */
bool virtq_pop_used(struct virtq *q, uint32_t *head, uint32_t *len);

/** 读走并撤销设备的中断（之后还要 irq_ack 才会再收到下一次） */
void virtio_irq_ack(struct virtio_dev *dev);

#endif // _USERLAND_LIB_VIRTIO_H_
