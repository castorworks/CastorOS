// ============================================================================
// devfs.c - 设备文件系统实现
// ============================================================================

#include <fs/devfs.h>
#include <fs/vfs.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <mm/heap.h>
#include <drivers/serial.h>
#include <kernel/io.h>

/* 设备节点数量 */
#define DEVFS_DEVICE_COUNT 4

/* 设备节点 */
static fs_node_t devfs_devices[DEVFS_DEVICE_COUNT];
static fs_node_t *devfs_root = NULL;

/* 前向声明 */
static uint32_t devnull_read(fs_node_t *node, uint32_t offset, 
                             uint32_t size, uint8_t *buffer);
static uint32_t devnull_write(fs_node_t *node, uint32_t offset,
                              uint32_t size, uint8_t *buffer);
static uint32_t devzero_read(fs_node_t *node, uint32_t offset,
                             uint32_t size, uint8_t *buffer);
static uint32_t devzero_write(fs_node_t *node, uint32_t offset,
                              uint32_t size, uint8_t *buffer);
static uint32_t devserial_read(fs_node_t *node, uint32_t offset,
                               uint32_t size, uint8_t *buffer);
static uint32_t devserial_write(fs_node_t *node, uint32_t offset,
                                uint32_t size, uint8_t *buffer);
static uint32_t devconsole_read(fs_node_t *node, uint32_t offset,
                                uint32_t size, uint8_t *buffer);
static uint32_t devconsole_write(fs_node_t *node, uint32_t offset,
                                 uint32_t size, uint8_t *buffer);
static struct dirent *devfs_readdir(fs_node_t *node, uint32_t index);
static fs_node_t *devfs_finddir(fs_node_t *node, const char *name);

/**
 * /dev/null - 空设备
 * 读取总是返回 0，写入总是成功但丢弃数据
 */
static uint32_t devnull_read(fs_node_t *node, uint32_t offset, 
                             uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0;  // 总是返回 0 字节
}

static uint32_t devnull_write(fs_node_t *node, uint32_t offset,
                              uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)buffer;
    return size;  // 假装写入成功
}

/**
 * /dev/zero - 零设备
 * 读取总是返回零字节，写入总是成功
 */
static uint32_t devzero_read(fs_node_t *node, uint32_t offset,
                             uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    memset(buffer, 0, size);
    return size;
}

static uint32_t devzero_write(fs_node_t *node, uint32_t offset,
                              uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)buffer;
    return size;  // 假装写入成功
}

/**
 * /dev/serial - 串口设备
 * 提供串口读写功能
 */
#define COM1 0x3F8

static uint32_t devserial_read(fs_node_t *node, uint32_t offset,
                               uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    
    uint32_t bytes_read = 0;
    
    // 读取串口数据（非阻塞）
    for (uint32_t i = 0; i < size; i++) {
        // 检查是否有数据可读（LSR 寄存器的位 0）
        if (inb(COM1 + 5) & 0x01) {
            buffer[i] = inb(COM1);
            bytes_read++;
        } else {
            // 没有更多数据
            break;
        }
    }
    
    return bytes_read;
}

static uint32_t devserial_write(fs_node_t *node, uint32_t offset,
                                uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    
    // 将数据写入串口
    for (uint32_t i = 0; i < size; i++) {
        serial_putchar(buffer[i]);
    }
    
    return size;
}

/**
 * /dev/console - 控制台设备
 * 提供标准输入输出功能
 */
static uint32_t devconsole_read(fs_node_t *node, uint32_t offset,
                                uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    
    // 从键盘读取（非阻塞）
    extern bool keyboard_try_getchar(char *c);
    
    uint32_t bytes_read = 0;
    for (uint32_t i = 0; i < size; i++) {
        char c;
        if (keyboard_try_getchar(&c)) {
            buffer[i] = c;
            bytes_read++;
        } else {
            break;
        }
    }
    
    return bytes_read;
}

static uint32_t devconsole_write(fs_node_t *node, uint32_t offset,
                                 uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    
    // 写入到 VGA 控制台
    extern void vga_putchar(char c);
    
    for (uint32_t i = 0; i < size; i++) {
        vga_putchar(buffer[i]);
    }
    
    return size;
}

/**
 * 读取 /dev 目录
 */
static struct dirent *devfs_readdir(fs_node_t *node, uint32_t index) {
    (void)node;
    
    if (index >= DEVFS_DEVICE_COUNT) {
        return NULL;
    }
    
    static struct dirent dirent;
    strcpy(dirent.d_name, devfs_devices[index].name);
    dirent.d_ino = devfs_devices[index].inode;
    dirent.d_reclen = sizeof(struct dirent);
    dirent.d_off = index + 1;  // 下一个索引
    dirent.d_type = DT_CHR;  // 字符设备
    
    return &dirent;
}

/**
 * 在 /dev 目录中查找设备
 */
static fs_node_t *devfs_finddir(fs_node_t *node, const char *name) {
    (void)node;
    
    for (uint32_t i = 0; i < DEVFS_DEVICE_COUNT; i++) {
        if (strcmp(devfs_devices[i].name, name) == 0) {
            return &devfs_devices[i];
        }
    }
    
    return NULL;
}

/**
 * 初始化 devfs
 */
