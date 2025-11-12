#include <drivers/ata.h>
#include <fs/blockdev.h>
#include <kernel/io.h>
#include <kernel/irq.h>
#include <lib/klog.h>
#include <lib/string.h>

#define ATA_PRIMARY_IO_BASE   0x1F0
#define ATA_PRIMARY_CTRL_BASE 0x3F6

#define ATA_REG_DATA       0x00
#define ATA_REG_ERROR      0x01
#define ATA_REG_FEATURES   0x01
#define ATA_REG_SECCOUNT0  0x02
#define ATA_REG_LBA0       0x03
#define ATA_REG_LBA1       0x04
#define ATA_REG_LBA2       0x05
#define ATA_REG_HDDEVSEL   0x06
#define ATA_REG_COMMAND    0x07
#define ATA_REG_STATUS     0x07

#define ATA_SR_ERR 0x01
#define ATA_SR_DRQ 0x08
#define ATA_SR_SRV 0x10
#define ATA_SR_DF  0x20
#define ATA_SR_RDY 0x40
#define ATA_SR_BSY 0x80

#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_IDENTIFY      0xEC

#define ATA_SECTOR_SIZE 512

typedef struct ata_device {
    uint16_t io_base;
    uint16_t ctrl_base;
    uint8_t drive;          // 0 = master, 1 = slave
    bool present;
    uint32_t total_sectors;
} ata_device_t;

static ata_device_t ata_primary_master = {
    .io_base = ATA_PRIMARY_IO_BASE,
    .ctrl_base = ATA_PRIMARY_CTRL_BASE,
    .drive = 0,
    .present = false,
    .total_sectors = 0,
};

static blockdev_t ata_blockdev;

static void ata_io_wait(ata_device_t *dev) {
    inb(dev->ctrl_base);
    inb(dev->ctrl_base);
    inb(dev->ctrl_base);
    inb(dev->ctrl_base);
}

static int ata_wait_status(ata_device_t *dev, uint8_t mask, uint8_t expected, bool allow_err) {
    for (uint32_t i = 0; i < 100000; i++) {
        uint8_t status = inb(dev->io_base + ATA_REG_STATUS);
        if (!allow_err && (status & ATA_SR_ERR)) {
            return -1;
        }
        if ((status & ATA_SR_DF)) {
            return -1;
        }
        if ((status & mask) == expected) {
            return 0;
        }
    }
    return -1;
}

static int ata_poll_ready(ata_device_t *dev) {
    if (ata_wait_status(dev, ATA_SR_BSY, 0, false) != 0) {
        return -1;
    }
    return ata_wait_status(dev, ATA_SR_DRQ, ATA_SR_DRQ, false);
}

static void ata_select_drive(ata_device_t *dev, uint32_t lba) {
    outb(dev->io_base + ATA_REG_HDDEVSEL,
         (uint8_t)(0xE0 | (dev->drive << 4) | ((lba >> 24) & 0x0F)));
    ata_io_wait(dev);
}

