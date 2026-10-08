// blk - 块设备驱动
//
// 用户态驱动，以 "blk" 登记，实现 blk.h 里的协议。没有特权：只碰得到 init 许可给它的
// 那些设备。磁盘可以是 virtio-blk（virtio_blk.cpp），PC 上也可以是 IDE 硬盘（ata.cpp）和
// U 盘（usb_storage.cpp，经 ehci.cpp 的 USB 控制器）；这里是它们共用的部分：收请求、
// 检查范围、在客户的共享缓冲区和磁盘之间搬数据。
// 一次只处理一个请求，这期间别的客户的请求留在各自的 call 里排队。

#include <syscall.h>
#include <stdio.h>
#include <names.h>
#include <blk.h>
#include <clients.h>
#include "disk.h"

// 找到的磁盘。客户的请求里带着磁盘的编号（见 blk.h）
#define MAX_DISKS 4
static struct disk disks[MAX_DISKS];
static int disk_count;

// 认领了的中断线，和线上的中断来了该叫谁
#define MAX_IRQS 4
static struct {
    int irq;
    void (*handler)(void);
} irqs[MAX_IRQS];
static int irq_count;

void disk_on_irq(int irq, void (*handler)(void)) {
    if (irq_count < MAX_IRQS) {
        irqs[irq_count].irq = irq;
        irqs[irq_count].handler = handler;
        irq_count++;
    }
}

/** 一条内核消息：是中断的话交给那条中断线上的设备（定时器到期的消息不用管） */
static void kernel_message(const struct ipc_msg *m) {
    if (m->label != IPC_LABEL_IRQ) {
        return;
    }
    for (int i = 0; i < irq_count; i++) {
        if (irqs[i].irq == (int)m->data[0]) {
            irqs[i].handler();
        }
    }
}

void disk_wait(uint32_t ms) {
    timer_set(ms);
    struct ipc_msg m;
    if (ipc_recv(IPC_FROM_KERNEL, &m) == 0) {
        kernel_message(&m);
    }
    timer_set(0);
}

// 许可给我们的是哪些设备，就打开哪些
static void open_disks(void) {
#if !defined(ARCH_ARM64)
    bool pc_disks = false;
    if (ata_allowed()) {
        pc_disks = true;
        disk_count += ata_open(&disks[disk_count]);
    }
    if (usb_allowed()) {
        pc_disks = true;
        disk_count += usb_open(&disks[disk_count]);
    }
    if (pc_disks) {
        return;
    }
#endif
    disk_count += virtio_blk_open(&disks[disk_count]);
}

int main() {
    open_disks();
    if (disk_count == 0) {
        printf("blk: no usable disk\n");
        return 1;
    }
    clients_init(BLK_BUF_SIZE, NULL);
    if (name_register(BLK_SERVICE_NAME) != 0) {
        printf("blk: cannot register name\n");
        return 1;
    }
    for (int i = 0; i < disk_count; i++) {
        printf("blk: disk %d: %s, irq %d, %u sectors (%u MB)\n", i, disks[i].kind, disks[i].irq,
               (uint32_t)disks[i].capacity, (uint32_t)(disks[i].capacity / 2048));
    }
    printf("blk: driver ready (pid %d)\n", getpid());

    struct ipc_msg m;
    for (;;) {
        if (ipc_recv(IPC_ANY, &m) != 0) {
            continue;
        }
        if (m.sender == IPC_KERNEL) {
            kernel_message(&m);     // 没有请求在途时到来的中断
            continue;
        }
        if (m.label == IPC_LABEL_GRANT) {
            clients_attach(&m);
            continue;
        }

        if (m.label == BLK_STOP) {
            // 要关机了：一次只处理一个请求，所以这时没有写到一半的。断电之前盘上要有全部内容
            for (int i = 0; i < disk_count; i++) {
                if (disks[i].flush) {
                    disks[i].flush();
                }
            }
            struct ipc_msg stopped = {};
            stopped.label = BLK_STOP;
            ipc_reply(m.sender, &stopped);
            printf("blk: stopped\n");
            return 0;
        }

        struct ipc_msg reply = {};
        reply.label = m.label;
        int64_t result = -1;

        char *buf = clients_buf((int)m.sender);
        uint64_t which = m.label == BLK_INFO ? m.data[0] : m.data[2];
        struct disk *disk = which < (uint64_t)disk_count ? &disks[which] : NULL;
        if (m.label == BLK_INFO && disk) {
            result = 0;
            reply.data[1] = disk->capacity;
            reply.data[2] = (uint64_t)disk_count;
        } else if ((m.label == BLK_READ || m.label == BLK_WRITE) && buf && disk) {
            uint64_t sector = m.data[0];
            uint64_t count = m.data[1];
            if (count >= 1 && count <= BLK_BUF_SIZE / BLK_SECTOR_SIZE &&
                sector < disk->capacity && count <= disk->capacity - sector) {
                bool ok = m.label == BLK_WRITE ? disk->write(sector, (uint32_t)count, buf)
                                               : disk->read(sector, (uint32_t)count, buf);
                result = ok ? 0 : -1;
            }
        }

        reply.data[0] = (uint64_t)result;
        ipc_reply(m.sender, &reply);
    }
}
