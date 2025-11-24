// ============================================================================
// fat32.c - FAT32 文件系统实现
// ============================================================================

#include <fs/fat32.h>
#include <fs/vfs.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <mm/heap.h>

// FAT32 引导扇区（BPB - BIOS Parameter Block）
typedef struct fat32_bpb {
    uint8_t jump[3];              // 跳转指令
    char oem_name[8];             // OEM 名称
    uint16_t bytes_per_sector;    // 每扇区字节数
    uint8_t sectors_per_cluster;  // 每簇扇区数
    uint16_t reserved_sectors;    // 保留扇区数
    uint8_t fat_count;            // FAT 表数量
    uint16_t root_entries;         // 根目录项数（FAT32 中为 0）
    uint16_t total_sectors_16;    // 总扇区数（16 位，FAT32 中为 0）
    uint8_t media_type;           // 媒体类型
    uint16_t sectors_per_fat_16; // 每 FAT 扇区数（16 位，FAT32 中为 0）
    uint16_t sectors_per_track;   // 每磁道扇区数
    uint16_t head_count;          // 磁头数
    uint32_t hidden_sectors;       // 隐藏扇区数
    uint32_t total_sectors_32;    // 总扇区数（32 位）
    
    // FAT32 扩展字段
    uint32_t sectors_per_fat_32;  // 每 FAT 扇区数（32 位）
    uint16_t flags;               // 标志
    uint16_t version;             // 版本
    uint32_t root_cluster;         // 根目录起始簇号
    uint16_t fs_info_sector;       // FSInfo 扇区号
    uint16_t backup_boot_sector;  // 备份引导扇区号
    uint8_t reserved[12];         // 保留
    uint8_t drive_number;         // 驱动器号
    uint8_t reserved2;            // 保留
    uint8_t boot_signature;        // 引导签名（0x29）
    uint32_t volume_id;           // 卷 ID
    char volume_label[11];        // 卷标
    char fs_type[8];             // 文件系统类型（"FAT32   "）
    uint8_t boot_code[420];       // 引导代码
    uint16_t signature;           // 签名（0xAA55）
} __attribute__((packed)) fat32_bpb_t;

// FAT32 目录项（32 字节）
typedef struct fat32_dirent {
    char name[11];                // 8.3 格式文件名
    uint8_t attributes;           // 属性
    uint8_t reserved;             // 保留
    uint8_t create_time_tenth;    // 创建时间（10 毫秒单位）
    uint16_t create_time;         // 创建时间
    uint16_t create_date;         // 创建日期
    uint16_t access_date;          // 访问日期
    uint16_t cluster_high;         // 起始簇号（高 16 位）
    uint16_t modify_time;          // 修改时间
    uint16_t modify_date;          // 修改日期
    uint16_t cluster_low;          // 起始簇号（低 16 位）
    uint32_t file_size;            // 文件大小（字节）
} __attribute__((packed)) fat32_dirent_t;

// FAT32 文件系统私有数据
typedef struct fat32_fs {
    blockdev_t *dev;              // 块设备
    fat32_bpb_t bpb;              // BPB
    uint32_t fat_start_sector;     // FAT 表起始扇区
    uint32_t data_start_sector;   // 数据区起始扇区
    uint32_t root_cluster;         // 根目录簇号
    uint32_t bytes_per_cluster;   // 每簇字节数
    uint32_t total_clusters;      // 数据区簇数量
    uint8_t *fat_cache;          // FAT 表缓存（可选）
    uint32_t fat_cache_sector;    // 缓存的 FAT 扇区号
    uint32_t last_allocated_cluster;  // 上次分配的簇号（用于加速下次分配）
    uint32_t next_free_cluster;   // FSInfo 中的下一个空闲簇号
    uint32_t fsinfo_sector;       // FSInfo 扇区号
} fat32_fs_t;

// FAT32 文件节点私有数据
typedef struct fat32_file {
    fat32_fs_t *fs;               // 文件系统
    uint32_t start_cluster;       // 起始簇号
    uint32_t size;                 // 文件大小
    bool is_dir;                  // 是否为目录
    uint32_t dirent_cluster;      // 目录项所在簇
    uint32_t dirent_offset;       // 目录项偏移（字节）
    uint32_t parent_cluster;      // 父目录簇号
    struct dirent readdir_cache;  // readdir 结果缓冲区（避免静态变量）
} fat32_file_t;

// 目录查找结果
typedef struct fat32_dir_lookup {
    fat32_dirent_t entry;         // 目录项副本
    uint32_t cluster;             // 目录项所在簇
    uint32_t offset;              // 目录项偏移（字节）
} fat32_dir_lookup_t;

// FSInfo 扇区结构（用于加速簇分配）
typedef struct fat32_fsinfo {
    uint32_t lead_sig;                // 前导签名（0x41615252）
    uint8_t reserved1[480];           // 保留
    uint32_t struct_sig;              // 结构签名（0x61417272）
    uint32_t free_clusters;           // 空闲簇数（-1 表示未知）
    uint32_t next_free_cluster;       // 下一个空闲簇号（-1 表示未知）
    uint8_t reserved2[12];            // 保留
    uint32_t trail_sig;               // 尾部签名（0xAA550000）
} __attribute__((packed)) fat32_fsinfo_t;

// 目录属性
#define FAT32_ATTR_READ_ONLY  0x01
#define FAT32_ATTR_HIDDEN     0x02
#define FAT32_ATTR_SYSTEM     0x04
#define FAT32_ATTR_VOLUME_ID  0x08
#define FAT32_ATTR_DIRECTORY  0x10
#define FAT32_ATTR_ARCHIVE    0x20
#define FAT32_ATTR_LONG_NAME  0x0F

// FAT 簇值
#define FAT32_CLUSTER_FREE    0x00000000
#define FAT32_CLUSTER_RESERVED_MIN 0x00000001
#define FAT32_CLUSTER_RESERVED_MAX 0x0FFFFFF6
#define FAT32_CLUSTER_BAD     0x0FFFFFF7
#define FAT32_CLUSTER_EOF_MIN 0x0FFFFFF8
#define FAT32_CLUSTER_EOF_MAX 0x0FFFFFFF

// 前向声明
static uint32_t fat32_cluster_to_sector(fat32_fs_t *fs, uint32_t cluster);
static int fat32_read_cluster(fat32_fs_t *fs, uint32_t cluster, uint8_t *buffer);
static int fat32_dir_create(fs_node_t *node, const char *name);
static int fat32_dir_mkdir(fs_node_t *node, const char *name, uint32_t permissions);
static int fat32_dir_unlink(fs_node_t *node, const char *name);

// ============================================================================
// 内部辅助函数
// ============================================================================

/**
 * 读取 FAT 表项
 */
