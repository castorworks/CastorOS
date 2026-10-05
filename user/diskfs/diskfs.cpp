// diskfs - 磁盘文件系统服务
//
// 非特权的用户态服务，以 "diskfs" 登记；客户用带 "disk:" 前缀的文件名访问它。
// 协议的处理在 user/lib 的 fs_server 里，这里是存储后端，数据经块设备服务
// （blk.h）落在磁盘上，所以内容跨重启保留。
//
// 磁盘格式（块大小 4096 字节）：
//   块 0            超级块
//   块 1..          FAT：每个块一个 32 位表项，记录文件的块链
//   之后 DIR_BLOCKS 块   目录：定长表项，命名空间是平的
//   其余            数据块
// FAT 和目录在内存里各有一份完整的副本；每次修改立刻把涉及的块写回磁盘，
// 没有延迟写，所以不需要 sync。磁盘上没有有效的超级块时自动格式化。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <names.h>
#include <blk.h>
#include <fs.h>
#include <fs_server.h>

#define BLOCK_SIZE          4096
#define SECTORS_PER_BLOCK   (BLOCK_SIZE / BLK_SECTOR_SIZE)

#define FAT_FREE            0u
#define FAT_END             0xFFFFFFFFu
#define FAT_RESERVED        0xFFFFFFFEu     // 超级块、FAT、目录自己占的块

#define DIR_BLOCKS          2
#define MAX_BLOCKS          (256u * 1024)   // 最多管理 1GB

static const char MAGIC[8] = { 'C', 'A', 'S', 'T', 'O', 'R', 'F', 'S' };

struct superblock {
    char magic[8];
    uint32_t version;
    uint32_t total_blocks;
    uint32_t fat_start;
    uint32_t fat_blocks;
    uint32_t dir_start;
    uint32_t dir_blocks;
    uint32_t data_start;
};

struct dir_entry {
    char name[FS_NAME_MAX];     // name[0] == 0 表示空闲
    uint32_t size;
    uint32_t first_block;       // FAT_END 表示还没有数据块
    uint32_t reserved[14];      // 凑成 128 字节
};

#define ENTRIES_PER_BLOCK   (BLOCK_SIZE / sizeof(struct dir_entry))
#define MAX_FILES           (DIR_BLOCKS * ENTRIES_PER_BLOCK)

static struct superblock sb;
static uint32_t *fat;                       // 内存里的 FAT，sb.total_blocks 项
static struct dir_entry dir[MAX_FILES];     // 内存里的目录
static char block_buf[BLOCK_SIZE];          // 读改写数据块用

// ============================================================================
// 块读写
// ============================================================================

static bool block_read(uint32_t block, void *buf) {
    return blk_read((uint64_t)block * SECTORS_PER_BLOCK, buf, SECTORS_PER_BLOCK) == 0;
}

static bool block_write(uint32_t block, const void *buf) {
    return blk_write((uint64_t)block * SECTORS_PER_BLOCK, buf, SECTORS_PER_BLOCK) == 0;
}

/** 把 FAT 里包含第 block 项的那一块写回磁盘 */
static bool fat_flush(uint32_t block) {
    uint32_t index = block / (BLOCK_SIZE / sizeof(uint32_t));
    return block_write(sb.fat_start + index, (char *)fat + (size_t)index * BLOCK_SIZE);
}

/** 把目录里包含第 file 项的那一块写回磁盘 */
static bool dir_flush(int file) {
    uint32_t index = (uint32_t)file / ENTRIES_PER_BLOCK;
    return block_write(sb.dir_start + index, (char *)dir + (size_t)index * BLOCK_SIZE);
}

// ============================================================================
// 块链
// ============================================================================

/** 分配一个空闲块（内容清零），接在 prev 后面（prev 为 FAT_END 表示这是链头） */
static uint32_t chain_append(uint32_t prev) {
    for (uint32_t b = sb.data_start; b < sb.total_blocks; b++) {
        if (fat[b] != FAT_FREE) {
            continue;
        }
        memset(block_buf, 0, BLOCK_SIZE);
        if (!block_write(b, block_buf)) {
            return FAT_END;
        }
        fat[b] = FAT_END;
        bool ok = fat_flush(b);
        if (prev != FAT_END) {
            fat[prev] = b;
            ok = ok && fat_flush(prev);
        }
        return ok ? b : FAT_END;
    }
    return FAT_END;     // 磁盘满了
}

static bool chain_free(uint32_t block) {
    bool ok = true;
    while (block != FAT_END && block >= sb.data_start && block < sb.total_blocks) {
        uint32_t next = fat[block];
        fat[block] = FAT_FREE;
        ok = fat_flush(block) && ok;
        block = next;
    }
    return ok;
}

