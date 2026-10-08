// diskfs - 磁盘文件系统服务
//
// 非特权的用户态服务，以 "diskfs" 登记。磁盘上有我们的文件系统时它就是根文件系统
// （见 <fs.h>）。协议的处理在 user/lib 的 fs_server 里，这里是存储后端，数据经块设备服务
// （blk.h）落在磁盘上，所以内容跨重启保留。磁盘上的格式在 <diskfs_format.h>。
//
// FAT 和目录在内存里各有一份完整的副本；每次修改立刻把涉及的块写回磁盘，
// 没有延迟写，所以不需要 sync。最近用过的数据块也留在内存里（缓存），读的时候不用再去
// 找驱动；写仍然立刻落盘。
//
// 这里从不格式化：文件系统是构建时做好的（tools/mkdiskfs.cpp）。磁盘上找不到它——
// 没有磁盘，或者盘上是别的东西——就退出，一个字节也不写。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <names.h>
#include <blk.h>
#include <fs.h>
#include <fs_server.h>
#include <diskfs_format.h>

static_assert(DISKFS_NAME_MAX == FS_NAME_MAX && DISKFS_SECTOR_SIZE == BLK_SECTOR_SIZE);

#define BLOCK_SIZE          DISKFS_BLOCK_SIZE
#define SECTORS_PER_BLOCK   (BLOCK_SIZE / BLK_SECTOR_SIZE)
#define FAT_FREE            DISKFS_FAT_FREE
#define FAT_END             DISKFS_FAT_END
#define ENTRY_DIR           DISKFS_ENTRY_DIR
#define ENTRIES_PER_BLOCK   DISKFS_ENTRIES_PER_BLOCK
#define MAX_FILES           DISKFS_MAX_FILES

#define superblock          diskfs_superblock
#define dir_entry           diskfs_dir_entry

static uint64_t fs_start;                   // 文件系统从磁盘的第几个扇区开始（在分区里时不是 0）
static struct superblock sb;
static uint32_t *fat;                       // 内存里的 FAT，sb.total_blocks 项
static struct dir_entry dir[MAX_FILES];     // 内存里的目录
static char block_buf[BLOCK_SIZE];          // 读改写数据块用

// ============================================================================
// 块读写
// ============================================================================

/** 直接读写磁盘上的一块。元数据（超级块、FAT、目录）用这两个：它们在内存里本来就有完整的副本 */
static bool disk_read(uint32_t block, void *buf) {
    return blk_read(fs_start + (uint64_t)block * SECTORS_PER_BLOCK, buf, SECTORS_PER_BLOCK) == 0;
}

static bool disk_write(uint32_t block, const void *buf) {
    return blk_write(fs_start + (uint64_t)block * SECTORS_PER_BLOCK, buf, SECTORS_PER_BLOCK) == 0;
}

// 数据块的缓存：最近用过的几块留在内存里，再读就不用去找块设备驱动了（每次去都是一次
// IPC 加一次磁盘请求）。写的时候同时写磁盘和缓存（write-through），磁盘上的内容从不落后，
// 所以仍然不需要 sync，断电也不会比没有缓存时多丢东西。
#define CACHE_BLOCKS 16

static struct {
    uint32_t block;                 // 0 表示空（块 0 是超级块，不会进缓存）
    uint32_t used;                  // 上次用到的时刻（cache_clock 的值），用来挑最久没用的
    char data[BLOCK_SIZE];
} cache[CACHE_BLOCKS];
static uint32_t cache_clock;

/** block 在缓存里的位置；不在的话返回一个可以拿来用的位置（空的，或者最久没用的），*hit 说明是哪种 */
static int cache_slot(uint32_t block, bool *hit) {
    int victim = 0;
    for (int i = 0; i < CACHE_BLOCKS; i++) {
        if (cache[i].block == block) {
            *hit = true;
            return i;
        }
        if (cache[i].block == 0 || (cache[victim].block != 0 && cache[i].used < cache[victim].used)) {
            victim = i;
        }
    }
    *hit = false;
    return victim;
}

static bool block_read(uint32_t block, void *buf) {
    bool hit;
    int i = cache_slot(block, &hit);
    if (!hit) {
        cache[i].block = 0;         // 读失败的话这个位置里是半截内容：先作废
        if (!disk_read(block, cache[i].data)) {
            return false;
        }
        cache[i].block = block;
    }
    cache[i].used = ++cache_clock;
    memcpy(buf, cache[i].data, BLOCK_SIZE);
    return true;
}

static bool block_write(uint32_t block, const void *buf) {
    bool hit;
    int i = cache_slot(block, &hit);
    if (!disk_write(block, buf)) {
        if (hit) {
            cache[i].block = 0;     // 不知道磁盘上现在是什么：别再相信缓存里的
        }
        return false;
    }
    cache[i].block = block;
    cache[i].used = ++cache_clock;
    memcpy(cache[i].data, buf, BLOCK_SIZE);
    return true;
}

/** 把 FAT 里包含第 block 项的那一块写回磁盘 */
static bool fat_flush(uint32_t block) {
    uint32_t index = block / (BLOCK_SIZE / sizeof(uint32_t));
    return disk_write(sb.fat_start + index, (char *)fat + (size_t)index * BLOCK_SIZE);
}

