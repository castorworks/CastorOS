// virtio-blk 磁盘
//
// 访问寄存器、队列这些 virtio 的公共部分在 user/lib 的 virtio 里（x86 走 PCI，arm64 走
// MMIO）；这里只有块设备自己的部分。一次一个请求：提交给设备后等它的中断。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <blk.h>
#include <virtio.h>
#include "disk.h"

#define PAGE_SIZE 4096

struct virtio_blk_req {
    uint32_t type;                  // 0 读，1 写
    uint32_t reserved;
    uint64_t sector;
};
#define VIRTIO_BLK_T_IN     0
#define VIRTIO_BLK_T_OUT    1

static struct virtio_dev dev;
static struct virtq queue;

// 请求用的 DMA 内存：一页放请求头和状态字节，一页放数据
static volatile struct virtio_blk_req *req_hdr;
static volatile uint8_t *req_status;
static char *req_data;
static uint64_t req_hdr_phys, req_status_phys, req_data_phys;

static void virtio_blk_on_irq(void) {
    virtio_irq_ack(&dev);
    irq_ack(dev.irq);
}

static bool device_init(uint64_t *capacity) {
    if (!virtio_open(&dev, VIRTIO_ID_BLOCK)) {
        return false;
    }
    virtio_set_features(&dev, 0);   // 不需要任何可选特性

    uint64_t phys = 0;
    char *mem = (char *)dma_alloc(2 * PAGE_SIZE, &phys);
    if (mem == MAP_FAILED || !virtq_setup(&dev, &queue, 0, 64)) {
        printf("blk: cannot set up the request queue\n");
        return false;
    }
    req_hdr = (volatile struct virtio_blk_req *)mem;
    req_status = (volatile uint8_t *)(mem + sizeof(struct virtio_blk_req));
    req_data = mem + PAGE_SIZE;
    req_hdr_phys = phys;
    req_status_phys = phys + sizeof(struct virtio_blk_req);
    req_data_phys = phys + PAGE_SIZE;

    if (irq_claim(dev.irq) != 0) {
        printf("blk: cannot claim IRQ %d\n", dev.irq);
        return false;
    }
    disk_on_irq(dev.irq, virtio_blk_on_irq);

    virtio_driver_ok(&dev);
    *capacity = (uint64_t)virtio_config_read32(&dev, 0) | ((uint64_t)virtio_config_read32(&dev, 4) << 32);
    return true;
}

// 读或写 count 个扇区（数据在 req_data 里），等设备完成。返回是否成功
static bool do_request(uint32_t type, uint64_t sector, uint32_t count) {
    req_hdr->type = type;
    req_hdr->reserved = 0;
    req_hdr->sector = sector;
    *req_status = 0xFF;

    // 一条三段的描述符链：请求头（设备读）、数据（读盘时设备写）、状态（设备写）
    queue.desc[0].addr = req_hdr_phys;
    queue.desc[0].len = sizeof(struct virtio_blk_req);
    queue.desc[0].flags = VRING_DESC_F_NEXT;
    queue.desc[0].next = 1;
    queue.desc[1].addr = req_data_phys;
    queue.desc[1].len = count * BLK_SECTOR_SIZE;
    queue.desc[1].flags = VRING_DESC_F_NEXT | (type == VIRTIO_BLK_T_IN ? VRING_DESC_F_WRITE : 0);
    queue.desc[1].next = 2;
    queue.desc[2].addr = req_status_phys;
    queue.desc[2].len = 1;
    queue.desc[2].flags = VRING_DESC_F_WRITE;
    queue.desc[2].next = 0;

    virtq_submit(&queue, 0);
    virtio_notify(&dev, &queue);

    // 等设备的完成中断（只收内核消息；这期间客户的请求留在各自的 call 里排队）
    while (!virtq_pop_used(&queue, NULL, NULL)) {
        struct ipc_msg m;
        if (ipc_recv(IPC_FROM_KERNEL, &m) != 0) {
            continue;
        }
        virtio_irq_ack(&dev);
        irq_ack(dev.irq);
    }
    return *req_status == 0;
}

static bool virtio_blk_read(uint64_t sector, uint32_t count, char *buf) {
    if (!do_request(VIRTIO_BLK_T_IN, sector, count)) {
        return false;
    }
    memcpy(buf, req_data, (size_t)count * BLK_SECTOR_SIZE);
    return true;
}

static bool virtio_blk_write(uint64_t sector, uint32_t count, const char *buf) {
    memcpy(req_data, buf, (size_t)count * BLK_SECTOR_SIZE);
    return do_request(VIRTIO_BLK_T_OUT, sector, count);
}

bool virtio_blk_open(struct disk *disk) {
    if (!device_init(&disk->capacity)) {
        return false;
    }
    disk->kind = "virtio-blk";
    disk->irq = dev.irq;
    disk->read = virtio_blk_read;
    disk->write = virtio_blk_write;
    return true;
}
