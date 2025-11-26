// ============================================================================
// blockdev.c - 块设备抽象层实现
// ============================================================================

#include <fs/blockdev.h>
#include <lib/klog.h>
#include <lib/string.h>
#include <kernel/sync/mutex.h>
#include <kernel/sync/spinlock.h>

static blockdev_t *blockdev_registry[BLOCKDEV_MAX_DEVICES];
static uint32_t blockdev_registry_count = 0;

// 保护注册表的互斥锁
static mutex_t blockdev_registry_mutex;
// 保护引用计数操作的自旋锁
static spinlock_t blockdev_refcount_lock;
// 标记是否已初始化锁
static bool blockdev_locks_initialized = false;

// 确保锁已初始化
static void blockdev_ensure_locks_init(void) {
    if (!blockdev_locks_initialized) {
        mutex_init(&blockdev_registry_mutex);
        spinlock_init(&blockdev_refcount_lock);
        blockdev_locks_initialized = true;
    }
}

static int blockdev_find_index(blockdev_t *dev) {
    for (uint32_t i = 0; i < blockdev_registry_count; i++) {
        if (blockdev_registry[i] == dev) {
            return (int)i;
        }
    }
    return -1;
}

static blockdev_t *blockdev_find_by_name_internal(const char *name) {
    for (uint32_t i = 0; i < blockdev_registry_count; i++) {
        if (strcmp(blockdev_registry[i]->name, name) == 0) {
            return blockdev_registry[i];
        }
    }
    return NULL;
}

int blockdev_read(blockdev_t *dev, uint32_t sector, uint32_t count, uint8_t *buffer) {
    if (!dev || !dev->read || !buffer) {
        return -1;
    }
    
    if (sector + count > dev->total_sectors) {
        LOG_ERROR_MSG("blockdev: Read beyond device size (sector %u, count %u, total %u)\n",
                      sector, count, dev->total_sectors);
        return -1;
    }
    
    return dev->read(dev->private_data, sector, count, buffer);
}

int blockdev_write(blockdev_t *dev, uint32_t sector, uint32_t count, const uint8_t *buffer) {
    if (!dev || !dev->write || !buffer) {
        return -1;
    }
    
    if (sector + count > dev->total_sectors) {
        LOG_ERROR_MSG("blockdev: Write beyond device size (sector %u, count %u, total %u)\n",
                      sector, count, dev->total_sectors);
        return -1;
    }
    
    return dev->write(dev->private_data, sector, count, buffer);
}

uint32_t blockdev_get_size(blockdev_t *dev) {
    if (!dev) {
        return 0;
    }
    
    if (dev->get_size) {
        return dev->get_size(dev->private_data);
    }
    
    return dev->total_sectors;
}

uint32_t blockdev_get_block_size(blockdev_t *dev) {
    if (!dev) {
        return 0;
    }
    
    if (dev->get_block_size) {
        return dev->get_block_size(dev->private_data);
    }
    
    return dev->block_size;
}

int blockdev_register(blockdev_t *dev) {
    if (!dev) {
        return -1;
    }
    
    blockdev_ensure_locks_init();
    mutex_lock(&blockdev_registry_mutex);

    if (dev->registered) {
        LOG_WARN_MSG("blockdev: Device '%s' already registered\n", dev->name);
        mutex_unlock(&blockdev_registry_mutex);
        return -1;
    }

    if (blockdev_registry_count >= BLOCKDEV_MAX_DEVICES) {
        LOG_ERROR_MSG("blockdev: Registry is full, cannot register '%s'\n", dev->name);
        mutex_unlock(&blockdev_registry_mutex);
        return -1;
    }

    if (dev->name[0] == '\0') {
        LOG_ERROR_MSG("blockdev: Device name is empty, cannot register\n");
        mutex_unlock(&blockdev_registry_mutex);
        return -1;
    }

    if (blockdev_find_by_name_internal(dev->name) != NULL) {
        LOG_ERROR_MSG("blockdev: Device name '%s' already exists\n", dev->name);
        mutex_unlock(&blockdev_registry_mutex);
        return -1;
    }

    dev->ref_count = 1;
    dev->registered = true;
    blockdev_registry[blockdev_registry_count++] = dev;

    LOG_INFO_MSG("blockdev: Registered device '%s'\n", dev->name);
    mutex_unlock(&blockdev_registry_mutex);
    return 0;
}

void blockdev_unregister(blockdev_t *dev) {
    if (!dev) {
        return;
    }
    
    blockdev_ensure_locks_init();
    mutex_lock(&blockdev_registry_mutex);
    
    if (!dev->registered) {
        mutex_unlock(&blockdev_registry_mutex);
        return;
    }

    int index = blockdev_find_index(dev);
    if (index < 0) {
        LOG_WARN_MSG("blockdev: Device '%s' not found in registry\n", dev->name);
        dev->registered = false;
        mutex_unlock(&blockdev_registry_mutex);
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
    mutex_unlock(&blockdev_registry_mutex);
    blockdev_release(dev);

    LOG_INFO_MSG("blockdev: Unregistered device '%s'\n", dev->name);
}

blockdev_t *blockdev_get_by_name(const char *name) {
    if (!name) {
        return NULL;
    }
    
    blockdev_ensure_locks_init();
    mutex_lock(&blockdev_registry_mutex);

    blockdev_t *dev = blockdev_find_by_name_internal(name);
    if (!dev) {
        mutex_unlock(&blockdev_registry_mutex);
        return NULL;
    }

    // 在持有注册表锁的情况下增加引用计数，确保设备不会被删除
    blockdev_t *result = blockdev_retain(dev);
    mutex_unlock(&blockdev_registry_mutex);
    return result;
}

blockdev_t *blockdev_retain(blockdev_t *dev) {
    if (!dev) {
        return NULL;
    }
    
    blockdev_ensure_locks_init();
    
    bool irq_state;
    spinlock_lock_irqsave(&blockdev_refcount_lock, &irq_state);
    dev->ref_count++;
    spinlock_unlock_irqrestore(&blockdev_refcount_lock, irq_state);
    
    return dev;
}

void blockdev_release(blockdev_t *dev) {
    if (!dev) {
        return;
    }
    
    blockdev_ensure_locks_init();
    
    bool irq_state;
    spinlock_lock_irqsave(&blockdev_refcount_lock, &irq_state);
    
    if (dev->ref_count == 0) {
        spinlock_unlock_irqrestore(&blockdev_refcount_lock, irq_state);
        LOG_WARN_MSG("blockdev: Device '%s' reference underflow\n", dev->name);
        return;
    }

    dev->ref_count--;
    spinlock_unlock_irqrestore(&blockdev_refcount_lock, irq_state);
}