fs_node_t *devfs_init(void) {
    LOG_INFO_MSG("devfs: Initializing device filesystem...\n");
    
    memset(devfs_devices, 0, sizeof(devfs_devices));
    
    // 设备 0: /dev/null
    strcpy(devfs_devices[0].name, "null");
    devfs_devices[0].inode = 0;
    devfs_devices[0].type = FS_CHARDEVICE;
    devfs_devices[0].size = 0;
    devfs_devices[0].permissions = FS_PERM_READ | FS_PERM_WRITE;
    devfs_devices[0].uid = 0;
    devfs_devices[0].gid = 0;
    devfs_devices[0].flags = 0;
    devfs_devices[0].read = devnull_read;
    devfs_devices[0].write = devnull_write;
    devfs_devices[0].open = NULL;
    devfs_devices[0].close = NULL;
    devfs_devices[0].readdir = NULL;
    devfs_devices[0].finddir = NULL;
    devfs_devices[0].create = NULL;
    devfs_devices[0].mkdir = NULL;
    devfs_devices[0].unlink = NULL;
    devfs_devices[0].ptr = NULL;
    
    // 设备 1: /dev/zero
    strcpy(devfs_devices[1].name, "zero");
    devfs_devices[1].inode = 1;
    devfs_devices[1].type = FS_CHARDEVICE;
    devfs_devices[1].size = 0;
    devfs_devices[1].permissions = FS_PERM_READ | FS_PERM_WRITE;
    devfs_devices[1].uid = 0;
    devfs_devices[1].gid = 0;
    devfs_devices[1].flags = 0;
    devfs_devices[1].read = devzero_read;
    devfs_devices[1].write = devzero_write;
    devfs_devices[1].open = NULL;
    devfs_devices[1].close = NULL;
    devfs_devices[1].readdir = NULL;
    devfs_devices[1].finddir = NULL;
    devfs_devices[1].create = NULL;
    devfs_devices[1].mkdir = NULL;
    devfs_devices[1].unlink = NULL;
    devfs_devices[1].ptr = NULL;
    
    // 设备 2: /dev/serial
    strcpy(devfs_devices[2].name, "serial");
    devfs_devices[2].inode = 2;
    devfs_devices[2].type = FS_CHARDEVICE;
    devfs_devices[2].size = 0;
    devfs_devices[2].permissions = FS_PERM_READ | FS_PERM_WRITE;
    devfs_devices[2].uid = 0;
    devfs_devices[2].gid = 0;
    devfs_devices[2].flags = 0;
    devfs_devices[2].read = devserial_read;
    devfs_devices[2].write = devserial_write;
    devfs_devices[2].open = NULL;
    devfs_devices[2].close = NULL;
    devfs_devices[2].readdir = NULL;
    devfs_devices[2].finddir = NULL;
    devfs_devices[2].create = NULL;
    devfs_devices[2].mkdir = NULL;
    devfs_devices[2].unlink = NULL;
    devfs_devices[2].ptr = NULL;
    
    // 设备 3: /dev/console
    strcpy(devfs_devices[3].name, "console");
    devfs_devices[3].inode = 3;
    devfs_devices[3].type = FS_CHARDEVICE;
    devfs_devices[3].size = 0;
    devfs_devices[3].permissions = FS_PERM_READ | FS_PERM_WRITE;
    devfs_devices[3].uid = 0;
    devfs_devices[3].gid = 0;
    devfs_devices[3].flags = 0;
    devfs_devices[3].read = devconsole_read;
    devfs_devices[3].write = devconsole_write;
    devfs_devices[3].open = NULL;
    devfs_devices[3].close = NULL;
    devfs_devices[3].readdir = NULL;
    devfs_devices[3].finddir = NULL;
    devfs_devices[3].create = NULL;
    devfs_devices[3].mkdir = NULL;
    devfs_devices[3].unlink = NULL;
    devfs_devices[3].ptr = NULL;
    
    // 创建 /dev 根目录节点
    devfs_root = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!devfs_root) {
        LOG_ERROR_MSG("devfs: Failed to allocate root node\n");
        return NULL;
    }
    
    memset(devfs_root, 0, sizeof(fs_node_t));
    strcpy(devfs_root->name, "dev");
    devfs_root->inode = 0;
    devfs_root->type = FS_DIRECTORY;
    devfs_root->size = 0;
    devfs_root->permissions = FS_PERM_READ | FS_PERM_EXEC;
    devfs_root->uid = 0;
    devfs_root->gid = 0;
    devfs_root->flags = 0;
    devfs_root->read = NULL;
    devfs_root->write = NULL;
    devfs_root->open = NULL;
    devfs_root->close = NULL;
    devfs_root->readdir = devfs_readdir;
    devfs_root->finddir = devfs_finddir;
    devfs_root->create = NULL;  // 不支持创建设备
    devfs_root->mkdir = NULL;   // 不支持创建目录
    devfs_root->unlink = NULL;  // 不支持删除设备
    devfs_root->ptr = NULL;
    
    LOG_INFO_MSG("devfs: Initialized with %u devices\n", DEVFS_DEVICE_COUNT);
    LOG_DEBUG_MSG("  - /dev/null\n");
    LOG_DEBUG_MSG("  - /dev/zero\n");
    LOG_DEBUG_MSG("  - /dev/serial\n");
    LOG_DEBUG_MSG("  - /dev/console\n");
    
    return devfs_root;
}