static uint32_t fat32_read_fat_entry(fat32_fs_t *fs, uint32_t cluster) {
    if (!fs || !fs->dev || cluster < 2 || cluster >= 0x0FFFFFF0) {
        return 0xFFFFFFFF;  // 无效簇号或参数
    }
    
    // 检查簇号是否超出文件系统范围
    if (cluster > fs->total_clusters + 1) {
        return 0xFFFFFFFF;
    }
    
    // 计算 FAT 表中的字节偏移
    uint32_t fat_offset = cluster * 4;  // FAT32 每个表项 4 字节
    uint32_t fat_sector = fs->fat_start_sector + (fat_offset / fs->bpb.bytes_per_sector);
    uint32_t fat_index = fat_offset % fs->bpb.bytes_per_sector;
    
    // 检查扇区边界
    if (fat_index + 4 > fs->bpb.bytes_per_sector) {
        return 0xFFFFFFFF;
    }
    
    // 读取 FAT 扇区
    uint8_t *sector_buffer = (uint8_t *)kmalloc(fs->bpb.bytes_per_sector);
    if (!sector_buffer) {
        LOG_ERROR_MSG("fat32: Failed to allocate sector buffer for FAT read\n");
        return 0xFFFFFFFF;
    }
    
    if (blockdev_read(fs->dev, fat_sector, 1, sector_buffer) != 0) {
        LOG_ERROR_MSG("fat32: Failed to read FAT sector %u\n", fat_sector);
        kfree(sector_buffer);
        return 0xFFFFFFFF;
    }
    
    // 读取 FAT 表项（小端序）
    uint32_t entry = *(uint32_t *)(sector_buffer + fat_index);
    entry &= 0x0FFFFFFF;  // 清除高 4 位（保留位）
    
    kfree(sector_buffer);
    return entry;
}

/**
 * 写入 FAT 表项
 */
static int fat32_write_fat_entry(fat32_fs_t *fs, uint32_t cluster, uint32_t value) {
    if (!fs || !fs->dev || cluster < 2 || cluster > fs->total_clusters + 1) {
        return -1;
    }

    uint32_t fat_offset = cluster * 4;
    uint32_t sector_offset = fat_offset / fs->bpb.bytes_per_sector;
    uint32_t byte_offset = fat_offset % fs->bpb.bytes_per_sector;
    
    // 检查字节边界
    if (byte_offset + 4 > fs->bpb.bytes_per_sector) {
        return -1;
    }

    uint8_t *sector_buffer = (uint8_t *)kmalloc(fs->bpb.bytes_per_sector);
    if (!sector_buffer) {
        LOG_ERROR_MSG("fat32: Failed to allocate sector buffer for FAT write\n");
        return -1;
    }

    // 更新所有 FAT 表副本
    for (uint32_t fat_index = 0; fat_index < fs->bpb.fat_count; fat_index++) {
        uint32_t sector = fs->fat_start_sector +
                          fat_index * fs->bpb.sectors_per_fat_32 +
                          sector_offset;

        if (blockdev_read(fs->dev, sector, 1, sector_buffer) != 0) {
            LOG_ERROR_MSG("fat32: Failed to read FAT sector %u for write\n", sector);
            kfree(sector_buffer);
            return -1;
        }

        uint32_t current = *(uint32_t *)(sector_buffer + byte_offset);
        current &= 0xF0000000;  // 保留高 4 位
        current |= (value & 0x0FFFFFFF);  // 设置低 28 位
        *(uint32_t *)(sector_buffer + byte_offset) = current;

        if (blockdev_write(fs->dev, sector, 1, sector_buffer) != 0) {
            LOG_ERROR_MSG("fat32: Failed to write FAT sector %u\n", sector);
            kfree(sector_buffer);
            return -1;
        }
    }

    kfree(sector_buffer);
    return 0;
}

/**
 * 将簇标记为未使用
 */
static void fat32_free_cluster(fat32_fs_t *fs, uint32_t cluster) {
    if (cluster >= 2) {
        fat32_write_fat_entry(fs, cluster, FAT32_CLUSTER_FREE);
    }
}

static void fat32_free_cluster_chain(fat32_fs_t *fs, uint32_t start_cluster) {
    if (!fs || start_cluster < 2) {
        return;
    }

    uint32_t cluster = start_cluster;
    uint32_t chain_length = 0;
    const uint32_t MAX_CHAIN_LENGTH = fs->total_clusters + 10;  // 防止无限循环
    
    while (cluster >= 2 && cluster < FAT32_CLUSTER_EOF_MIN && chain_length < MAX_CHAIN_LENGTH) {
        uint32_t next = fat32_read_fat_entry(fs, cluster);
        if (next == 0xFFFFFFFF) {
            LOG_WARN_MSG("fat32: Error reading FAT entry during cluster chain free\n");
            break;
        }
        
        fat32_free_cluster(fs, cluster);
        
        if (next >= FAT32_CLUSTER_EOF_MIN) {
            break;
        }
        
        cluster = next;
        chain_length++;
    }
    
    if (chain_length >= MAX_CHAIN_LENGTH) {
        LOG_ERROR_MSG("fat32: Detected potential infinite loop in cluster chain, stopping\n");
    }
}

/**
 * 将簇清零
 */
static int fat32_zero_cluster(fat32_fs_t *fs, uint32_t cluster) {
    if (cluster < 2) {
        return -1;
    }

    uint8_t *buffer = (uint8_t *)kmalloc(fs->bytes_per_cluster);
    if (!buffer) {
        return -1;
    }
    memset(buffer, 0, fs->bytes_per_cluster);

    int ret = blockdev_write(fs->dev,
                             fat32_cluster_to_sector(fs, cluster),
                             fs->bpb.sectors_per_cluster,
                             buffer);
    kfree(buffer);
    return ret;
}

/**
 * 分配新的簇 - 使用 FSInfo 优化
 */
static uint32_t fat32_allocate_cluster(fat32_fs_t *fs) {
    if (!fs) {
        return 0;
    }
    
    uint32_t max_cluster = fs->total_clusters + 1;
    uint32_t start_cluster = 2;

    // 优先使用 FSInfo 中的下一个空闲簇号
    if (fs->next_free_cluster >= 2 && fs->next_free_cluster < max_cluster) {
        start_cluster = fs->next_free_cluster;
    }

    // 第一次扫描：从 FSInfo 指示的位置开始
    for (uint32_t cluster = start_cluster; cluster <= max_cluster; cluster++) {
        uint32_t entry = fat32_read_fat_entry(fs, cluster);
        
        if (entry == 0xFFFFFFFF) {
            LOG_ERROR_MSG("fat32: Failed to read FAT entry for cluster %u\n", cluster);
            return 0;
        }
        if (entry == FAT32_CLUSTER_FREE) {
            // 分配这个簇
            if (fat32_write_fat_entry(fs, cluster, FAT32_CLUSTER_EOF_MAX) != 0) {
                LOG_ERROR_MSG("fat32: Failed to write FAT entry for cluster %u\n", cluster);
                return 0;
            }
            if (fat32_zero_cluster(fs, cluster) != 0) {
                // 回滚 FAT 表项
                LOG_ERROR_MSG("fat32: Failed to zero cluster %u, rolling back\n", cluster);
                fat32_write_fat_entry(fs, cluster, FAT32_CLUSTER_FREE);
                return 0;
            }
            // 更新 FSInfo 中的下一个空闲簇号
            fs->next_free_cluster = cluster + 1;
            return cluster;
        }
    }

    // 第二次扫描：如果第一次没找到，从簇 2 开始重新扫描
    if (start_cluster > 2) {
        for (uint32_t cluster = 2; cluster < start_cluster; cluster++) {
            uint32_t entry = fat32_read_fat_entry(fs, cluster);
            
            if (entry == 0xFFFFFFFF) {
                LOG_ERROR_MSG("fat32: Failed to read FAT entry for cluster %u\n", cluster);
                return 0;
            }
            if (entry == FAT32_CLUSTER_FREE) {
                // 分配这个簇
                if (fat32_write_fat_entry(fs, cluster, FAT32_CLUSTER_EOF_MAX) != 0) {
                    LOG_ERROR_MSG("fat32: Failed to write FAT entry for cluster %u\n", cluster);
                    return 0;
                }
                if (fat32_zero_cluster(fs, cluster) != 0) {
                    // 回滚 FAT 表项
                    LOG_ERROR_MSG("fat32: Failed to zero cluster %u, rolling back\n", cluster);
                    fat32_write_fat_entry(fs, cluster, FAT32_CLUSTER_FREE);
                    return 0;
                }
                // 更新 FSInfo 中的下一个空闲簇号
                fs->next_free_cluster = cluster + 1;
                return cluster;
            }
        }
    }

    LOG_ERROR_MSG("fat32: No free clusters available\n");
    return 0;
}

