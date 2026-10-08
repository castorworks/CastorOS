// IDE 硬盘（ATA，PIO 方式）
//
// PC 的 IDE 控制器每个通道有两组端口：一组 8 个是“任务文件”（数据、扇区数、扇区号、
// 命令/状态），另一个是控制端口；init 许可给本进程的就是这两组端口和通道的中断线，
// 认的是通道上的主盘。读写的步骤：往任务文件里填好从哪个扇区开始、读写几个，写入命令；
// 之后每个扇区的 512 字节通过数据端口一个字一个字地搬（PIO），搬之前等设备说“可以搬了”。
// 设备每忙完一步发一次中断。
//
// 扇区号用 28 位（LBA28）：够用到 128GB。

#include <syscall.h>
#include <stdio.h>
#include <blk.h>
#include "disk.h"

// 任务文件里的寄存器（相对它的端口基址）
#define REG_DATA        0       // 16 位
#define REG_ERROR       1
#define REG_COUNT       2       // 扇区数
#define REG_LBA_LOW     3
#define REG_LBA_MID     4
#define REG_LBA_HIGH    5
#define REG_DRIVE       6       // 选哪块盘，以及扇区号的最高 4 位
#define REG_STATUS      7       // 读：状态（同时撤销设备的中断请求）；写：命令
#define REG_COMMAND     7

#define STATUS_ERROR    0x01
#define STATUS_DATA     0x08    // 可以从数据端口搬一个扇区了
#define STATUS_FAULT    0x20
#define STATUS_BUSY     0x80

#define DRIVE_MASTER    0xA0
#define DRIVE_LBA       0x40    // 扇区号是线性编号，不是柱面/磁头/扇区

#define CONTROL_RESET   0x04    // 写到控制端口：复位通道上的设备

#define CMD_READ        0x20
#define CMD_WRITE       0x30
#define CMD_FLUSH       0xE7    // 把设备自己缓存着的写入真正写到盘上
#define CMD_IDENTIFY    0xEC

// IDENTIFY 应答（256 个字）里用到的几项
#define ID_MODEL            27      // 型号，40 个字符，每个字里高字节在前
#define ID_MODEL_WORDS      20
#define ID_CAPABILITIES     49
#define ID_CAP_LBA          (1 << 9)
#define ID_SECTORS          60      // LBA28 能访问的扇区数，两个字

#define LBA28_SECTORS   (1u << 28)

/** 一个命令最多等这么久（毫秒）：停转的硬盘要先转起来 */
#define COMMAND_TIMEOUT 10000

static uint32_t task_file, control;     // 两组端口的基址
static int ata_irq;

static uint8_t reg_read(uint32_t reg) {
    uint32_t v = 0;
    io_read(task_file + reg, 1, &v);
    return (uint8_t)v;
}

static void reg_write(uint32_t reg, uint8_t value) {
    io_write(task_file + reg, 1, value);
}

/** 控制端口读出来也是状态，但读它不撤销中断请求 */
static uint8_t alt_status(void) {
    uint32_t v = 0;
    io_read(control, 1, &v);
    return (uint8_t)v;
}

/** 写完命令或选完盘，状态要过 400ns 才作数：读一次控制端口大约 100ns */
static void settle(void) {
    for (int i = 0; i < 4; i++) {
        alt_status();
    }
}

static void take_irq(void) {
    reg_read(REG_STATUS);
    irq_ack(ata_irq);
}

/**
 * 等设备忙完。它忙完时发中断；另外每 10ms 自己看一眼，中断没来也不会一直等下去。
 * @return 超时了返回 false
 */
static bool wait_idle(void) {
    uint64_t deadline = uptime_ms() + COMMAND_TIMEOUT;
    bool idle = false;
    for (;;) {
        idle = !(alt_status() & STATUS_BUSY);
        if (idle || uptime_ms() >= deadline) {
            break;
        }
        disk_wait(10);
    }
    return idle;
}

/** 等设备忙完并且没有出错。want_data：还要求它准备好了搬一个扇区 */
static bool wait_ready(bool want_data) {
    settle();
    if (!wait_idle()) {
        return false;
    }
    uint8_t status = reg_read(REG_STATUS);
    if (status & (STATUS_ERROR | STATUS_FAULT)) {
        return false;
    }
    return !want_data || (status & STATUS_DATA);
}

static void read_sector(uint16_t *words) {
    for (int i = 0; i < BLK_SECTOR_SIZE / 2; i++) {
        uint32_t v = 0;
        io_read(task_file + REG_DATA, 2, &v);
        words[i] = (uint16_t)v;
    }
}

