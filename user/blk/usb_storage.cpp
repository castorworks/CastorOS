// U 盘：USB 大容量存储设备
//
// 分三步走：
//   1. 认设备：复位端口，给设备一个地址，读它的描述符（它是什么、有哪些端点），选中它的配置。
//      这些都走每个 USB 设备都有的控制端点（ehci_control）。
//   2. 说 SCSI：U 盘听的是磁盘的那套命令（查询、读容量、读、写），只是装在 USB 的信封里
//      （“仅批量传输”）：往批量输出端点发一个 31 字节的命令包（CBW），接着在批量端点上
//      传数据，最后从批量输入端点收一个 13 字节的状态包（CSW）。
//   3. 做成一块磁盘（struct disk），交给 blk.cpp。
//
// 只认直接插在机器 USB 2.0 口上的第一个高速 U 盘；只在启动时找一次。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <blk.h>
#include <usb.h>
#include "disk.h"
#include "ehci.h"

#define CLASS_MASS_STORAGE      0x08
#define SUBCLASS_SCSI           0x06
#define PROTOCOL_BULK_ONLY      0x50
#define REQ_MASS_STORAGE_RESET  0xFF

// 仅批量传输的两个包
#define CBW_SIGNATURE   0x43425355u
#define CSW_SIGNATURE   0x53425355u
#define CBW_SIZE        31
#define CSW_SIZE        13
#define CBW_DATA_IN     0x80

// SCSI 命令
#define SCSI_TEST_UNIT_READY    0x00
#define SCSI_REQUEST_SENSE      0x03
#define SCSI_INQUIRY            0x12
#define SCSI_READ_CAPACITY      0x25
#define SCSI_READ_10            0x28
#define SCSI_WRITE_10           0x2A

static uint8_t address;         // 设备的地址
static uint8_t interface;
static uint8_t endpoint_in, endpoint_out;
static uint32_t next_tag = 1;
static bool dead;               // 出过恢复不了的错：之后的请求一律失败

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ============================================================================
// 仅批量传输
// ============================================================================

/** 设备把一个批量端点挂起了（它用这个表示“这一步不行”）：两边都解除 */
static void clear_halt(bool in) {
    ehci_control(USB_TYPE_TO_ENDPOINT, USB_REQ_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_HALT, in ? endpoint_in : endpoint_out,
                 NULL, 0);
    ehci_bulk_reset(in);
}

/** 命令、数据、状态三步走乱了的时候，让设备回到“等一个新命令”的状态 */
static void reset_recovery(void) {
    if (ehci_control(USB_TYPE_CLASS_TO_INTERFACE, REQ_MASS_STORAGE_RESET, 0, interface, NULL, 0) < 0) {
        dead = true;            // 连控制端点都不通了：设备被拔掉了，或者彻底乱了
        return;
    }
    clear_halt(true);
    clear_halt(false);
}

/**
 * 发一条 SCSI 命令。data_in：数据阶段的方向；length 为 0 表示没有数据阶段。
 * @return 0 命令成功；1 设备说命令失败了（可以用 REQUEST SENSE 问原因）；-1 传输出了错
 */
static int scsi(const uint8_t *command, uint8_t command_length, bool data_in, void *data, uint32_t length) {
    if (dead) {
        return -1;
    }
    uint8_t cbw[CBW_SIZE] = {};
    uint32_t tag = next_tag++;
    put_le32(cbw, CBW_SIGNATURE);
    put_le32(cbw + 4, tag);
    put_le32(cbw + 8, length);
    cbw[12] = data_in ? CBW_DATA_IN : 0;
    cbw[13] = 0;                        // 逻辑单元 0
    cbw[14] = command_length;
    memcpy(cbw + 15, command, command_length);

    bool ok = ehci_bulk(false, cbw, CBW_SIZE) == CBW_SIZE;
    if (ok && length > 0 && ehci_bulk(data_in, data, length) < 0) {
        // 数据阶段被设备挂起：解除之后状态包照样要收，里面说命令失败了
        clear_halt(data_in);
    }
    uint8_t csw[CSW_SIZE];
    if (ok && ehci_bulk(true, csw, CSW_SIZE) != CSW_SIZE) {
        clear_halt(true);               // 状态包自己被挂起过一次：解除，再收一次
        ok = ehci_bulk(true, csw, CSW_SIZE) == CSW_SIZE;
    }
    if (!ok || get_le32(csw) != CSW_SIGNATURE || get_le32(csw + 4) != tag || csw[12] > 1) {
        reset_recovery();
        return -1;
    }
    return csw[12];
}