/**
 * 将字符串转换为 FAT 8.3 格式
 */
static int fat32_make_short_name(const char *name, char out[11]) {
    if (!name || !out) {
        return -1;
    }

    size_t len = strlen(name);
    if (len == 0 || len > 255) {  // 检查最大长度
        return -1;
    }
    
    // 检查特殊名称
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return -1;
    }

    memset(out, ' ', 11);

    int main_index = 0;
    int ext_index = 0;
    bool seen_dot = false;

    for (size_t i = 0; i < len; i++) {
        char c = name[i];

        if (c == '.') {
            if (seen_dot || i == 0) {  // 不允许以点开头或多个点
                return -1;
            }
            seen_dot = true;
            continue;
        }

        // 检查非法字符
        if (c < 0x20 || c == '"' || c == '*' || c == '+' || c == ',' ||
            c == '/' || c == ':' || c == ';' || c == '<' || c == '=' ||
            c == '>' || c == '?' || c == '[' || c == '\\' || c == ']' ||
            c == '|' ) {
            return -1;
        }

        if (c == ' ') {
            return -1;
        }

        // 转换为大写
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        } else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   c == '_' || c == '-' || c == '$' || c == '~') {
            // 允许的字符
        } else {
            return -1;
        }

        if (!seen_dot) {
            if (main_index >= 8) {
                return -1;
            }
            out[main_index++] = c;
        } else {
            if (ext_index >= 3) {
                return -1;
            }
            out[8 + ext_index++] = c;
        }
    }

    if (main_index == 0) {
        return -1;
    }

    return 0;
}

/**
 * 获取目录中新条目的可用槽位
 */
static int fat32_find_free_dir_entry(fat32_file_t *dir, uint32_t *out_cluster,
                                     uint32_t *out_offset, uint32_t *out_prev_cluster,
                                     bool *out_new_cluster) {
    if (!dir || !dir->fs || !out_cluster || !out_offset || !out_prev_cluster) {
        return -1;
    }
    
    if (!dir->is_dir) {
        return -1;
    }

    fat32_fs_t *fs = dir->fs;
    uint8_t *buffer = (uint8_t *)kmalloc(fs->bytes_per_cluster);
    if (!buffer) {
        return -1;
    }

    if (out_new_cluster) {
        *out_new_cluster = false;
    }

    uint32_t current = dir->start_cluster;
    uint32_t last_cluster = 0;
    uint32_t chain_length = 0;
    const uint32_t MAX_CHAIN_LENGTH = fs->total_clusters;  // 防止无限循环

    // 处理根目录或普通目录
    bool is_root = (current == fs->root_cluster);
    
    while ((current >= 2 || is_root) && current < FAT32_CLUSTER_EOF_MIN && chain_length < MAX_CHAIN_LENGTH) {
        if (fat32_read_cluster(fs, current, buffer) != 0) {
            kfree(buffer);
            return -1;
        }

        uint32_t entries = fs->bytes_per_cluster / sizeof(fat32_dirent_t);
        for (uint32_t i = 0; i < entries; i++) {
            uint8_t first = buffer[i * sizeof(fat32_dirent_t)];
            if (first == 0x00 || first == 0xE5) {
                *out_cluster = current;
                *out_offset = i * sizeof(fat32_dirent_t);
                *out_prev_cluster = 0;
                if (out_new_cluster) {
                    *out_new_cluster = false;
                }
                kfree(buffer);
                return 0;
            }
        }

        last_cluster = current;
        uint32_t next = fat32_read_fat_entry(fs, current);
        if (next == 0xFFFFFFFF) {
            kfree(buffer);
            return -1;
        }
        if (next >= FAT32_CLUSTER_EOF_MIN) {
            break;
        }
        current = next;
        is_root = false;  // 只有第一个簇可能是根目录
        chain_length++;
    }

    kfree(buffer);
    
    if (chain_length >= MAX_CHAIN_LENGTH) {
        LOG_ERROR_MSG("fat32: Directory chain too long, possible corruption\n");
        return -1;
    }

    // 需要分配新簇
    uint32_t new_cluster = fat32_allocate_cluster(fs);
    if (new_cluster == 0) {
        return -1;
    }

    if (last_cluster >= 2 && last_cluster < FAT32_CLUSTER_EOF_MIN) {
        if (fat32_write_fat_entry(fs, last_cluster, new_cluster) != 0) {
            fat32_free_cluster(fs, new_cluster);
            return -1;
        }
    } else if (dir->start_cluster < 2 && dir->start_cluster != fs->root_cluster) {
        // 只有在非根目录的情况下才修改起始簇号
        dir->start_cluster = new_cluster;
    }

    *out_cluster = new_cluster;
    *out_offset = 0;
    *out_prev_cluster = last_cluster;
    if (out_new_cluster) {
        *out_new_cluster = true;
    }
    return 0;
}

/**
 * 将目录项写回磁盘
 */
static int fat32_write_dir_entry(fat32_fs_t *fs, uint32_t dir_cluster, uint32_t offset,
                                 const fat32_dirent_t *entry) {
    if (!fs || !entry || offset >= fs->bytes_per_cluster) {
        return -1;
    }
    
    // 簇号应该 >= 2，或者是根目录簇号
    if (dir_cluster < 2 && dir_cluster != fs->root_cluster) {
        return -1;
    }

    uint8_t *buffer = (uint8_t *)kmalloc(fs->bytes_per_cluster);
    if (!buffer) {
        return -1;
    }

    if (fat32_read_cluster(fs, dir_cluster, buffer) != 0) {
        kfree(buffer);
        return -1;
    }

    memcpy(buffer + offset, entry, sizeof(fat32_dirent_t));

    int ret = blockdev_write(fs->dev,
                             fat32_cluster_to_sector(fs, dir_cluster),
                             fs->bpb.sectors_per_cluster,
                             buffer);
    kfree(buffer);
    return ret;
}

/**
 * 回滚新分配的目录簇
 */
static void fat32_revert_new_dir_cluster(fat32_fs_t *fs, uint32_t new_cluster, uint32_t prev_cluster) {
    if (prev_cluster >= 2 && prev_cluster < FAT32_CLUSTER_EOF_MIN) {
        fat32_write_fat_entry(fs, prev_cluster, FAT32_CLUSTER_EOF_MAX);
    }
    fat32_free_cluster(fs, new_cluster);
}

/**
 * 初始化新目录的 "." 和 ".." 条目
 */
