#ifndef _FS_PARTITION_H_
#define _FS_PARTITION_H_

#include <fs/blockdev.h>
#include <types.h>

/**
 * 分区抽象层
 *
 * 支持 MBR / GPT 分区表，提供分区枚举和访问接口
 */

#define MAX_PARTITIONS 4

// MBR 分区表项（16 字节）
typedef struct mbr_partition_entry {
    uint8_t boot_flag;          // 引导标志（0x80 = 活动分区）
    uint8_t start_head;         // 起始磁头
    uint8_t start_sector;       // 起始扇区（低 6 位）和柱面（高 2 位）
    uint8_t start_cylinder;     // 起始柱面（低 8 位）
    uint8_t partition_type;     // 分区类型
    uint8_t end_head;           // 结束磁头
    uint8_t end_sector;         // 结束扇区（低 6 位）和柱面（高 2 位）
    uint8_t end_cylinder;       // 结束柱面（低 8 位）
    uint32_t start_lba;         // 起始 LBA（小端序）
    uint32_t sector_count;      // 扇区数（小端序）
} __attribute__((packed)) mbr_partition_entry_t;

// MBR 引导扇区结构
typedef struct mbr_boot_sector {
    uint8_t boot_code[446];                    // 引导代码
    mbr_partition_entry_t partitions[4];      // 4 个分区表项
    uint16_t signature;                        // 签名（0xAA55）
} __attribute__((packed)) mbr_boot_sector_t;

// GPT 结构
typedef struct gpt_guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} __attribute__((packed)) gpt_guid_t;

typedef struct gpt_header {
    char signature[8];               // "EFI PART"
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    gpt_guid_t disk_guid;
    uint64_t partition_entries_lba;
    uint32_t num_partition_entries;
    uint32_t sizeof_partition_entry;
    uint32_t partition_entries_crc32;
    uint8_t reserved2[420];
} __attribute__((packed)) gpt_header_t;

typedef struct gpt_partition_entry {
    gpt_guid_t type_guid;
    gpt_guid_t unique_guid;
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint16_t name[36];
} __attribute__((packed)) gpt_partition_entry_t;

// 分区信息
typedef struct partition {
    uint8_t index;                 // 分区索引（0-3）
    uint64_t start_lba;            // 起始 LBA
    uint64_t sector_count;         // 扇区数
    uint8_t type;                  // 分区类型（MBR）
    bool active;                   // 是否为活动分区
    bool is_gpt;                   // 是否来自 GPT
    gpt_guid_t type_guid;          // GPT 分区类型
    blockdev_t *parent_dev;        // 父块设备
} partition_t;

/**
 * 从块设备读取 MBR 并解析分区表
 * @param dev 块设备
 * @param partitions 输出分区数组（至少 MAX_PARTITIONS 个元素）
 * @param count 输出实际找到的分区数
 * @return 0 成功，-1 失败
 */
int partition_parse_mbr(blockdev_t *dev, partition_t *partitions, uint32_t *count);

/**
 * 根据块设备上真实分区表（MBR 或 GPT）解析分区
 * @param dev 块设备
 * @param partitions 输出分区数组
 * @param count 输出分区数量
 * @return 0 成功，-1 失败
 */
int partition_parse(blockdev_t *dev, partition_t *partitions, uint32_t *count);

/**
 * 创建分区块设备（将分区作为块设备访问）
 * @param part 分区信息
 * @return 块设备指针，失败返回 NULL
 */
blockdev_t *partition_create_blockdev(partition_t *part);

/**
 * 释放分区块设备
 * @param dev 分区块设备
 */
void partition_destroy_blockdev(blockdev_t *dev);

#endif // _FS_PARTITION_H_