static bool usb_read(uint64_t sector, uint32_t count, char *buf) {
    uint8_t command[10] = { SCSI_READ_10 };
    put_be32(command + 2, (uint32_t)sector);
    command[8] = (uint8_t)count;
    return scsi(command, sizeof(command), true, buf, count * BLK_SECTOR_SIZE) == 0;
}

static bool usb_write(uint64_t sector, uint32_t count, const char *buf) {
    uint8_t command[10] = { SCSI_WRITE_10 };
    put_be32(command + 2, (uint32_t)sector);
    command[8] = (uint8_t)count;
    return scsi(command, sizeof(command), false, (void *)buf, count * BLK_SECTOR_SIZE) == 0;
}

/** 等 U 盘准备好，问它是谁、有多大。@return 是一块 512 字节扇区的盘，*sectors 是它的扇区数 */
static bool scsi_start(uint64_t *sectors) {
    // 刚上电的 U 盘头几次会说“还没准备好”，问一下原因它才往下走
    bool ready = false;
    for (int i = 0; i < 30 && !ready && !dead; i++) {
        uint8_t test[6] = { SCSI_TEST_UNIT_READY };
        int status = scsi(test, sizeof(test), false, NULL, 0);
        if (status == 0) {
            ready = true;
        } else {
            uint8_t sense[18];
            uint8_t request[6] = { SCSI_REQUEST_SENSE, 0, 0, 0, sizeof(sense) };
            scsi(request, sizeof(request), true, sense, sizeof(sense));
            usleep(100000);
        }
    }
    if (!ready) {
        return false;
    }

    uint8_t inquiry[36] = {};
    uint8_t ask[6] = { SCSI_INQUIRY, 0, 0, 0, sizeof(inquiry) };
    if (scsi(ask, sizeof(ask), true, inquiry, sizeof(inquiry)) == 0) {
        // 第 8 字节起是 8 个字符的厂商和 16 个字符的型号，用空格补齐
        char name[25];
        memcpy(name, inquiry + 8, 24);
        int len = 24;
        while (len > 0 && (name[len - 1] == ' ' || name[len - 1] == '\0')) {
            len--;
        }
        name[len] = '\0';
        printf("blk: USB disk \"%s\"\n", name);
    }

    uint8_t capacity[8];
    uint8_t read_capacity[10] = { SCSI_READ_CAPACITY };
    if (scsi(read_capacity, sizeof(read_capacity), true, capacity, sizeof(capacity)) != 0) {
        return false;
    }
    // 最后一个扇区的编号和扇区大小，都是大端
    if (get_be32(capacity + 4) != BLK_SECTOR_SIZE) {
        printf("blk: USB disk with %u-byte sectors is not supported\n", get_be32(capacity + 4));
        return false;
    }
    *sectors = (uint64_t)get_be32(capacity) + 1;
    return true;
}

// ============================================================================
// 认设备
// ============================================================================

