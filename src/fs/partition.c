// ============================================================================
// partition.c - 分区抽象层实现
// ============================================================================

#include <fs/partition.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <mm/heap.h>

#ifndef UINT32_MAX
#define UINT32_MAX 0xFFFFFFFFu
#endif

// 分区块设备的私有数据
typedef struct partition_blockdev_data {
    partition_t *partition;
} partition_blockdev_data_t;

static int partition_parse_gpt(blockdev_t *dev, partition_t *partitions, uint32_t *count);

// 分区块设备的读取函数
static int partition_blockdev_read(void *dev, uint32_t sector, uint32_t count, uint8_t *buffer) {
    partition_blockdev_data_t *data = (partition_blockdev_data_t *)dev;
    partition_t *part = data->partition;
    
    // 检查边界
    uint64_t request_last = (uint64_t)sector + count;
    if (request_last > part->sector_count) {
        LOG_ERROR_MSG("partition: Read beyond partition size (sector %lu, count %lu, total %llu)\n",
                      (unsigned long)sector, (unsigned long)count, (unsigned long long)part->sector_count);
        return -1;
    }
    
    // 转换为父设备的扇区号
    uint64_t parent_sector = part->start_lba + sector;
    if (parent_sector > UINT32_MAX) {
        LOG_ERROR_MSG("partition: Read LBA out of range for parent device (lba=%llu)\n",
                      (unsigned long long)parent_sector);
        return -1;
    }
    
    // 从父设备读取
    return blockdev_read(part->parent_dev, (uint32_t)parent_sector, count, buffer);
}

// 分区块设备的写入函数
static int partition_blockdev_write(void *dev, uint32_t sector, uint32_t count, const uint8_t *buffer) {
    partition_blockdev_data_t *data = (partition_blockdev_data_t *)dev;
    partition_t *part = data->partition;
    
    // 检查边界
    uint64_t request_last = (uint64_t)sector + count;
    if (request_last > part->sector_count) {
        LOG_ERROR_MSG("partition: Write beyond partition size (sector %lu, count %lu, total %llu)\n",
                      (unsigned long)sector, (unsigned long)count, (unsigned long long)part->sector_count);
        return -1;
    }
    
    // 转换为父设备的扇区号
    uint64_t parent_sector = part->start_lba + sector;
    if (parent_sector > UINT32_MAX) {
        LOG_ERROR_MSG("partition: Write LBA out of range for parent device (lba=%llu)\n",
                      (unsigned long long)parent_sector);
        return -1;
    }
    
    // 写入到父设备
    return blockdev_write(part->parent_dev, (uint32_t)parent_sector, count, buffer);
}

// 分区块设备的大小查询函数
static uint32_t partition_blockdev_get_size(void *dev) {
    partition_blockdev_data_t *data = (partition_blockdev_data_t *)dev;
    if (data->partition->sector_count > UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)data->partition->sector_count;
}

// 分区块设备的块大小查询函数
static uint32_t partition_blockdev_get_block_size(void *dev) {
    partition_blockdev_data_t *data = (partition_blockdev_data_t *)dev;
    return blockdev_get_block_size(data->partition->parent_dev);
}

int partition_parse_mbr(blockdev_t *dev, partition_t *partitions, uint32_t *count) {
    if (!dev || !partitions || !count) {
        return -1;
    }
    
    // 读取 MBR（第一个扇区）
    mbr_boot_sector_t mbr;
    if (blockdev_read(dev, 0, 1, (uint8_t *)&mbr) != 0) {
        LOG_ERROR_MSG("partition: Failed to read MBR\n");
        return -1;
    }
    
    // 检查 MBR 签名
    if (mbr.signature != 0xAA55) {
        LOG_ERROR_MSG("partition: Invalid MBR signature (0x%04X)\n", mbr.signature);
        return -1;
    }
    
    // 检查是否为保护 MBR (GPT)
    for (uint32_t i = 0; i < 4; i++) {
        if (mbr.partitions[i].partition_type == 0xEE) {
            LOG_INFO_MSG("partition: Protective MBR detected, attempting GPT parsing\n");
            return partition_parse_gpt(dev, partitions, count);
        }
    }

    // 解析分区表
    *count = 0;
    for (uint32_t i = 0; i < 4; i++) {
        mbr_partition_entry_t *entry = &mbr.partitions[i];
        
        // 跳过空分区（类型为 0）
        if (entry->partition_type == 0) {
            continue;
        }
        
        // 填充分区信息
        partitions[*count].index = i;
        partitions[*count].start_lba = entry->start_lba;
        partitions[*count].sector_count = entry->sector_count;
        partitions[*count].type = entry->partition_type;
        partitions[*count].active = (entry->boot_flag == 0x80);
        partitions[*count].is_gpt = false;
        memset(&partitions[*count].type_guid, 0, sizeof(gpt_guid_t));
        partitions[*count].parent_dev = dev;
        
        LOG_INFO_MSG("partition: Found partition %u: type=0x%02X, start_lba=%lu, sectors=%lu, active=%s\n",
                     (unsigned int)i, entry->partition_type,
                     (unsigned long)entry->start_lba,
                     (unsigned long)entry->sector_count,
                     partitions[*count].active ? "yes" : "no");
        
        (*count)++;
    }
    
    LOG_INFO_MSG("partition: Parsed %u partitions from MBR\n", (unsigned int)*count);
    return 0;
}

