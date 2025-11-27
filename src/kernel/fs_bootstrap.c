#include <kernel/fs_bootstrap.h>

#include <drivers/ata.h>

#include <fs/vfs.h>
#include <fs/ramfs.h>
#include <fs/devfs.h>
#include <fs/procfs.h>
#include <fs/shmfs.h>
#include <fs/blockdev.h>
#include <fs/partition.h>
#include <fs/fat32.h>

#include <lib/klog.h>
#include <lib/string.h>

#include <mm/heap.h>

/**
 * 尝试在指定的 ATA 设备上初始化 FAT32 文件系统
 * @param ata_dev ATA 块设备
 * @param dev_name 设备名称（用于日志）
 * @return 成功返回根节点，失败返回 NULL
 */
static fs_node_t *try_init_fat32_on_device(blockdev_t *ata_dev, const char *dev_name) {
    if (!ata_dev) {
        return NULL;
    }

    LOG_INFO_MSG("fs: ATA device '%s' detected, probing partitions\n", dev_name);

    partition_t partitions[MAX_PARTITIONS];
    uint32_t partition_count = 0;

    // 首先尝试从分区中查找 FAT32
    if (partition_parse(ata_dev, partitions, &partition_count) == 0 && partition_count > 0) {
        for (uint32_t i = 0; i < partition_count; i++) {
            partition_t *part = &partitions[i];
            blockdev_t *part_dev = partition_create_blockdev(part);
            if (!part_dev) {
                continue;
            }

            if (fat32_probe(part_dev)) {
                LOG_INFO_MSG("fs: FAT32 partition detected on %s (index %u)\n", 
                             dev_name, (unsigned int)part->index);
                fs_node_t *fat32_root = fat32_init(part_dev);
                if (fat32_root) {
                    LOG_INFO_MSG("fs: FAT32 initialized successfully as root filesystem on %s\n", dev_name);
                    // 注意：不销毁 part_dev，因为 FAT32 文件系统需要持续使用它
                    // fat32_init 内部已经调用了 blockdev_retain 来保留引用
                    return fat32_root;
                } else {
                    LOG_WARN_MSG("fs: Failed to initialize FAT32 on %s partition %u\n", 
                                 dev_name, (unsigned int)part->index);
                }
            }

            partition_destroy_blockdev(part_dev);
        }
    } else {
        // 如果没有分区，尝试将整个磁盘作为 FAT32
        LOG_WARN_MSG("fs: No partitions found on %s, attempting whole-disk FAT32\n", dev_name);
        if (fat32_probe(ata_dev)) {
            fs_node_t *fat32_root = fat32_init(ata_dev);
            if (fat32_root) {
                LOG_INFO_MSG("fs: FAT32 initialized successfully on whole disk %s\n", dev_name);
                return fat32_root;
            } else {
                LOG_WARN_MSG("fs: Failed to initialize FAT32 on whole disk %s\n", dev_name);
            }
        }
    }

    return NULL;
}

void fs_init(void) {
    vfs_init();

    fs_node_t *root = NULL;
    
    // 第一步：尝试初始化 FAT32 作为根文件系统
    // 尝试所有 ATA 设备（ata0, ata1, ata2, ata3）
    const char *ata_devices[] = {"ata0", "ata1", "ata2", "ata3"};
    
    for (uint32_t dev_idx = 0; dev_idx < 4 && !root; dev_idx++) {
        blockdev_t *ata_dev = blockdev_get_by_name(ata_devices[dev_idx]);
        if (!ata_dev) {
            continue;
        }

        root = try_init_fat32_on_device(ata_dev, ata_devices[dev_idx]);
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

    // 第六步：初始化 shmfs（共享内存文件系统）
    fs_node_t *shmfs_root = shmfs_init();
    if (!shmfs_root) {
        LOG_ERROR_MSG("Failed to initialize shmfs\n");
        return;
    }

    // 创建 /shm 目录并挂载 shmfs
    if (vfs_mkdir("/shm", FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC) != 0) {
        LOG_WARN_MSG("Failed to create /shm directory (may already exist)\n");
    } else {
        LOG_INFO_MSG("/shm directory created\n");
    }

    // 尝试挂载 shmfs
    if (vfs_mount("/shm", shmfs_root) != 0) {
        LOG_ERROR_MSG("Failed to mount shmfs to /shm\n");
        // 不返回错误，系统可以继续运行，只是没有 /shm
    } else {
        LOG_INFO_MSG("shmfs mounted at /shm\n");
    }
}