/** 把目录里包含第 file 项的那一块写回磁盘 */
static bool dir_flush(int file) {
    uint32_t index = (uint32_t)file / ENTRIES_PER_BLOCK;
    return disk_write(sb.dir_start + index, (char *)dir + (size_t)index * BLOCK_SIZE);
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

static int diskfs_create(const char *name, bool directory) {
    for (int i = 0; i < (int)MAX_FILES; i++) {
        if (dir[i].name[0] == '\0') {
            memset(&dir[i], 0, sizeof(dir[i]));
            strcpy(dir[i].name, name);
            dir[i].first_block = FAT_END;
            dir[i].flags = directory ? ENTRY_DIR : 0;
            return dir_flush(i) ? i : -1;
        }
    }
    return -1;
}

static bool diskfs_is_dir(int file) {
    return (dir[file].flags & ENTRY_DIR) != 0;
}

static const char *diskfs_path(int file) {
    return dir[file].name;
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

static int diskfs_rename(int file, const char *path) {
    memset(dir[file].name, 0, sizeof(dir[file].name));
    strcpy(dir[file].name, path);
    return dir_flush(file) ? 0 : -1;
}

static int diskfs_entry(int index) {
    for (int i = 0; i < (int)MAX_FILES; i++) {
        if (dir[i].name[0] != '\0' && index-- == 0) {
            return i;
        }
    }
    return -1;
}

static const struct fs_backend diskfs_backend = {
    diskfs_find, diskfs_create, diskfs_is_dir, diskfs_path, diskfs_size, diskfs_read, diskfs_write,
    diskfs_truncate, diskfs_remove, diskfs_rename, diskfs_entry,
};

// ============================================================================
// 挂载
// ============================================================================

/** 从第 start 个扇区开始、最多 sectors 个扇区的地方是不是我们的文件系统；是的话 sb 里是它的超级块 */
static bool probe(uint64_t start, uint64_t sectors) {
    fs_start = start;
    if (sectors < SECTORS_PER_BLOCK || !disk_read(0, block_buf)) {
        return false;
    }
    memcpy(&sb, block_buf, sizeof(sb));
    return memcmp(sb.magic, DISKFS_MAGIC, DISKFS_MAGIC_SIZE) == 0 && sb.version == DISKFS_VERSION &&
           sb.total_blocks >= 16 && sb.total_blocks <= DISKFS_MAX_BLOCKS &&
           sb.total_blocks <= sectors / SECTORS_PER_BLOCK &&
           sb.fat_start == 1 && sb.fat_blocks == DISKFS_FAT_BLOCKS(sb.total_blocks) &&
           sb.dir_start == sb.fat_start + sb.fat_blocks && sb.dir_blocks == DISKFS_DIR_BLOCKS &&
           sb.data_start == sb.dir_start + sb.dir_blocks && sb.data_start < sb.total_blocks;
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/**
 * 在磁盘上找我们的文件系统：它占着整块盘，或者在分区表的某个分区里（系统映像写进硬盘后
 * 是这样：前面是引导程序和内核）。认的是分区开头的内容，不是分区类型。
 */
static bool find_fs(void) {
    uint64_t sectors = blk_capacity();
    if (probe(0, sectors)) {
        return true;
    }
    // 不是：第 0 个扇区（刚读进 block_buf）也许是分区表
    static uint8_t mbr[BLK_SECTOR_SIZE];
    memcpy(mbr, block_buf, sizeof(mbr));
    if (sectors == 0 || mbr[MBR_SIGNATURE_OFFSET] != 0x55 || mbr[MBR_SIGNATURE_OFFSET + 1] != 0xAA) {
        return false;
    }
    for (int i = 0; i < MBR_ENTRIES; i++) {
        const uint8_t *entry = mbr + MBR_TABLE_OFFSET + i * MBR_ENTRY_SIZE;
        uint64_t start = le32(entry + MBR_ENTRY_START);
        uint64_t count = le32(entry + MBR_ENTRY_SECTORS);
        if (entry[MBR_ENTRY_TYPE] != 0 && start != 0 && start < sectors && count <= sectors - start &&
            probe(start, count)) {
            return true;
        }
    }
    return false;
}

static bool mount(void) {
    if (!find_fs()) {
        return false;
    }
    size_t fat_bytes = (size_t)sb.fat_blocks * BLOCK_SIZE;
    fat = (uint32_t *)mmap(NULL, fat_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (fat == MAP_FAILED) {
        return false;
    }
    for (uint32_t i = 0; i < sb.fat_blocks; i++) {
        if (!disk_read(sb.fat_start + i, (char *)fat + (size_t)i * BLOCK_SIZE)) {
            return false;
        }
    }
    for (uint32_t i = 0; i < sb.dir_blocks; i++) {
        if (!disk_read(sb.dir_start + i, (char *)dir + (size_t)i * BLOCK_SIZE)) {
            return false;
        }
    }
    return true;
}

int main() {
    // 块设备驱动与我们同时启动：等它登记，或者等到它退出了（没有磁盘）
    if (name_settle(BLK_SERVICE_NAME) <= 0) {
        printf("diskfs: no disk\n");
        return 1;
    }
    if (!mount()) {
        printf("diskfs: no CastorOS file system on the disk, leaving it untouched\n");
        return 1;
    }

    int count = 0;
    for (int i = 0; i < (int)MAX_FILES; i++) {
        count += dir[i].name[0] != '\0';
    }
    printf("diskfs: ready (pid %d), %u blocks at sector %u, %d files\n", getpid(), sb.total_blocks,
           (uint32_t)fs_start, count);

    fs_serve(FS_DISK_SERVICE_NAME, &diskfs_backend);
    printf("diskfs: cannot register name\n");
    return 1;
}
