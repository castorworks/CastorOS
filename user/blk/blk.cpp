// blk - 块设备驱动
//
// 用户态驱动，以 "blk" 登记，实现 blk.h 里的协议。没有特权：只碰得到 init 许可给它的
// 那个设备。设备可以是 virtio-blk（virtio_blk.cpp），PC 上也可以是 IDE 硬盘（ata.cpp）；
// 这里是两者共用的部分：收请求、检查范围、在客户的共享缓冲区和磁盘之间搬数据。
// 一次只处理一个请求，这期间别的客户的请求留在各自的 call 里排队。

#include <syscall.h>
#include <stdio.h>
#include <names.h>
#include <blk.h>
#include <clients.h>
#include "disk.h"

static struct disk disk;

// 许可给我们的是哪种设备，就用哪种
static bool disk_open(void) {
#if !defined(ARCH_ARM64)
    if (ata_allowed()) {
        return ata_open(&disk);
    }
#endif
    return virtio_blk_open(&disk);
}

int main() {
    if (!disk_open()) {
        printf("blk: no usable disk\n");
        return 1;
    }
    clients_init(BLK_BUF_SIZE, NULL);
    if (name_register(BLK_SERVICE_NAME) != 0) {
        printf("blk: cannot register name\n");
        return 1;
    }
    printf("blk: driver ready (pid %d, %s, irq %d), %u sectors (%u MB)\n",
           getpid(), disk.kind, disk.irq, (uint32_t)disk.capacity, (uint32_t)(disk.capacity / 2048));

    struct ipc_msg m;
    for (;;) {
        if (ipc_recv(IPC_ANY, &m) != 0) {
            continue;
        }
        if (m.sender == IPC_KERNEL) {
            if (m.label == IPC_LABEL_IRQ) {
                disk.stray_irq();
            }
            continue;
        }
        if (m.label == IPC_LABEL_GRANT) {
            clients_attach(&m);
            continue;
        }

        struct ipc_msg reply = {};
        reply.label = m.label;
        int64_t result = -1;

        char *buf = clients_buf((int)m.sender);
        if (m.label == BLK_INFO) {
            result = 0;
            reply.data[1] = disk.capacity;
        } else if ((m.label == BLK_READ || m.label == BLK_WRITE) && buf) {
            uint64_t sector = m.data[0];
            uint64_t count = m.data[1];
            if (count >= 1 && count <= BLK_BUF_SIZE / BLK_SECTOR_SIZE &&
                sector < disk.capacity && count <= disk.capacity - sector) {
                bool ok = m.label == BLK_WRITE ? disk.write(sector, (uint32_t)count, buf)
                                               : disk.read(sector, (uint32_t)count, buf);
                result = ok ? 0 : -1;
            }
        }

        reply.data[0] = (uint64_t)result;
        ipc_reply(m.sender, &reply);
    }
}