/**
 * 文件的第 index 个数据块。grow 时链不够长就接着分配。
 * @return 块号，没有（或分配失败）返回 FAT_END
 */
static uint32_t file_block(int file, uint32_t index, bool grow) {
    struct dir_entry *e = &dir[file];
    uint32_t block = e->first_block;
    if (block == FAT_END) {
        if (!grow) {
            return FAT_END;
        }
        block = chain_append(FAT_END);
        if (block == FAT_END) {
            return FAT_END;
        }
        e->first_block = block;
        if (!dir_flush(file)) {
            return FAT_END;
        }
    }
    for (uint32_t i = 0; i < index; i++) {
        uint32_t next = fat[block];
        if (next == FAT_END) {
            if (!grow) {
                return FAT_END;
            }
            next = chain_append(block);
            if (next == FAT_END) {
                return FAT_END;
            }
        }
        block = next;
    }
    return block;
}

// ============================================================================
// 存储后端（文件号就是目录表项的下标）
// ============================================================================

static int diskfs_find(const char *name) {
    for (int i = 0; i < (int)MAX_FILES; i++) {
        if (dir[i].name[0] != '\0' && strcmp(dir[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int diskfs_create(const char *name) {
    for (int i = 0; i < (int)MAX_FILES; i++) {
        if (dir[i].name[0] == '\0') {
            memset(&dir[i], 0, sizeof(dir[i]));
            strcpy(dir[i].name, name);
            dir[i].first_block = FAT_END;
            return dir_flush(i) ? i : -1;
        }
    }
    return -1;
}

static long diskfs_size(int file) {
    return dir[file].size;
}

static long diskfs_read(int file, uint32_t offset, char *buf, uint32_t len) {
    struct dir_entry *e = &dir[file];
    if (offset >= e->size) {
        return 0;
    }
    if (len > e->size - offset) {
        len = e->size - offset;
    }
    uint32_t done = 0;
    while (done < len) {
        uint32_t pos = offset + done;
        uint32_t in_block = pos % BLOCK_SIZE;
        uint32_t n = BLOCK_SIZE - in_block;
        if (n > len - done) {
            n = len - done;
        }
        uint32_t block = file_block(file, pos / BLOCK_SIZE, false);
        if (block == FAT_END || !block_read(block, block_buf)) {
            return -1;
        }
        memcpy(buf + done, block_buf + in_block, n);
        done += n;
    }
    return done;
}

static long diskfs_write(int file, uint32_t offset, const char *buf, uint32_t len) {
    struct dir_entry *e = &dir[file];
    uint32_t done = 0;
    while (done < len) {
        uint32_t pos = offset + done;
        uint32_t in_block = pos % BLOCK_SIZE;
        uint32_t n = BLOCK_SIZE - in_block;
        if (n > len - done) {
            n = len - done;
        }
        // 新分配的块是清零的，所以偏移越过文件末尾留下的空洞读出来是 0
        uint32_t block = file_block(file, pos / BLOCK_SIZE, true);
        if (block == FAT_END) {
            break;      // 磁盘满了：报告已经写进去的部分
        }
        if (n < BLOCK_SIZE && !block_read(block, block_buf)) {
            return -1;
        }
        memcpy(block_buf + in_block, buf + done, n);
        if (!block_write(block, block_buf)) {
            return -1;
        }
        done += n;
    }
    if (offset + done > e->size) {
        e->size = offset + done;
        if (!dir_flush(file)) {
            return -1;
        }
    }
    return done > 0 || len == 0 ? (long)done : -1;
}

static int diskfs_truncate(int file) {
    struct dir_entry *e = &dir[file];
    uint32_t first = e->first_block;
    e->first_block = FAT_END;
    e->size = 0;
    // 先让目录不再指向这条链，再释放它：中途断电最多泄漏几个块，不会出现悬空的链
    return dir_flush(file) && chain_free(first) ? 0 : -1;
}

static int diskfs_remove(int file) {
    uint32_t first = dir[file].first_block;
    memset(&dir[file], 0, sizeof(dir[file]));
    return dir_flush(file) && chain_free(first) ? 0 : -1;
}

static int diskfs_list(int index, char *name, uint32_t *size) {
    for (int i = 0; i < (int)MAX_FILES; i++) {
        if (dir[i].name[0] == '\0') {
            continue;
        }
        if (index == 0) {
            strcpy(name, dir[i].name);
            *size = dir[i].size;
            return 0;
        }
        index--;
    }
    return -1;
}

static const struct fs_backend diskfs_backend = {
    diskfs_find, diskfs_create, diskfs_size, diskfs_read, diskfs_write,
    diskfs_truncate, diskfs_remove, diskfs_list,
};

// ============================================================================
// 挂载和格式化
// ============================================================================

static bool format(uint32_t total_blocks) {
    memset(&sb, 0, sizeof(sb));
    memcpy(sb.magic, MAGIC, sizeof(MAGIC));
    sb.version = 1;
    sb.total_blocks = total_blocks;
    sb.fat_start = 1;
    sb.fat_blocks = (total_blocks * sizeof(uint32_t) + BLOCK_SIZE - 1) / BLOCK_SIZE;
    sb.dir_start = sb.fat_start + sb.fat_blocks;
    sb.dir_blocks = DIR_BLOCKS;
    sb.data_start = sb.dir_start + sb.dir_blocks;

    // 先写 FAT 和目录，最后写超级块：超级块在，说明其余部分都已经就位
    memset(fat, 0, (size_t)sb.fat_blocks * BLOCK_SIZE);
    for (uint32_t b = 0; b < sb.data_start; b++) {
        fat[b] = FAT_RESERVED;
    }
    memset(dir, 0, sizeof(dir));
    for (uint32_t i = 0; i < sb.fat_blocks; i++) {
        if (!block_write(sb.fat_start + i, (char *)fat + (size_t)i * BLOCK_SIZE)) {
            return false;
        }
    }
    for (uint32_t i = 0; i < sb.dir_blocks; i++) {
        if (!block_write(sb.dir_start + i, (char *)dir + (size_t)i * BLOCK_SIZE)) {
            return false;
        }
    }
    memset(block_buf, 0, BLOCK_SIZE);
    memcpy(block_buf, &sb, sizeof(sb));
    return block_write(0, block_buf);
}

/** @return 1 挂载了已有的文件系统，2 新格式化的，0 失败 */
static int mount(void) {
    uint64_t sectors = blk_capacity();
    uint32_t total_blocks = sectors / SECTORS_PER_BLOCK > MAX_BLOCKS
                            ? MAX_BLOCKS : (uint32_t)(sectors / SECTORS_PER_BLOCK);
    if (total_blocks < 16) {
        return 0;
    }

    size_t fat_bytes = (((size_t)total_blocks * sizeof(uint32_t)) + BLOCK_SIZE - 1) & ~(size_t)(BLOCK_SIZE - 1);
    fat = (uint32_t *)mmap(NULL, fat_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (fat == MAP_FAILED || !block_read(0, block_buf)) {
        return 0;
    }

    memcpy(&sb, block_buf, sizeof(sb));
    bool valid = memcmp(sb.magic, MAGIC, sizeof(MAGIC)) == 0 && sb.version == 1 &&
                 sb.total_blocks == total_blocks && sb.dir_blocks == DIR_BLOCKS &&
                 sb.fat_start == 1 && sb.dir_start == sb.fat_start + sb.fat_blocks &&
                 sb.data_start == sb.dir_start + sb.dir_blocks &&
                 (size_t)sb.fat_blocks * BLOCK_SIZE == fat_bytes;
    if (!valid) {
        return format(total_blocks) ? 2 : 0;
    }

    for (uint32_t i = 0; i < sb.fat_blocks; i++) {
        if (!block_read(sb.fat_start + i, (char *)fat + (size_t)i * BLOCK_SIZE)) {
            return 0;
        }
    }
    for (uint32_t i = 0; i < sb.dir_blocks; i++) {
        if (!block_read(sb.dir_start + i, (char *)dir + (size_t)i * BLOCK_SIZE)) {
            return 0;
        }
    }
    return 1;
}

int main() {
    // 块设备驱动与我们同时启动：给它一点时间；没有磁盘时它不会出现
    for (int i = 0; i < 50 && name_lookup(BLK_SERVICE_NAME) == 0; i++) {
        usleep(20000);
    }
    int mounted = mount();
    if (!mounted) {
        printf("diskfs: no usable block device\n");
        return 1;
    }

    int count = 0;
    for (int i = 0; i < (int)MAX_FILES; i++) {
        count += dir[i].name[0] != '\0';
    }
    printf("diskfs: ready (pid %d), %u blocks, %s\n", getpid(), sb.total_blocks,
           mounted == 2 ? "newly formatted" : "mounted");
    if (mounted == 1) {
        printf("diskfs: %d files on disk\n", count);
    }

    fs_serve(FS_DISK_SERVICE_NAME, &diskfs_backend);
    printf("diskfs: cannot register name\n");
    return 1;
}
