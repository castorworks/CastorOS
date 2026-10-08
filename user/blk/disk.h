#ifndef _BLK_DISK_H_
#define _BLK_DISK_H_

// 一块磁盘。blk.cpp 通过这几个函数读写它，不关心它是哪种设备；每种设备一个源文件，
// 各自提供一个 <设备>_open：许可给本进程的设备是它那一种、而且能用，就填好 *disk 返回 true。
//
// 许可给本进程的是什么，看许可表的样子就知道，不靠试——把一种设备的寄存器当成另一种的
// 去写，会让设备做出谁也不想要的事：
//   - virtio-blk：一段端口（x86）或一段设备内存（arm64），一条中断线。有它就没有别的
//   - IDE 通道：8 个端口、1 个端口、一条中断线
//   - USB 控制器：一段设备内存，一条中断线（排在 IDE 通道的后面）
// 后两种可以同时有：机器上既有硬盘又插着 U 盘。

#include <types.h>

struct disk {
    const char *kind;       // 设备的种类，只用来打印
    uint64_t capacity;      // 扇区数
    int irq;                // 中断线；-1 表示没有（全靠定时去看）
    /** 读/写 count 个扇区（1..BLK_BUF_SIZE / BLK_SECTOR_SIZE），等设备做完。@return 成功了没有 */
    bool (*read)(uint64_t sector, uint32_t count, char *buf);
    bool (*write)(uint64_t sector, uint32_t count, const char *buf);
    /** 让设备把自己缓存里还没写到介质上的内容写下去（关机前调用）。NULL：写完就已经在介质上了 */
    void (*flush)(void);
};

// 中断（blk.cpp）。本进程里可能有不止一个设备，内核发来的中断消息要按中断线分给它们。
/**
 * 登记：中断线 irq 上的中断来了就调用 handler，它让设备撤销中断并重新打开中断线（irq_ack）。
 * 认领中断线（irq_claim）之后马上登记：认领了却没人应答的中断线会一直关着，同一条线上
 * 别的设备（哪怕在别的进程里）也跟着收不到中断。
 */
void disk_on_irq(int irq, void (*handler)(void));

/**
 * 等中断，最多 ms 毫秒。驱动在等设备做完一步时调用它，然后自己再看一眼设备的状态：
 * 来的中断交给登记的 handler，返回时不一定是调用者等的那一个。
 */
void disk_wait(uint32_t ms);

bool virtio_blk_open(struct disk *disk);
#if !defined(ARCH_ARM64)
bool ata_allowed(void);                 // 许可表里有一个 IDE 通道
bool ata_open(struct disk *disk);       // PC 的 IDE 硬盘
bool usb_allowed(void);                 // 许可表里有一个 USB 控制器
bool usb_open(struct disk *disk);       // 插在 USB 2.0 口上的 U 盘
#endif

#endif // _BLK_DISK_H_
