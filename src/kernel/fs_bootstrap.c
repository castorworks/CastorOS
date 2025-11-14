#include <kernel/fs_bootstrap.h>

#include <drivers/ata.h>

#include <fs/vfs.h>
#include <fs/ramfs.h>
#include <fs/devfs.h>
#include <fs/procfs.h>
#include <fs/blockdev.h>
#include <fs/partition.h>
#include <fs/fat32.h>

#include <lib/klog.h>
#include <lib/string.h>

#include <mm/heap.h>

void fs_init(void) {
    vfs_init();

    fs_node_t *root = NULL;
    fs_node_t *fat32_root = NULL;
    blockdev_t *ata_dev = blockdev_get_by_name("ata0");

    // 第一步：尝试初始化 FAT32 作为根文件系统
    if (ata_dev) {
        LOG_INFO_MSG("fs: ATA device 'ata0' detected, probing partitions\n");

        partition_t partitions[MAX_PARTITIONS];
        uint32_t partition_count = 0;

        if (partition_parse(ata_dev, partitions, &partition_count) == 0 && partition_count > 0) {
            for (uint32_t i = 0; i < partition_count; i++) {
                partition_t *part = &partitions[i];
                blockdev_t *part_dev = partition_create_blockdev(part);
                if (!part_dev) {
                    continue;
                }

                if (fat32_probe(part_dev)) {
                    LOG_INFO_MSG("fs: FAT32 partition detected (index %u)\n", (unsigned int)part->index);
                    fat32_root = fat32_init(part_dev);
                    if (fat32_root) {
                        root = fat32_root;
                        LOG_INFO_MSG("fs: FAT32 initialized successfully as root filesystem\n");
                        break;
                    } else {
                        LOG_WARN_MSG("fs: Failed to initialize FAT32 on partition %u\n", (unsigned int)part->index);
                    }
                }

                partition_destroy_blockdev(part_dev);
            }
        } else {
            LOG_WARN_MSG("fs: No partitions found on ata0, attempting whole-disk FAT32\n");
            if (fat32_probe(ata_dev)) {
                fat32_root = fat32_init(ata_dev);
                if (fat32_root) {
                    root = fat32_root;
                    LOG_INFO_MSG("fs: FAT32 initialized successfully on whole disk\n");
                } else {
                    LOG_WARN_MSG("fs: Failed to initialize FAT32 on whole disk\n");
                }
            }
        }

        blockdev_release(ata_dev);
    }

    // 第二步：如果 FAT32 不可用，回退到 RAMFS
    if (!root) {
        LOG_WARN_MSG("fs: No usable FAT32 filesystem found, falling back to RAMFS\n");
        root = ramfs_init();
        if (!root) {
            LOG_ERROR_MSG("Failed to initialize RAMFS\n");
            return;
        }
        LOG_INFO_MSG("RAMFS initialized as root filesystem\n");
    }

    vfs_set_root(root);

    // 第三步：初始化 devfs
    fs_node_t *devfs_root = devfs_init();
    if (!devfs_root) {
        LOG_ERROR_MSG("Failed to initialize devfs\n");
        return;
    }
    LOG_INFO_MSG("devfs initialized\n");

    // 第四步：创建虚拟文件系统挂载点并挂载 devfs
    // 注意：/dev 是虚拟的，不应该持久化到磁盘
    // 如果创建失败，继续尝试挂载（可能已经存在）
    if (vfs_mkdir("/dev", FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC) != 0) {
        LOG_WARN_MSG("Failed to create /dev directory (may already exist)\n");
    } else {
        LOG_INFO_MSG("/dev directory created\n");
    }

    // 尝试挂载 devfs
    if (vfs_mount("/dev", devfs_root) != 0) {
        LOG_ERROR_MSG("Failed to mount devfs to /dev\n");
        // 不返回错误，系统可以继续运行，只是没有 /dev
    } else {
        LOG_INFO_MSG("devfs mounted at /dev\n");
    }

    // 第五步：初始化 procfs
    fs_node_t *procfs_root = procfs_init();
    if (!procfs_root) {
        LOG_ERROR_MSG("Failed to initialize procfs\n");
        return;
    }
    LOG_INFO_MSG("procfs initialized\n");

    // 创建 /proc 目录并挂载 procfs
    if (vfs_mkdir("/proc", FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC) != 0) {
        LOG_WARN_MSG("Failed to create /proc directory (may already exist)\n");
    } else {
        LOG_INFO_MSG("/proc directory created\n");
    }

    // 尝试挂载 procfs
    if (vfs_mount("/proc", procfs_root) != 0) {
        LOG_ERROR_MSG("Failed to mount procfs to /proc\n");
        // 不返回错误，系统可以继续运行，只是没有 /proc
    } else {
        LOG_INFO_MSG("procfs mounted at /proc\n");
    }
}