static int ata_identify(ata_device_t *dev) {
    ata_select_drive(dev, 0);

    outb(dev->io_base + ATA_REG_SECCOUNT0, 0);
    outb(dev->io_base + ATA_REG_LBA0, 0);
    outb(dev->io_base + ATA_REG_LBA1, 0);
    outb(dev->io_base + ATA_REG_LBA2, 0);

    outb(dev->io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_io_wait(dev);

    uint8_t status = inb(dev->io_base + ATA_REG_STATUS);
    if (status == 0) {
        return -1;
    }

    while ((status & ATA_SR_BSY) != 0) {
        status = inb(dev->io_base + ATA_REG_STATUS);
    }

    uint8_t cl = inb(dev->io_base + ATA_REG_LBA1);
    uint8_t ch = inb(dev->io_base + ATA_REG_LBA2);
    if (cl != 0 || ch != 0) {
        return -1;
    }

    if (ata_poll_ready(dev) != 0) {
        return -1;
    }

    uint16_t identify_buffer[256];
    for (uint32_t i = 0; i < 256; i++) {
        identify_buffer[i] = inw(dev->io_base + ATA_REG_DATA);
    }

    uint32_t total_sectors = ((uint32_t)identify_buffer[61] << 16) | identify_buffer[60];
    if (total_sectors == 0) {
        return -1;
    }

    dev->total_sectors = total_sectors;
    dev->present = true;
    return 0;
}

static int ata_pio_read_sector(ata_device_t *dev, uint32_t lba, uint8_t *buffer) {
    ata_select_drive(dev, lba);

    outb(dev->io_base + ATA_REG_FEATURES, 0);
    outb(dev->io_base + ATA_REG_SECCOUNT0, 1);
    outb(dev->io_base + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(dev->io_base + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(dev->io_base + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(dev->io_base + ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);

    if (ata_poll_ready(dev) != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
        uint16_t data = inw(dev->io_base + ATA_REG_DATA);
        ((uint16_t *)buffer)[i] = data;
    }

    ata_io_wait(dev);
    return 0;
}

static int ata_pio_write_sector(ata_device_t *dev, uint32_t lba, const uint8_t *buffer) {
    ata_select_drive(dev, lba);

    outb(dev->io_base + ATA_REG_FEATURES, 0);
    outb(dev->io_base + ATA_REG_SECCOUNT0, 1);
    outb(dev->io_base + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(dev->io_base + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(dev->io_base + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(dev->io_base + ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);

    if (ata_poll_ready(dev) != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
        outw(dev->io_base + ATA_REG_DATA, ((const uint16_t *)buffer)[i]);
    }

    ata_io_wait(dev);

    if (ata_wait_status(dev, ATA_SR_BSY, 0, false) != 0) {
        return -1;
    }
    return 0;
}

static int ata_blockdev_read(void *dev_ptr, uint32_t sector, uint32_t count, uint8_t *buffer) {
    ata_device_t *dev = (ata_device_t *)dev_ptr;
    if (!dev || !dev->present || !buffer || count == 0) {
        return -1;
    }

    if (sector + count > dev->total_sectors) {
        LOG_ERROR_MSG("ata: Read beyond device size (lba %u, count %u, total %u)\n",
                      sector, count, dev->total_sectors);
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (ata_pio_read_sector(dev, sector + i, buffer + i * ATA_SECTOR_SIZE) != 0) {
            LOG_ERROR_MSG("ata: Read sector %u failed\n", sector + i);
            return -1;
        }
    }
    return 0;
}

static int ata_blockdev_write(void *dev_ptr, uint32_t sector, uint32_t count, const uint8_t *buffer) {
    ata_device_t *dev = (ata_device_t *)dev_ptr;
    if (!dev || !dev->present || !buffer || count == 0) {
        return -1;
    }

    if (sector + count > dev->total_sectors) {
        LOG_ERROR_MSG("ata: Write beyond device size (lba %u, count %u, total %u)\n",
                      sector, count, dev->total_sectors);
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (ata_pio_write_sector(dev, sector + i, buffer + i * ATA_SECTOR_SIZE) != 0) {
            LOG_ERROR_MSG("ata: Write sector %u failed\n", sector + i);
            return -1;
        }
    }
    return 0;
}

void ata_init(void) {
    irq_disable_line(14);
    if (ata_identify(&ata_primary_master) != 0) {
        LOG_WARN_MSG("ata: No ATA device detected on primary master\n");
        return;
    }

    memset(&ata_blockdev, 0, sizeof(ata_blockdev));
    strcpy(ata_blockdev.name, "ata0");
    ata_blockdev.private_data = &ata_primary_master;
    ata_blockdev.block_size = ATA_SECTOR_SIZE;
    ata_blockdev.total_sectors = ata_primary_master.total_sectors;
    ata_blockdev.read = ata_blockdev_read;
    ata_blockdev.write = ata_blockdev_write;
    ata_blockdev.get_size = NULL;
    ata_blockdev.get_block_size = NULL;

    if (blockdev_register(&ata_blockdev) != 0) {
        LOG_ERROR_MSG("ata: Failed to register block device\n");
        return;
    }

    LOG_INFO_MSG("ata: Primary master detected, %u sectors (approx %u MB)\n",
                 ata_primary_master.total_sectors,
                 (ata_primary_master.total_sectors / 2048));
}