static int fat32_initialize_directory_cluster(fat32_fs_t *fs, uint32_t self_cluster, uint32_t parent_cluster) {
    uint8_t *buffer = (uint8_t *)kmalloc(fs->bytes_per_cluster);
    if (!buffer) {
        return -1;
    }
    memset(buffer, 0, fs->bytes_per_cluster);

    fat32_dirent_t dot;
    memset(&dot, 0, sizeof(dot));
    memset(dot.name, ' ', sizeof(dot.name));
    dot.name[0] = '.';
    dot.attributes = FAT32_ATTR_DIRECTORY;
    dot.cluster_low = (uint16_t)(self_cluster & 0xFFFF);
    dot.cluster_high = (uint16_t)((self_cluster >> 16) & 0xFFFF);

    memcpy(buffer, &dot, sizeof(dot));

    fat32_dirent_t dotdot;
    memset(&dotdot, 0, sizeof(dotdot));
    memset(dotdot.name, ' ', sizeof(dotdot.name));
    dotdot.name[0] = '.';
    dotdot.name[1] = '.';
    dotdot.attributes = FAT32_ATTR_DIRECTORY;

    uint32_t parent = parent_cluster;
    if (parent < 2) {
        parent = self_cluster;
    }
    dotdot.cluster_low = (uint16_t)(parent & 0xFFFF);
    dotdot.cluster_high = (uint16_t)((parent >> 16) & 0xFFFF);

    memcpy(buffer + sizeof(fat32_dirent_t), &dotdot, sizeof(fat32_dirent_t));

    int ret = blockdev_write(fs->dev,
                             fat32_cluster_to_sector(fs, self_cluster),
                             fs->bpb.sectors_per_cluster,
                             buffer);
    kfree(buffer);
    return ret;
}

static bool fat32_dir_is_empty(fat32_fs_t *fs, uint32_t dir_cluster) {
    if (!fs || dir_cluster < 2) {
        return true;
    }

    uint8_t *buffer = (uint8_t *)kmalloc(fs->bytes_per_cluster);
    if (!buffer) {
        return false;
    }

    uint32_t current = dir_cluster;
    bool empty = true;
    uint32_t chain_length = 0;
    const uint32_t MAX_CHAIN_LENGTH = fs->total_clusters;

    while (current >= 2 && current < FAT32_CLUSTER_EOF_MIN && chain_length < MAX_CHAIN_LENGTH) {
        if (fat32_read_cluster(fs, current, buffer) != 0) {
            empty = false;
            break;
        }

        uint32_t entries = fs->bytes_per_cluster / sizeof(fat32_dirent_t);
        for (uint32_t i = 0; i < entries; i++) {
            fat32_dirent_t *entry = (fat32_dirent_t *)(buffer + i * sizeof(fat32_dirent_t));
            uint8_t first = entry->name[0];

            if (first == 0x00) {
                goto done;
            }
            if (first == 0xE5) {
                continue;
            }
            if ((entry->attributes & FAT32_ATTR_LONG_NAME) == FAT32_ATTR_LONG_NAME) {
                continue;
            }
            if (entry->attributes & FAT32_ATTR_VOLUME_ID) {
                continue;
            }

            if (entry->name[0] == '.' &&
                (entry->name[1] == ' ' || (entry->name[1] == '.' && entry->name[2] == ' '))) {
                continue;
            }

            empty = false;
            goto done;
        }

        uint32_t next = fat32_read_fat_entry(fs, current);
        if (next == 0xFFFFFFFF || next >= FAT32_CLUSTER_EOF_MIN) {
            break;
        }
        current = next;
        chain_length++;
    }
    
    if (chain_length >= MAX_CHAIN_LENGTH) {
        LOG_WARN_MSG("fat32: Directory chain too long during empty check\n");
        empty = false;
    }

done:
    kfree(buffer);
    return empty;
}

static int fat32_mark_entry_deleted(fat32_fs_t *fs, uint32_t dir_cluster, uint32_t offset) {
    uint8_t *buffer = (uint8_t *)kmalloc(fs->bytes_per_cluster);
    if (!buffer) {
        return -1;
    }

    if (fat32_read_cluster(fs, dir_cluster, buffer) != 0) {
        kfree(buffer);
        return -1;
    }

    buffer[offset] = 0xE5;
    memset(buffer + offset + 1, 0, sizeof(fat32_dirent_t) - 1);

    int ret = blockdev_write(fs->dev,
                             fat32_cluster_to_sector(fs, dir_cluster),
                             fs->bpb.sectors_per_cluster,
                             buffer);
    kfree(buffer);
    return ret;
}

/**
 * 获取指定索引的簇
 */
static int fat32_get_cluster_by_index(fat32_fs_t *fs, uint32_t start_cluster,
                                      uint32_t index, uint32_t *out_cluster) {
    if (!out_cluster || start_cluster < 2) {
        return -1;
    }

    uint32_t cluster = start_cluster;
    for (uint32_t i = 0; i < index; i++) {
        uint32_t next = fat32_read_fat_entry(fs, cluster);
        if (next == 0xFFFFFFFF || next >= FAT32_CLUSTER_EOF_MIN) {
            return -1;
        }
        cluster = next;
    }

    *out_cluster = cluster;
    return 0;
}

/**
 * 确保文件具有足够的簇支持指定大小
 */
static int fat32_ensure_file_size(fat32_file_t *file, uint32_t new_size) {
    fat32_fs_t *fs = file->fs;
    uint32_t cluster_size = fs->bytes_per_cluster;
    uint32_t required_clusters = (new_size == 0) ? 0 :
        ((new_size + cluster_size - 1) / cluster_size);

    uint32_t current_clusters = 0;
    uint32_t last_cluster = 0;
    uint32_t cluster = file->start_cluster;

    if (cluster >= 2) {
        while (true) {
            current_clusters++;
            last_cluster = cluster;
            uint32_t next = fat32_read_fat_entry(fs, cluster);
            if (next == 0xFFFFFFFF) {
                return -1;
            }
            if (next >= FAT32_CLUSTER_EOF_MIN) {
                break;
            }
            cluster = next;
        }
    }

    if (required_clusters == 0) {
        return 0;
    }

    uint32_t prev_cluster = (current_clusters > 0) ? last_cluster : 0;

    while (current_clusters < required_clusters) {
        uint32_t new_cluster = fat32_allocate_cluster(fs);
        if (new_cluster == 0) {
            return -1;
        }

        if (prev_cluster >= 2) {
            if (fat32_write_fat_entry(fs, prev_cluster, new_cluster) != 0) {
                return -1;
            }
        } else {
            file->start_cluster = new_cluster;
        }

        prev_cluster = new_cluster;
        current_clusters++;
    }

    if (prev_cluster >= 2) {
        if (fat32_write_fat_entry(fs, prev_cluster, FAT32_CLUSTER_EOF_MAX) != 0) {
            return -1;
        }
    }

    return 0;
}

/**
 * 将指定范围填充为 0
 */
static int fat32_zero_range(fat32_file_t *file, uint32_t start, uint32_t end) {
    if (start >= end) {
        return 0;
    }

    fat32_fs_t *fs = file->fs;
    uint32_t cluster_size = fs->bytes_per_cluster;
    uint8_t *cluster_buffer = (uint8_t *)kmalloc(cluster_size);
    if (!cluster_buffer) {
        return -1;
    }

    uint32_t position = start;
    while (position < end) {
        uint32_t cluster_index = position / cluster_size;
        uint32_t cluster;
        if (fat32_get_cluster_by_index(fs, file->start_cluster, cluster_index, &cluster) != 0) {
            kfree(cluster_buffer);
            return -1;
        }

        if (fat32_read_cluster(fs, cluster, cluster_buffer) != 0) {
            kfree(cluster_buffer);
            return -1;
        }

        uint32_t cluster_start = cluster_index * cluster_size;
        uint32_t cluster_end = cluster_start + cluster_size;
        uint32_t zero_end = (end < cluster_end) ? end : cluster_end;

        memset(cluster_buffer + (position - cluster_start), 0, zero_end - position);

        if (blockdev_write(fs->dev,
                           fat32_cluster_to_sector(fs, cluster),
                           fs->bpb.sectors_per_cluster,
                           cluster_buffer) != 0) {
            kfree(cluster_buffer);
            return -1;
        }

        position = zero_end;
    }

    kfree(cluster_buffer);
    return 0;
}