int partition_parse(blockdev_t *dev, partition_t *partitions, uint32_t *count) {
    return partition_parse_mbr(dev, partitions, count);
}

static int partition_parse_gpt(blockdev_t *dev, partition_t *partitions, uint32_t *count) {
    if (!dev || !partitions || !count) {
        return -1;
    }

    uint32_t block_size = blockdev_get_block_size(dev);
    if (block_size == 0) {
        block_size = 512;
    }

    uint8_t *header_buf = (uint8_t *)kmalloc(block_size);
    if (!header_buf) {
        return -1;
    }

    if (blockdev_read(dev, 1, 1, header_buf) != 0) {
        LOG_ERROR_MSG("partition: Failed to read GPT header\n");
        kfree(header_buf);
        return -1;
    }

    gpt_header_t *header = (gpt_header_t *)header_buf;
    const char gpt_signature[8] = { 'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T' };
    if (memcmp(header->signature, gpt_signature, sizeof(gpt_signature)) != 0) {
        LOG_ERROR_MSG("partition: Invalid GPT signature\n");
        kfree(header_buf);
        return -1;
    }

    if (header->sizeof_partition_entry < sizeof(gpt_partition_entry_t) ||
        header->num_partition_entries == 0) {
        LOG_ERROR_MSG("partition: Invalid GPT header parameters\n");
        kfree(header_buf);
        return -1;
    }

    uint32_t entries_to_copy = header->num_partition_entries;
    if (entries_to_copy > MAX_PARTITIONS) {
        entries_to_copy = MAX_PARTITIONS;
    }
    if (entries_to_copy == 0) {
        LOG_WARN_MSG("partition: GPT header reports zero entries\n");
        kfree(header_buf);
        return -1;
    }

    uint64_t entries_lba = header->partition_entries_lba;
    if (entries_lba > UINT32_MAX) {
        LOG_ERROR_MSG("partition: GPT entries LBA exceeds 32-bit limit\n");
        kfree(header_buf);
        return -1;
    }

    uint64_t bytes_needed = (uint64_t)entries_to_copy * header->sizeof_partition_entry;
    uint32_t sectors_to_read = (uint32_t)((bytes_needed + block_size - 1) / block_size);
    if (sectors_to_read == 0) {
        sectors_to_read = 1;
    }

    uint8_t *entry_buf = (uint8_t *)kmalloc((size_t)sectors_to_read * block_size);
    if (!entry_buf) {
        kfree(header_buf);
        return -1;
    }

    if (blockdev_read(dev, (uint32_t)entries_lba, sectors_to_read, entry_buf) != 0) {
        LOG_ERROR_MSG("partition: Failed to read GPT entries\n");
        kfree(entry_buf);
        kfree(header_buf);
        return -1;
    }

    gpt_guid_t zero_guid;
    memset(&zero_guid, 0, sizeof(zero_guid));

    *count = 0;
    for (uint32_t i = 0; i < entries_to_copy && *count < MAX_PARTITIONS; i++) {
        uint8_t *entry_ptr = entry_buf + (size_t)i * header->sizeof_partition_entry;
        gpt_partition_entry_t entry_read;
        memset(&entry_read, 0, sizeof(entry_read));
        size_t copy_size = header->sizeof_partition_entry;
        if (copy_size > sizeof(entry_read)) {
            copy_size = sizeof(entry_read);
        }
        memcpy(&entry_read, entry_ptr, copy_size);

        if (memcmp(&entry_read.type_guid, &zero_guid, sizeof(gpt_guid_t)) == 0) {
            continue;
        }

        if (entry_read.first_lba == 0 && entry_read.last_lba == 0) {
            continue;
        }

        if (entry_read.last_lba < entry_read.first_lba) {
            continue;
        }

        uint64_t sectors = entry_read.last_lba - entry_read.first_lba + 1;
        if (sectors == 0) {
            continue;
        }

        partitions[*count].index = i;
        partitions[*count].start_lba = entry_read.first_lba;
        partitions[*count].sector_count = sectors;
        partitions[*count].type = 0;
        partitions[*count].active = false;
        partitions[*count].is_gpt = true;
        partitions[*count].type_guid = entry_read.type_guid;
        partitions[*count].parent_dev = dev;

        LOG_INFO_MSG("partition: GPT partition %u: first_lba=%llu, last_lba=%llu, sectors=%llu\n",
                     (unsigned int)i,
                     (unsigned long long)entry_read.first_lba,
                     (unsigned long long)entry_read.last_lba,
                     (unsigned long long)sectors);

        (*count)++;
    }

    kfree(entry_buf);
    kfree(header_buf);

    if (*count == 0) {
        LOG_WARN_MSG("partition: No usable GPT partitions found\n");
        return -1;
    }

    LOG_INFO_MSG("partition: Parsed %u partitions from GPT\n", (unsigned int)*count);
    return 0;
}

