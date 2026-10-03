// ============================================================================
// blockdev.c - 块设备抽象层实现
// ============================================================================

#include <fs/blockdev.h>
#include <lib/klog.h>
#include <lib/string.h>
#include <kernel/sync/mutex.h>
#include <kernel/sync/spinlock.h>

static fs::Blockdev *blockdev_registry[BLOCKDEV_MAX_DEVICES];
static uint32_t blockdev_registry_count = 0;

// 保护注册表的互斥锁
static sync::Mutex blockdev_registry_mutex;
// 保护引用计数操作的自旋锁
static sync::Spinlock blockdev_refcount_lock;
// 标记是否已初始化锁
static bool blockdev_locks_initialized = false;

// 确保锁已初始化
static void blockdev_ensure_locks_init(void) {
    if (!blockdev_locks_initialized) {
        blockdev_registry_mutex.init();
        blockdev_refcount_lock.init();
        blockdev_locks_initialized = true;
    }
}

static int blockdev_find_index(fs::Blockdev *dev) {
    for (uint32_t i = 0; i < blockdev_registry_count; i++) {
        if (blockdev_registry[i] == dev) {
            return (int)i;
        }
    }
    return -1;
}

static fs::Blockdev *blockdev_find_by_name_internal(const char *name) {
    for (uint32_t i = 0; i < blockdev_registry_count; i++) {
        if (strcmp(blockdev_registry[i]->name, name) == 0) {
            return blockdev_registry[i];
        }
    }
    return NULL;
}

uint32_t fs::BlockdevOps::get_size(const fs::Blockdev *bdev) const {
    return bdev->total_sectors;
}

uint32_t fs::BlockdevOps::get_block_size(const fs::Blockdev *bdev) const {
    return bdev->block_size;
}

int fs::Blockdev::read(fs::Blockdev *dev, uint32_t sector, uint32_t count, uint8_t *buffer) {
    if (!dev || !dev->ops || !buffer) {
        return -1;
    }
    
    if (sector + count > dev->total_sectors) {
        LOG_ERROR_MSG("blockdev: Read beyond device size (sector %u, count %u, total %u)\n",
                      sector, count, dev->total_sectors);
        return -1;
    }
    
    return dev->ops->read(dev->private_data, sector, count, buffer);
}

int fs::Blockdev::write(fs::Blockdev *dev, uint32_t sector, uint32_t count, const uint8_t *buffer) {
    if (!dev || !dev->ops || !buffer) {
        return -1;
    }
    
    if (sector + count > dev->total_sectors) {
        LOG_ERROR_MSG("blockdev: Write beyond device size (sector %u, count %u, total %u)\n",
                      sector, count, dev->total_sectors);
        return -1;
    }
    
    return dev->ops->write(dev->private_data, sector, count, buffer);
}

uint32_t fs::Blockdev::get_size(fs::Blockdev *dev) {
    if (!dev) {
        return 0;
    }
    
    if (dev->ops) {
        return dev->ops->get_size(dev);
    }
    
    return dev->total_sectors;
}

uint32_t fs::Blockdev::get_block_size(fs::Blockdev *dev) {
    if (!dev) {
        return 0;
    }
    
    if (dev->ops) {
        return dev->ops->get_block_size(dev);
    }
    
    return dev->block_size;
}

int fs::Blockdev::register_device(fs::Blockdev *dev) {
    if (!dev) {
        return -1;
    }
    
    blockdev_ensure_locks_init();
    sync::MutexGuard guard(blockdev_registry_mutex);

    if (dev->registered) {
        LOG_WARN_MSG("blockdev: Device '%s' already registered\n", dev->name);
        return -1;
    }

    if (blockdev_registry_count >= BLOCKDEV_MAX_DEVICES) {
        LOG_ERROR_MSG("blockdev: Registry is full, cannot register '%s'\n", dev->name);
        return -1;
    }

    if (dev->name[0] == '\0') {
        LOG_ERROR_MSG("blockdev: Device name is empty, cannot register\n");
        return -1;
    }

    if (blockdev_find_by_name_internal(dev->name) != NULL) {
        LOG_ERROR_MSG("blockdev: Device name '%s' already exists\n", dev->name);
        return -1;
    }

    dev->ref_count = 1;
    dev->registered = true;
    blockdev_registry[blockdev_registry_count++] = dev;

    LOG_INFO_MSG("blockdev: Registered device '%s'\n", dev->name);
    return 0;
}

void fs::Blockdev::unregister_device(fs::Blockdev *dev) {
    if (!dev) {
        return;
    }
    
    blockdev_ensure_locks_init();
    blockdev_registry_mutex.lock();
    
    if (!dev->registered) {
        blockdev_registry_mutex.unlock();
        return;
    }

    int index = blockdev_find_index(dev);
    if (index < 0) {
        LOG_WARN_MSG("blockdev: Device '%s' not found in registry\n", dev->name);
        dev->registered = false;
        blockdev_registry_mutex.unlock();
        return;
    }

    if (dev->ref_count > 1) {
        LOG_WARN_MSG("blockdev: Unregistering device '%s' with %u outstanding references\n",
                     dev->name, dev->ref_count - 1);
    }

    for (uint32_t i = (uint32_t)index; i + 1 < blockdev_registry_count; i++) {
        blockdev_registry[i] = blockdev_registry[i + 1];
    }
    blockdev_registry_count--;
    blockdev_registry[blockdev_registry_count] = NULL;

    dev->registered = false;
    
    // 在解锁后释放引用，避免在持锁时调用可能重入的操作
    blockdev_registry_mutex.unlock();
    fs::Blockdev::release(dev);

    LOG_INFO_MSG("blockdev: Unregistered device '%s'\n", dev->name);
}

fs::Blockdev *fs::Blockdev::get_by_name(const char *name) {
    if (!name) {
        return NULL;
    }
    
    blockdev_ensure_locks_init();
    sync::MutexGuard guard(blockdev_registry_mutex);

    fs::Blockdev *dev = blockdev_find_by_name_internal(name);
    if (!dev) {
        return NULL;
    }

    // 在持有注册表锁的情况下增加引用计数，确保设备不会被删除
    fs::Blockdev *result = fs::Blockdev::retain(dev);
    return result;
}

fs::Blockdev *fs::Blockdev::retain(fs::Blockdev *dev) {
    if (!dev) {
        return NULL;
    }
    
    blockdev_ensure_locks_init();
    
    sync::SpinlockIrqGuard guard(blockdev_refcount_lock);
    dev->ref_count++;
    
    return dev;
}

void fs::Blockdev::release(fs::Blockdev *dev) {
    if (!dev) {
        return;
    }
    
    blockdev_ensure_locks_init();
    
    bool irq_state;
    blockdev_refcount_lock.lock_irqsave(irq_state);
    
    if (dev->ref_count == 0) {
        blockdev_refcount_lock.unlock_irqrestore(irq_state);
        LOG_WARN_MSG("blockdev: Device '%s' reference underflow\n", dev->name);
        return;
    }

    dev->ref_count--;
    blockdev_refcount_lock.unlock_irqrestore(irq_state);
}