/**
 * 更新目录项中的元数据
 */
static int fat32_update_dirent_metadata(fat32_file_t *file) {
    if (!file || !file->fs) {
        return -1;
    }

    if (file->dirent_offset >= file->fs->bytes_per_cluster ||
        file->dirent_cluster < 2) {
        // 根目录或无效目录项
        return 0;
    }

    fat32_fs_t *fs = file->fs;
    uint8_t *buffer = (uint8_t *)kmalloc(fs->bytes_per_cluster);
    if (!buffer) {
        return -1;
    }

    if (fat32_read_cluster(fs, file->dirent_cluster, buffer) != 0) {
        kfree(buffer);
        return -1;
    }

    fat32_dirent_t *entry = (fat32_dirent_t *)(buffer + file->dirent_offset);
    uint32_t cluster = (file->start_cluster >= 2) ? file->start_cluster : 0;
    entry->cluster_low = (uint16_t)(cluster & 0xFFFF);
    entry->cluster_high = (uint16_t)((cluster >> 16) & 0xFFFF);
    entry->file_size = file->size;
    entry->attributes |= FAT32_ATTR_ARCHIVE;

    int ret = blockdev_write(fs->dev,
                             fat32_cluster_to_sector(fs, file->dirent_cluster),
                             fs->bpb.sectors_per_cluster,
                             buffer);

    kfree(buffer);
    return ret;
}
/**
 * 将簇号转换为扇区号
 */
static uint32_t fat32_cluster_to_sector(fat32_fs_t *fs, uint32_t cluster) {
    return fs->data_start_sector + (cluster - 2) * fs->bpb.sectors_per_cluster;
}

/**
 * 读取簇数据
 */
static int fat32_read_cluster(fat32_fs_t *fs, uint32_t cluster, uint8_t *buffer) {
    uint32_t sector = fat32_cluster_to_sector(fs, cluster);
    return blockdev_read(fs->dev, sector, fs->bpb.sectors_per_cluster, buffer);
}

/**
 * 将 8.3 格式文件名转换为普通文件名
 */
static void fat32_format_filename(const char *fat_name, char *name) {
    // 提取主文件名（8 字符）
    int i = 0;
    while (i < 8 && fat_name[i] != ' ' && fat_name[i] != 0) {
        name[i] = fat_name[i];
        i++;
    }
    
    // 检查扩展名
    if (fat_name[8] != ' ' && fat_name[8] != 0) {
        name[i++] = '.';
        int j = 8;
        while (j < 11 && fat_name[j] != ' ' && fat_name[j] != 0) {
            name[i++] = fat_name[j++];
        }
    }
    
    name[i] = '\0';
    
    // 转换为小写
    for (int k = 0; k < i; k++) {
        if (name[k] >= 'A' && name[k] <= 'Z') {
            name[k] = name[k] - 'A' + 'a';
        }
    }
}

/**
 * 检查目录项是否为有效文件
 */
static bool fat32_is_valid_dirent(fat32_dirent_t *dirent) {
    // 检查第一个字节
    uint8_t first_byte = (uint8_t)dirent->name[0];
    if (first_byte == 0x00) {
        return false;  // 空目录项
    }
    if (first_byte == 0xE5) {
        return false;  // 已删除
    }
    if ((dirent->attributes & FAT32_ATTR_LONG_NAME) == FAT32_ATTR_LONG_NAME) {
        return false;  // 长文件名项（暂时跳过）
    }
    if (dirent->attributes & FAT32_ATTR_VOLUME_ID) {
        return false;  // 卷标
    }
    return true;
}

/**
 * 在目录中查找文件
 */
static fat32_dir_lookup_t *fat32_find_file_in_dir(fat32_fs_t *fs, uint32_t dir_cluster, const char *name) {
    if (!fs || dir_cluster < 2) {
        return NULL;
    }

    uint8_t *cluster_buffer = (uint8_t *)kmalloc(fs->bytes_per_cluster);
    if (!cluster_buffer) {
        return NULL;
    }
    
    uint32_t current_cluster = dir_cluster;
    uint32_t chain_length = 0;
    const uint32_t MAX_CHAIN_LENGTH = fs->total_clusters;
    
    while (current_cluster >= 2 && current_cluster < FAT32_CLUSTER_EOF_MIN && chain_length < MAX_CHAIN_LENGTH) {
        // 读取当前簇
        if (fat32_read_cluster(fs, current_cluster, cluster_buffer) != 0) {
            kfree(cluster_buffer);
            return NULL;
        }
        
        // 遍历簇中的目录项
        uint32_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat32_dirent_t);
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat32_dirent_t *dirent = (fat32_dirent_t *)(cluster_buffer + i * sizeof(fat32_dirent_t));
            
            if (!fat32_is_valid_dirent(dirent)) {
                continue;
            }
            
            // 格式化文件名
            char formatted_name[13];
            fat32_format_filename(dirent->name, formatted_name);
            
            // 比较文件名（不区分大小写）
            if (strcasecmp(formatted_name, name) == 0) {
                // 找到文件，复制目录项
                fat32_dir_lookup_t *result = (fat32_dir_lookup_t *)kmalloc(sizeof(fat32_dir_lookup_t));
                if (result) {
                    memcpy(&result->entry, dirent, sizeof(fat32_dirent_t));
                    result->cluster = current_cluster;
                    result->offset = i * sizeof(fat32_dirent_t);
                }
                kfree(cluster_buffer);
                return result;
            }
        }
        
        // 读取下一个簇
        uint32_t next = fat32_read_fat_entry(fs, current_cluster);
        if (next == 0xFFFFFFFF || next >= FAT32_CLUSTER_EOF_MIN) {
            break;
        }
        current_cluster = next;
        chain_length++;
    }
    
    if (chain_length >= MAX_CHAIN_LENGTH) {
        LOG_WARN_MSG("fat32: Directory chain too long during file search\n");
    }
    
    kfree(cluster_buffer);
    return NULL;
}

static int fat32_dir_create_entry(fat32_file_t *dir, const char *name, bool is_dir) {
    if (!dir || !dir->fs || !name) {
        return -1;
    }

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return -1;
    }

    fat32_fs_t *fs = dir->fs;

    fat32_dir_lookup_t *existing = fat32_find_file_in_dir(fs, dir->start_cluster, name);
    if (existing) {
        kfree(existing);
        return -1;
    }

    char short_name[11];
    if (fat32_make_short_name(name, short_name) != 0) {
        return -1;
    }

    uint32_t entry_cluster = 0;
    uint32_t entry_offset = 0;
    uint32_t prev_cluster = 0;
    bool new_dir_cluster = false;
    uint32_t original_start = dir->start_cluster;
    if (fat32_find_free_dir_entry(dir, &entry_cluster, &entry_offset, &prev_cluster, &new_dir_cluster) != 0) {
        return -1;
    }

    fat32_dirent_t entry;
    memset(&entry, 0, sizeof(entry));
    memcpy(entry.name, short_name, sizeof(entry.name));
    entry.attributes = is_dir ? FAT32_ATTR_DIRECTORY : FAT32_ATTR_ARCHIVE;

    uint32_t new_cluster = 0;

    if (is_dir) {
        new_cluster = fat32_allocate_cluster(fs);
        if (new_cluster == 0) {
            if (prev_cluster) {
                fat32_revert_new_dir_cluster(fs, entry_cluster, prev_cluster);
            }
            return -1;
        }

        entry.cluster_low = (uint16_t)(new_cluster & 0xFFFF);
        entry.cluster_high = (uint16_t)((new_cluster >> 16) & 0xFFFF);
        if (fat32_initialize_directory_cluster(fs, new_cluster, dir->start_cluster) != 0) {
            fat32_free_cluster_chain(fs, new_cluster);
            if (new_dir_cluster) {
                fat32_revert_new_dir_cluster(fs, entry_cluster, prev_cluster);
                dir->start_cluster = original_start;
            }
            return -1;
        }
    } else {
        entry.cluster_low = 0;
        entry.cluster_high = 0;
        entry.file_size = 0;
    }

    if (fat32_write_dir_entry(fs, entry_cluster, entry_offset, &entry) != 0) {
        if (is_dir && new_cluster >= 2) {
            fat32_free_cluster_chain(fs, new_cluster);
        }
        if (new_dir_cluster) {
            fat32_revert_new_dir_cluster(fs, entry_cluster, prev_cluster);
            dir->start_cluster = original_start;
        }
        return -1;
    }

    return 0;
}