blockdev_t *partition_create_blockdev(partition_t *part) {
    if (!part || !part->parent_dev) {
        return NULL;
    }
    
    if (part->start_lba > UINT32_MAX || part->sector_count > UINT32_MAX) {
        LOG_ERROR_MSG("partition: Partition %u exceeds 32-bit LBA range (start=%llu, count=%llu)\n",
                      part->index,
                      (unsigned long long)part->start_lba,
                      (unsigned long long)part->sector_count);
        return NULL;
    }
    
    // 分配块设备结构
    blockdev_t *dev = (blockdev_t *)kmalloc(sizeof(blockdev_t));
    if (!dev) {
        LOG_ERROR_MSG("partition: Failed to allocate blockdev\n");
        return NULL;
    }
    
    memset(dev, 0, sizeof(blockdev_t));

    // 分配私有数据
    partition_blockdev_data_t *data = (partition_blockdev_data_t *)kmalloc(sizeof(partition_blockdev_data_t));
    if (!data) {
        kfree(dev);
        LOG_ERROR_MSG("partition: Failed to allocate blockdev data\n");
        return NULL;
    }
    
    // 初始化私有数据
    partition_t *part_copy = (partition_t *)kmalloc(sizeof(partition_t));
    if (!part_copy) {
        kfree(data);
        kfree(dev);
        LOG_ERROR_MSG("partition: Failed to allocate partition copy\n");
        return NULL;
    }
    memcpy(part_copy, part, sizeof(partition_t));
    data->partition = part_copy;
    
    // 初始化块设备
    snprintf(dev->name, sizeof(dev->name), "partition%d", part->index);
    dev->private_data = data;
    dev->block_size = blockdev_get_block_size(part->parent_dev);
    dev->total_sectors = (uint32_t)part->sector_count;
    dev->read = partition_blockdev_read;
    dev->write = partition_blockdev_write;
    dev->get_size = partition_blockdev_get_size;
    dev->get_block_size = partition_blockdev_get_block_size;
    
    if (blockdev_register(dev) != 0) {
        kfree(data);
        kfree(dev);
        return NULL;
    }

    blockdev_retain(dev);
    
    LOG_INFO_MSG("partition: Created blockdev for partition %u (%llu sectors)\n",
                 part->index, (unsigned long long)part->sector_count);
    
    return dev;
}

void partition_destroy_blockdev(blockdev_t *dev) {
    if (!dev) {
        return;
    }
    
    partition_blockdev_data_t *data = (partition_blockdev_data_t *)dev->private_data;

    blockdev_unregister(dev);
    blockdev_release(dev);

    if (data) {
        if (data->partition) {
            kfree(data->partition);
        }
        kfree(data);
    }
    
    kfree(dev);
}