/** 在配置描述符里找大容量存储接口和它的两个批量端点。@return 找到了，*config 是这个配置的编号 */
static bool find_storage(const uint8_t *desc, uint32_t total, uint8_t *config, uint16_t *max_packet) {
    // 一个配置描述符后面跟着它的接口描述符，每个接口后面跟着它的端点描述符；
    // 每个描述符开头两个字节是长度和类型
    *config = desc[5];
    bool in_storage = false, have_in = false, have_out = false;
    for (uint32_t off = 0; off + 2 <= total && desc[off] >= 2; off += desc[off]) {
        const uint8_t *d = desc + off;
        if (off + d[0] > total) {
            break;
        }
        if (d[1] == USB_DESC_INTERFACE && d[0] >= 9) {
            if (have_in && have_out) {
                break;              // 上一个接口已经齐了
            }
            in_storage = d[5] == CLASS_MASS_STORAGE && d[6] == SUBCLASS_SCSI && d[7] == PROTOCOL_BULK_ONLY;
            interface = d[2];
            have_in = have_out = false;
        } else if (d[1] == USB_DESC_ENDPOINT && d[0] >= 7 && in_storage &&
                   (d[3] & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_BULK) {
            if (d[2] & USB_ENDPOINT_IN) {
                endpoint_in = d[2] & 0x0F;
                have_in = true;
            } else {
                endpoint_out = d[2] & 0x0F;
                have_out = true;
            }
            *max_packet = (uint16_t)(d[4] | ((d[5] & 0x07) << 8));
        }
    }
    return have_in && have_out;
}

/** 第 port 个端口上是不是一个 U 盘；是的话把它配置好 */
static bool attach(int port) {
    if (!ehci_port_reset(port)) {
        return false;
    }
    // 刚复位的设备在地址 0 上：给它一个自己的地址（之后它要缓一下）
    ehci_set_address(0);
    uint8_t assigned = (uint8_t)(port + 1);
    if (ehci_control(USB_TYPE_TO_DEVICE, USB_REQ_SET_ADDRESS, assigned, 0, NULL, 0) < 0) {
        return false;
    }
    usleep(20000);
    ehci_set_address(assigned);

    // 配置描述符：先读开头 9 个字节，里面有连同接口、端点描述符在内的总长度，再读全
    static uint8_t desc[512];
    if (ehci_control(USB_TYPE_FROM_DEVICE, USB_REQ_GET_DESCRIPTOR, USB_DESC_CONFIGURATION << 8, 0, desc, 9) < 9) {
        return false;
    }
    uint32_t total = (uint32_t)desc[2] | ((uint32_t)desc[3] << 8);
    if (total > sizeof(desc)) {
        total = sizeof(desc);
    }
    long got = ehci_control(USB_TYPE_FROM_DEVICE, USB_REQ_GET_DESCRIPTOR, USB_DESC_CONFIGURATION << 8, 0, desc, (uint16_t)total);
    uint8_t config;
    uint16_t max_packet = 512;
    if (got < 9 || !find_storage(desc, (uint32_t)got, &config, &max_packet)) {
        return false;           // 不是 U 盘（或者是我们不认识的那种）
    }
    if (ehci_control(USB_TYPE_TO_DEVICE, USB_REQ_SET_CONFIGURATION, config, 0, NULL, 0) < 0) {
        return false;
    }
    address = assigned;
    ehci_bulk_setup(address, endpoint_in, endpoint_out, max_packet);
    return true;
}

bool usb_allowed(void) {
    struct hw_range mem;
    return hw_find(HW_MEMORY, 0, &mem);
}

bool usb_open(struct disk *disk) {
    if (!ehci_open()) {
        return false;
    }
    // 每个端口都要复位一遍，找到了 U 盘也一样：低速和全速的设备（键盘）是在复位时认出来、
    // 让给伙伴控制器的，不走这一步它们就一直挂在这个不会和它们说话的控制器上
    bool found = false;
    for (int port = 0; port < ehci_ports(); port++) {
        if (found) {
            ehci_port_reset(port);
            continue;
        }
        uint64_t sectors = 0;
        dead = false;
        if (attach(port) && scsi_start(&sectors)) {
            disk->kind = "USB";
            disk->capacity = sectors;
            disk->irq = ehci_irq();
            disk->read = usb_read;
            disk->write = usb_write;
            found = true;
        }
    }
    if (found) {
        return true;
    }
    ehci_close();       // 没有 U 盘：让控制器歇着
    return false;
}
