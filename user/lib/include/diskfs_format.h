#ifndef _USERLAND_LIB_DISKFS_FORMAT_H_
#define _USERLAND_LIB_DISKFS_FORMAT_H_

// 磁盘文件系统在盘上的格式。两处用它：读写它的服务（user/diskfs），和构建时在宿主机上
// 做出文件系统映像的工具（tools/mkdiskfs.cpp）。所以这个头文件不包含别的头文件：
// uint32_t 由包含它的一方提供（用户库的 <types.h>，或者宿主机的 <stdint.h>）。
//
// 块大小 4096 字节：
//   块 0                 超级块
//   块 1..               FAT：每个块一个 32 位表项，记录文件的块链
//   之后 DISKFS_DIR_BLOCKS 块   目录表：定长表项，每一项是一个文件或一个目录，名字是从根写起
//                        的完整路径（"usr/share/doc"）。目录的规则不在格式里：这只是一张平的表
//   其余                 数据块
//
// 文件系统可以占整块盘（超级块在第 0 个扇区），也可以在 MBR 分区表的一个分区里
// （超级块在那个分区的开头）。

#define DISKFS_MAGIC            "CASTORFS"
#define DISKFS_MAGIC_SIZE       8
#define DISKFS_VERSION          2

#define DISKFS_BLOCK_SIZE       4096
#define DISKFS_SECTOR_SIZE      512
#define DISKFS_NAME_MAX         64          // 路径的最大长度，含结尾 NUL（等于 FS_NAME_MAX）

#define DISKFS_FAT_FREE         0u
#define DISKFS_FAT_END          0xFFFFFFFFu
#define DISKFS_FAT_RESERVED     0xFFFFFFFEu // 超级块、FAT、目录自己占的块

#define DISKFS_DIR_BLOCKS       8
#define DISKFS_MAX_BLOCKS       (256u * 1024)   // 最多管理 1GB

struct diskfs_superblock {
    char magic[DISKFS_MAGIC_SIZE];
    uint32_t version;
    uint32_t total_blocks;
    uint32_t fat_start;
    uint32_t fat_blocks;
    uint32_t dir_start;
    uint32_t dir_blocks;
    uint32_t data_start;
};

struct diskfs_dir_entry {
    char name[DISKFS_NAME_MAX];     // 完整的路径；name[0] == 0 表示空闲
    uint32_t size;
    uint32_t first_block;           // DISKFS_FAT_END 表示还没有数据块
    uint32_t flags;                 // DISKFS_ENTRY_DIR：这一项是目录（没有数据块）
    uint32_t reserved[13];          // 凑成 128 字节
};
#define DISKFS_ENTRY_DIR        0x1

#define DISKFS_ENTRIES_PER_BLOCK    (DISKFS_BLOCK_SIZE / sizeof(struct diskfs_dir_entry))
#define DISKFS_MAX_FILES            (DISKFS_DIR_BLOCKS * DISKFS_ENTRIES_PER_BLOCK)

/** total_blocks 个块的文件系统，FAT 占几块 */
#define DISKFS_FAT_BLOCKS(total_blocks) \
    (((total_blocks) * sizeof(uint32_t) + DISKFS_BLOCK_SIZE - 1) / DISKFS_BLOCK_SIZE)

// MBR 分区表：第 0 个扇区的最后两个字节是 0x55 0xAA，之前是 4 个 16 字节的表项
#define MBR_SIGNATURE_OFFSET    510
#define MBR_TABLE_OFFSET        446
#define MBR_ENTRIES             4
#define MBR_ENTRY_SIZE          16
#define MBR_ENTRY_TYPE          4       // 表项里的偏移：分区类型（0 = 空表项）
#define MBR_ENTRY_START         8       //   起始扇区，32 位小端
#define MBR_ENTRY_SECTORS       12      //   扇区数，32 位小端

#endif // _USERLAND_LIB_DISKFS_FORMAT_H_