static int fat32_dir_create(fs_node_t *node, const char *name) {
    if (!node || !name || node->type != FS_DIRECTORY) {
        return -1;
    }
    fat32_file_t *dir = (fat32_file_t *)node->impl;
    if (!dir || !dir->is_dir || !dir->fs) {
        return -1;
    }
    return fat32_dir_create_entry(dir, name, false);
}

static int fat32_dir_mkdir(fs_node_t *node, const char *name, uint32_t permissions) {
    (void)permissions;
    if (!node || !name || node->type != FS_DIRECTORY) {
        return -1;
    }
    fat32_file_t *dir = (fat32_file_t *)node->impl;
    if (!dir || !dir->is_dir || !dir->fs) {
        return -1;
    }
    return fat32_dir_create_entry(dir, name, true);
}

static int fat32_dir_remove_entry(fs_node_t *node, const char *name) {
    if (!node || !name || node->type != FS_DIRECTORY) {
        return -1;
    }

    fat32_file_t *dir = (fat32_file_t *)node->impl;
    if (!dir || !dir->is_dir || !dir->fs) {
        return -1;
    }

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return -1;
    }

    fat32_fs_t *fs = dir->fs;
    fat32_dir_lookup_t *lookup = fat32_find_file_in_dir(fs, dir->start_cluster, name);
    if (!lookup) {
        return -1;
    }

    bool is_directory = (lookup->entry.attributes & FAT32_ATTR_DIRECTORY) != 0;
    uint32_t start_cluster = (((uint32_t)lookup->entry.cluster_high) << 16) | lookup->entry.cluster_low;

    if (is_directory) {
        if (start_cluster == fs->root_cluster) {
            kfree(lookup);
            return -1;
        }
        if (!fat32_dir_is_empty(fs, start_cluster)) {
            kfree(lookup);
            return -1;
        }
    }

    if (start_cluster >= 2) {
        fat32_free_cluster_chain(fs, start_cluster);
    }

    if (fat32_mark_entry_deleted(fs, lookup->cluster, lookup->offset) != 0) {
        kfree(lookup);
        return -1;
    }

    kfree(lookup);
    return 0;
}

static int fat32_dir_unlink(fs_node_t *node, const char *name) {
    return fat32_dir_remove_entry(node, name);
}

// ============================================================================
// VFS 操作函数
// ============================================================================

/**
 * FAT32 文件读取
 */
static uint32_t fat32_file_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    fat32_file_t *file = (fat32_file_t *)node->impl;
    if (!file || file->is_dir) {
        return 0;
    }

    if (file->start_cluster < 2 || file->size == 0) {
        return 0;
    }
    
    // 检查边界
    if (offset >= file->size) {
        return 0;
    }
    if (offset + size > file->size) {
        size = file->size - offset;
    }
    
    fat32_fs_t *fs = file->fs;
    uint32_t bytes_read = 0;
    uint32_t current_cluster = file->start_cluster;
    uint32_t cluster_offset = offset % fs->bytes_per_cluster;
    uint32_t current_cluster_index = offset / fs->bytes_per_cluster;
    
    // 读取数据
    uint8_t *cluster_buffer = (uint8_t *)kmalloc(fs->bytes_per_cluster);
    if (!cluster_buffer) {
        return 0;
    }
    
    // 跳过到起始簇
    for (uint32_t i = 0; i < current_cluster_index; i++) {
        uint32_t next = fat32_read_fat_entry(fs, current_cluster);
        if (next == 0xFFFFFFFF || next >= FAT32_CLUSTER_EOF_MIN) {
            kfree(cluster_buffer);
            return bytes_read;
        }
        current_cluster = next;
    }
    
    uint32_t chain_length = 0;
    const uint32_t MAX_CHAIN_LENGTH = fs->total_clusters;
    
    while (bytes_read < size && current_cluster >= 2 && current_cluster < FAT32_CLUSTER_EOF_MIN && chain_length < MAX_CHAIN_LENGTH) {
        // 读取当前簇
        if (fat32_read_cluster(fs, current_cluster, cluster_buffer) != 0) {
            break;
        }
        
        // 计算本次读取的字节数
        uint32_t to_read = fs->bytes_per_cluster - cluster_offset;
        if (to_read > size - bytes_read) {
            to_read = size - bytes_read;
        }
        
        // 复制数据
        memcpy(buffer + bytes_read, cluster_buffer + cluster_offset, to_read);
        bytes_read += to_read;
        cluster_offset = 0;  // 后续簇从开头读取
        
        // 读取下一个簇
        uint32_t next = fat32_read_fat_entry(fs, current_cluster);
        if (next == 0xFFFFFFFF) {
            break;
        }
        current_cluster = next;
        chain_length++;
    }
    
    if (chain_length >= MAX_CHAIN_LENGTH) {
        LOG_WARN_MSG("fat32: File cluster chain too long during read\n");
    }
    
    kfree(cluster_buffer);
    return bytes_read;
}

/**
 * FAT32 文件写入
 */
static uint32_t fat32_file_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    fat32_file_t *file = (fat32_file_t *)node->impl;
    if (!file || file->is_dir || !buffer || size == 0) {
        return 0;
    }

    fat32_fs_t *fs = file->fs;
    uint32_t cluster_size = fs->bytes_per_cluster;
    uint32_t original_size = file->size;

    uint64_t end_pos64 = (uint64_t)offset + (uint64_t)size;
    if (end_pos64 > 0xFFFFFFFFULL) {
        size = (uint32_t)(0xFFFFFFFFULL - offset);
        if (size == 0) {
            return 0;
        }
        end_pos64 = (uint64_t)offset + (uint64_t)size;
    }

    uint32_t requested_end = (uint32_t)end_pos64;

    if (requested_end > original_size) {
        if (fat32_ensure_file_size(file, requested_end) != 0) {
            return 0;
        }
    } else if (file->start_cluster < 2 && requested_end > 0) {
        if (fat32_ensure_file_size(file, requested_end) != 0) {
            return 0;
        }
    }

    if (file->start_cluster < 2 && requested_end > 0) {
        return 0;
    }

    if (offset > original_size) {
        if (fat32_zero_range(file, original_size, offset) != 0) {
            // 尝试保持文件大小一致
            return 0;
        }
    }

    uint8_t *cluster_buffer = (uint8_t *)kmalloc(cluster_size);
    if (!cluster_buffer) {
        return 0;
    }

    uint32_t bytes_written = 0;
    uint32_t position = offset;
    uint32_t remaining = size;

    while (remaining > 0) {
        uint32_t cluster_index = position / cluster_size;
        uint32_t cluster_offset = position % cluster_size;
        uint32_t cluster;
        if (fat32_get_cluster_by_index(fs, file->start_cluster, cluster_index, &cluster) != 0) {
            break;
        }

        if (fat32_read_cluster(fs, cluster, cluster_buffer) != 0) {
            break;
        }

        uint32_t to_write = cluster_size - cluster_offset;
        if (to_write > remaining) {
            to_write = remaining;
        }

        memcpy(cluster_buffer + cluster_offset, buffer + bytes_written, to_write);

        if (blockdev_write(fs->dev,
                           fat32_cluster_to_sector(fs, cluster),
                           fs->bpb.sectors_per_cluster,
                           cluster_buffer) != 0) {
            break;
        }

        bytes_written += to_write;
        position += to_write;
        remaining -= to_write;
    }

    kfree(cluster_buffer);

    uint32_t final_end = offset + bytes_written;
    if (final_end > file->size) {
        file->size = final_end;
    } else if (file->size < original_size) {
        file->size = original_size;
    }

    if (file->start_cluster >= 2) {
        node->inode = file->start_cluster;
    }

    node->size = file->size;

    fat32_update_dirent_metadata(file);

    return bytes_written;
}

