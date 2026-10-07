#ifndef _BLK_DISK_H_
#define _BLK_DISK_H_

// 一块磁盘。blk.cpp 通过这几个函数读写它，不关心它是哪种设备；每种设备一个源文件，
// 各自提供一个 <设备>_open：许可给本进程的设备是它那一种、而且能用，就填好 *disk 返回 true。

#include <types.h>

struct disk {
    const char *kind;       // 设备的种类，只用来打印
    uint64_t capacity;      // 扇区数
    int irq;
    /** 读/写 count 个扇区（1..BLK_BUF_SIZE / BLK_SECTOR_SIZE），等设备做完。@return 成功了没有 */
    bool (*read)(uint64_t sector, uint32_t count, char *buf);
    bool (*write)(uint64_t sector, uint32_t count, const char *buf);
    /** 没有请求在途时来了一个中断：让设备撤销它，重新打开中断线 */
    void (*stray_irq)(void);
};

bool virtio_blk_open(struct disk *disk);
#if !defined(ARCH_ARM64)
// PC 的 IDE 硬盘。许可表里的端口是谁的要先分清：把 IDE 的端口当成 virtio 的寄存器去写
// （或者反过来）会让设备做出谁也不想要的事
bool ata_allowed(void);                 // 许可给本进程的是一个 IDE 通道
bool ata_open(struct disk *disk);
#endif

#endif // _BLK_DISK_H_