static void write_sector(const uint16_t *words) {
    for (int i = 0; i < BLK_SECTOR_SIZE / 2; i++) {
        io_write(task_file + REG_DATA, 2, words[i]);
    }
}

/** 填好任务文件，发出命令 */
static void start(uint8_t command, uint32_t sector, uint32_t count) {
    reg_write(REG_DRIVE, (uint8_t)(DRIVE_MASTER | DRIVE_LBA | ((sector >> 24) & 0x0F)));
    settle();
    reg_write(REG_COUNT, (uint8_t)count);
    reg_write(REG_LBA_LOW, (uint8_t)sector);
    reg_write(REG_LBA_MID, (uint8_t)(sector >> 8));
    reg_write(REG_LBA_HIGH, (uint8_t)(sector >> 16));
    reg_write(REG_COMMAND, command);
}

static bool ata_read(uint64_t sector, uint32_t count, char *buf) {
    start(CMD_READ, (uint32_t)sector, count);
    for (uint32_t i = 0; i < count; i++) {
        if (!wait_ready(true)) {
            return false;
        }
        read_sector((uint16_t *)(buf + (size_t)i * BLK_SECTOR_SIZE));
    }
    return true;
}

static bool ata_write(uint64_t sector, uint32_t count, const char *buf) {
    start(CMD_WRITE, (uint32_t)sector, count);
    for (uint32_t i = 0; i < count; i++) {
        if (!wait_ready(true)) {
            return false;
        }
        write_sector((const uint16_t *)(buf + (size_t)i * BLK_SECTOR_SIZE));
    }
    if (!wait_ready(false)) {
        return false;
    }
    // 应答了“写好了”，数据就得真的在盘上：断电也不丢
    start(CMD_FLUSH, 0, 0);
    return wait_ready(false);
}

/** 复位通道，向主盘要它的身份信息。@return 那里有一块能用的硬盘 */
static bool identify(uint16_t *id) {
    if (reg_read(REG_STATUS) == 0xFF) {
        return false;           // 端口后面什么都没有
    }
    io_write(control, 1, CONTROL_RESET);
    usleep(1000);
    io_write(control, 1, 0);    // 复位结束；设备的中断开着
    usleep(2000);
    if (!wait_idle()) {
        return false;
    }

    start(CMD_IDENTIFY, 0, 0);
    settle();
    if (reg_read(REG_STATUS) == 0) {
        return false;           // 通道上没有主盘
    }
    // 光驱之类不认这个命令：报错，或者根本不准备数据
    if (!wait_ready(true)) {
        return false;
    }
    read_sector(id);
    return (id[ID_CAPABILITIES] & ID_CAP_LBA) != 0;
}

// IDE 通道的许可表是这个样子：8 个端口的任务文件、1 个控制端口、一条中断线
static bool find_channel(struct hw_range *files, struct hw_range *ctl, struct hw_range *irq) {
    return hw_find(HW_PORTS, 0, files) && files->count == 8 && hw_find(HW_PORTS, 1, ctl) &&
           ctl->count == 1 && hw_find(HW_IRQ, 0, irq);
}

bool ata_allowed(void) {
    struct hw_range files, ctl, irq;
    return find_channel(&files, &ctl, &irq);
}

bool ata_open(struct disk *disk) {
    struct hw_range files, ctl, irq;
    if (!find_channel(&files, &ctl, &irq)) {
        return false;
    }
    task_file = (uint32_t)files.start;
    control = (uint32_t)ctl.start;
    ata_irq = (int)irq.start;
    if (irq_claim(ata_irq) != 0) {
        printf("blk: cannot claim IRQ %d\n", ata_irq);
        return false;
    }
    disk_on_irq(ata_irq, take_irq);

    static uint16_t id[BLK_SECTOR_SIZE / 2];
    if (!identify(id)) {
        return false;
    }
    uint32_t sectors = (uint32_t)id[ID_SECTORS] | ((uint32_t)id[ID_SECTORS + 1] << 16);
    if (sectors == 0) {
        return false;
    }

    char model[ID_MODEL_WORDS * 2 + 1];
    for (int i = 0; i < ID_MODEL_WORDS; i++) {
        model[i * 2] = (char)(id[ID_MODEL + i] >> 8);
        model[i * 2 + 1] = (char)id[ID_MODEL + i];
    }
    int len = ID_MODEL_WORDS * 2;
    while (len > 0 && model[len - 1] == ' ') {
        len--;
    }
    model[len] = '\0';
    printf("blk: IDE disk \"%s\"\n", model);

    disk->kind = "IDE";
    disk->capacity = sectors < LBA28_SECTORS ? sectors : LBA28_SECTORS;
    disk->irq = ata_irq;
    disk->read = ata_read;
    disk->write = ata_write;
    return true;
}