/**
 * FAT32 目录读取
 */
static struct dirent *fat32_dir_readdir(fs_node_t *node, uint32_t index) {
    fat32_file_t *file = (fat32_file_t *)node->impl;
    if (!file || !file->is_dir) {
        return NULL;
    }
    
    fat32_fs_t *fs = file->fs;
    uint8_t *cluster_buffer = (uint8_t *)kmalloc(fs->bytes_per_cluster);
    if (!cluster_buffer) {
        return NULL;
    }
    
    // 使用 fat32_file_t 中的缓冲区，避免静态变量带来的并发问题
    struct dirent *result = &file->readdir_cache;
    uint32_t current_index = 0;
    uint32_t current_cluster = file->start_cluster;
    uint32_t chain_length = 0;
    const uint32_t MAX_CHAIN_LENGTH = fs->total_clusters;
    
    while (current_cluster >= 2 && current_cluster < FAT32_CLUSTER_EOF_MIN && chain_length < MAX_CHAIN_LENGTH) {
        // 读取当前簇
        if (fat32_read_cluster(fs, current_cluster, cluster_buffer) != 0) {
            break;
        }
        
        // 遍历簇中的目录项
        uint32_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat32_dirent_t);
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat32_dirent_t *fat_dirent = (fat32_dirent_t *)(cluster_buffer + i * sizeof(fat32_dirent_t));
            
            if (!fat32_is_valid_dirent(fat_dirent)) {
                continue;
            }
            
            if (current_index == index) {
                // 找到目标项
                char formatted_name[13];
                fat32_format_filename(fat_dirent->name, formatted_name);
                strncpy(result->d_name, formatted_name, sizeof(result->d_name) - 1);
                result->d_name[sizeof(result->d_name) - 1] = '\0';
                
                uint32_t cluster = ((uint32_t)fat_dirent->cluster_high << 16) | fat_dirent->cluster_low;
                result->d_ino = cluster;  // 使用簇号作为 inode
                result->d_reclen = sizeof(struct dirent);
                result->d_off = index + 1;
                
                if (fat_dirent->attributes & FAT32_ATTR_DIRECTORY) {
                    result->d_type = DT_DIR;
                } else {
                    result->d_type = DT_REG;
                }
                
                kfree(cluster_buffer);
                return result;
            }
            
            current_index++;
        }
        
        // 读取下一个簇
        uint32_t next = fat32_read_fat_entry(fs, current_cluster);
        if (next == 0xFFFFFFFF || next >= FAT32_CLUSTER_EOF_MIN) {
            break;
        }
        current_cluster = next;
        chain_length++;
    }
    
    kfree(cluster_buffer);
    return NULL;
}

/**
 * FAT32 目录查找
 */
static fs_node_t *fat32_dir_finddir(fs_node_t *node, const char *name) {
    fat32_file_t *file = (fat32_file_t *)node->impl;
    if (!file || !file->is_dir) {
        return NULL;
    }
    
    fat32_fs_t *fs = file->fs;
    
    // 查找目录项
    fat32_dir_lookup_t *lookup = fat32_find_file_in_dir(fs, file->start_cluster, name);
    if (!lookup) {
        return NULL;
    }
    
    // 创建文件节点
    fs_node_t *new_node = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!new_node) {
        kfree(lookup);
        return NULL;
    }
    
    fat32_file_t *new_file = (fat32_file_t *)kmalloc(sizeof(fat32_file_t));
    if (!new_file) {
        kfree(new_node);
        kfree(lookup);
        return NULL;
    }
    
    // 初始化文件节点
    memset(new_node, 0, sizeof(fs_node_t));
    memset(new_file, 0, sizeof(fat32_file_t));
    strncpy(new_node->name, name, sizeof(new_node->name) - 1);
    
    uint32_t cluster = ((uint32_t)lookup->entry.cluster_high << 16) | lookup->entry.cluster_low;
    new_node->inode = cluster;
    new_node->size = lookup->entry.file_size;
    new_node->permissions = FS_PERM_READ | FS_PERM_WRITE;
    new_node->flags = FS_NODE_FLAG_ALLOCATED;  // 标记为动态分配的节点
    new_node->ref_count = 1;  // 返回时引用计数为 1，表示调用者拥有一个引用
    
    if (lookup->entry.attributes & FAT32_ATTR_DIRECTORY) {
        new_node->type = FS_DIRECTORY;
        new_node->readdir = fat32_dir_readdir;
        new_node->finddir = fat32_dir_finddir;
        new_node->create = fat32_dir_create;
        new_node->mkdir = fat32_dir_mkdir;
        new_node->unlink = fat32_dir_unlink;
        new_node->permissions = FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC;
        new_file->is_dir = true;
    } else {
        new_node->type = FS_FILE;
        new_node->read = fat32_file_read;
        new_node->write = fat32_file_write;
        new_file->is_dir = false;
    }
    
    new_file->fs = fs;
    new_file->start_cluster = cluster;
    new_file->size = lookup->entry.file_size;
    new_file->dirent_cluster = lookup->cluster;
    new_file->dirent_offset = lookup->offset;
    new_file->parent_cluster = file->start_cluster;
    new_node->impl = (uint32_t)new_file;
    
    kfree(lookup);
    return new_node;
}

// ============================================================================
// 公共接口
// ============================================================================

bool fat32_probe(blockdev_t *dev) {
    if (!dev) {
        return false;
    }
    
    // 读取引导扇区
    fat32_bpb_t bpb;
    if (blockdev_read(dev, 0, 1, (uint8_t *)&bpb) != 0) {
        return false;
    }
    
    // 检查签名
    if (bpb.signature != 0xAA55) {
        return false;
    }
    
    // 检查文件系统类型
    if (strncmp(bpb.fs_type, "FAT32   ", 8) != 0) {
        return false;
    }
    
    // 检查每扇区字节数（必须是 512 的倍数）
    if (bpb.bytes_per_sector == 0 || (bpb.bytes_per_sector & (bpb.bytes_per_sector - 1)) != 0) {
        return false;
    }
    
    return true;
}

