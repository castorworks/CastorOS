// ============================================================================
// blockdev.c - 块设备抽象层实现
// ============================================================================

#include <fs/blockdev.h>
#include <lib/klog.h>
#include <lib/string.h>

static blockdev_t *blockdev_registry[BLOCKDEV_MAX_DEVICES];
static uint32_t blockdev_registry_count = 0;

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

void blockdev_unregister(blockdev_t *dev) {
    if (!dev || !dev->registered) {
        return;
    }

    int index = blockdev_find_index(dev);
    if (index < 0) {
        LOG_WARN_MSG("blockdev: Device '%s' not found in registry\n", dev->name);
        dev->registered = false;
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
    blockdev_release(dev);

    LOG_INFO_MSG("blockdev: Unregistered device '%s'\n", dev->name);
}

blockdev_t *blockdev_get_by_name(const char *name) {
    if (!name) {
        return NULL;
    }

    blockdev_t *dev = blockdev_find_by_name_internal(name);
    if (!dev) {
        return NULL;
    }

    return blockdev_retain(dev);
}

blockdev_t *blockdev_retain(blockdev_t *dev) {
    if (!dev) {
        return NULL;
    }

    dev->ref_count++;
    return dev;
}

void blockdev_release(blockdev_t *dev) {
    if (!dev) {
        return;
    }

    if (dev->ref_count == 0) {
        LOG_WARN_MSG("blockdev: Device '%s' reference underflow\n", dev->name);
        return;
    }

    dev->ref_count--;
}