fs_node_t *fat32_init(blockdev_t *dev) {
    if (!dev) {
        LOG_ERROR_MSG("fat32: Invalid block device\n");
        return NULL;
    }
    
    // 探测文件系统
    if (!fat32_probe(dev)) {
        LOG_ERROR_MSG("fat32: Not a valid FAT32 filesystem\n");
        return NULL;
    }
    
    // 读取 BPB
    fat32_fs_t *fs = (fat32_fs_t *)kmalloc(sizeof(fat32_fs_t));
    if (!fs) {
        LOG_ERROR_MSG("fat32: Failed to allocate filesystem structure\n");
        return NULL;
    }
    
    memset(fs, 0, sizeof(fat32_fs_t));
    fs->dev = blockdev_retain(dev);  // 保留设备引用，防止被销毁
    
    if (blockdev_read(dev, 0, 1, (uint8_t *)&fs->bpb) != 0) {
        blockdev_release(fs->dev);  // 释放设备引用
        kfree(fs);
        LOG_ERROR_MSG("fat32: Failed to read BPB\n");
        return NULL;
    }
    
    // 计算关键扇区位置
    fs->fat_start_sector = fs->bpb.reserved_sectors;
    fs->data_start_sector = fs->fat_start_sector + 
                           (fs->bpb.fat_count * fs->bpb.sectors_per_fat_32);
    fs->root_cluster = fs->bpb.root_cluster;
    fs->bytes_per_cluster = fs->bpb.bytes_per_sector * fs->bpb.sectors_per_cluster;
    
    uint32_t total_sectors = fs->bpb.total_sectors_32 ? fs->bpb.total_sectors_32
                                                     : fs->bpb.total_sectors_16;
    if (fs->bpb.sectors_per_cluster == 0) {
        LOG_ERROR_MSG("fat32: Invalid sectors per cluster (0)\n");
        blockdev_release(fs->dev);  // 释放设备引用
        kfree(fs);
        return NULL;
    }
    uint32_t fats_total = fs->bpb.fat_count * fs->bpb.sectors_per_fat_32;
    if (total_sectors < fs->bpb.reserved_sectors + fats_total) {
        LOG_ERROR_MSG("fat32: Invalid BPB, total sectors too small\n");
        blockdev_release(fs->dev);  // 释放设备引用
        kfree(fs);
        return NULL;
    }
    uint32_t data_sectors = total_sectors - fs->bpb.reserved_sectors - fats_total;
    fs->total_clusters = data_sectors / fs->bpb.sectors_per_cluster;
    if (fs->total_clusters == 0) {
        LOG_ERROR_MSG("fat32: No data clusters available\n");
        blockdev_release(fs->dev);  // 释放设备引用
        kfree(fs);
        return NULL;
    }
    
    // 读取 FSInfo 扇区以获取下一个空闲簇号
    fs->fsinfo_sector = fs->bpb.fs_info_sector;
    fs->next_free_cluster = 2;  // 默认从簇 2 开始
    
    if (fs->fsinfo_sector > 0 && fs->fsinfo_sector < 100) {
        uint8_t *fsinfo_buffer = (uint8_t *)kmalloc(fs->bpb.bytes_per_sector);
        if (fsinfo_buffer) {
            if (blockdev_read(fs->dev, fs->fsinfo_sector, 1, fsinfo_buffer) == 0) {
                fat32_fsinfo_t *fsinfo = (fat32_fsinfo_t *)fsinfo_buffer;
                // 检查 FSInfo 签名
                if (fsinfo->lead_sig == 0x41615252 && fsinfo->struct_sig == 0x61417272) {
                    if (fsinfo->next_free_cluster != 0xFFFFFFFF && fsinfo->next_free_cluster >= 2) {
                        fs->next_free_cluster = fsinfo->next_free_cluster;
                        LOG_INFO_MSG("fat32: FSInfo next_free_cluster: %u\n", fs->next_free_cluster);
                    }
                }
            }
            kfree(fsinfo_buffer);
        }
    }
    
    LOG_INFO_MSG("fat32: Initialized filesystem\n");
    LOG_INFO_MSG("  Bytes per sector: %u\n", fs->bpb.bytes_per_sector);
    LOG_INFO_MSG("  Sectors per cluster: %u\n", fs->bpb.sectors_per_cluster);
    LOG_INFO_MSG("  FAT count: %u\n", fs->bpb.fat_count);
    LOG_INFO_MSG("  Sectors per FAT: %u\n", fs->bpb.sectors_per_fat_32);
    LOG_INFO_MSG("  Root cluster: %u\n", fs->root_cluster);
    LOG_INFO_MSG("  Data start sector: %u\n", fs->data_start_sector);
    LOG_INFO_MSG("  Total clusters: %u\n", fs->total_clusters);
    
    // 创建根目录节点
    fs_node_t *root = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!root) {
        blockdev_release(fs->dev);  // 释放设备引用
        kfree(fs);
        return NULL;
    }
    
    fat32_file_t *root_file = (fat32_file_t *)kmalloc(sizeof(fat32_file_t));
    if (!root_file) {
        kfree(root);
        blockdev_release(fs->dev);  // 释放设备引用
        kfree(fs);
        return NULL;
    }
    
    memset(root, 0, sizeof(fs_node_t));
    memset(root_file, 0, sizeof(fat32_file_t));
    strcpy(root->name, "/");
    root->inode = fs->root_cluster;
    root->type = FS_DIRECTORY;
    root->size = 0;
    root->permissions = FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC;
    root->ref_count = 0;  // 初始化引用计数
    root->readdir = fat32_dir_readdir;
    root->finddir = fat32_dir_finddir;
    root->create = fat32_dir_create;
    root->mkdir = fat32_dir_mkdir;
    root->unlink = fat32_dir_unlink;
    
    root_file->fs = fs;
    root_file->start_cluster = fs->root_cluster;
    root_file->size = 0;
    root_file->is_dir = true;
    root_file->dirent_cluster = 0;
    root_file->dirent_offset = 0;
    root_file->parent_cluster = 0;
    root->impl = (uint32_t)root_file;
    
    LOG_INFO_MSG("fat32: Root directory created\n");
    
    return root;
}

/**
 * 卸载 FAT32 文件系统并释放所有资源
 * @param root 根目录节点
 */
void fat32_deinit(fs_node_t *root) {
    if (!root || !root->impl) {
        LOG_WARN_MSG("fat32_deinit: Invalid root node\n");
        return;
    }
    
    fat32_file_t *root_file = (fat32_file_t *)root->impl;
    if (!root_file || !root_file->fs) {
        LOG_WARN_MSG("fat32_deinit: Invalid root_file or fs\n");
        return;
    }
    
    fat32_fs_t *fs = root_file->fs;
    
    LOG_INFO_MSG("fat32: Unmounting filesystem...\n");
    
    // 释放设备引用
    if (fs->dev) {
        blockdev_release(fs->dev);
        LOG_DEBUG_MSG("fat32: Released block device\n");
    }
    
    // 释放 FAT 缓存（如果有）
    if (fs->fat_cache) {
        kfree(fs->fat_cache);
        LOG_DEBUG_MSG("fat32: Freed FAT cache\n");
    }
    
    // 释放文件系统结构
    kfree(fs);
    LOG_DEBUG_MSG("fat32: Freed filesystem structure\n");
    
    // 释放根文件节点
    kfree(root_file);
    LOG_DEBUG_MSG("fat32: Freed root_file\n");
    
    // 释放根目录节点
    kfree(root);
    LOG_DEBUG_MSG("fat32: Freed root node\n");
    
    LOG_INFO_MSG("fat32: Filesystem unmounted and all resources freed\n");
}

