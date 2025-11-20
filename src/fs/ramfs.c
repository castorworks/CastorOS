// ============================================================================
// ramfs.c - 基于 RAM 的简单文件系统实现
// ============================================================================

#include <fs/ramfs.h>
#include <fs/vfs.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <mm/heap.h>
#include <kernel/sync/spinlock.h>

// ramfs 文件数据结构
typedef struct ramfs_file {
    uint8_t *data;        // 文件数据
    uint32_t size;        // 文件大小
    uint32_t capacity;    // 已分配容量
} ramfs_file_t;

// ramfs 目录项
typedef struct ramfs_dirent {
    char name[128];       // 文件名
    fs_node_t *node;      // 指向文件节点
    struct ramfs_dirent *next;  // 链表下一项
} ramfs_dirent_t;

// ramfs 目录数据结构
typedef struct ramfs_dir {
    ramfs_dirent_t *entries;  // 目录项链表
    uint32_t count;           // 目录项数量
} ramfs_dir_t;

// 全局 inode 计数器
static uint32_t next_inode = 1;

// inode 分配锁（保护 next_inode 的并发访问）
static spinlock_t inode_alloc_lock;

// ============================================================================
// 内部辅助函数
// ============================================================================

/**
 * 查找目录中的条目
 */
static ramfs_dirent_t *ramfs_find_entry(ramfs_dir_t *dir, const char *name) {
    ramfs_dirent_t *current = dir->entries;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

/**
 * 添加目录项到目录
 */
static int ramfs_add_entry(ramfs_dir_t *dir, const char *name, fs_node_t *node) {
    // 检查是否已存在
    if (ramfs_find_entry(dir, name)) {
        return -1;  // 文件已存在
    }
    
    // 创建新目录项
    ramfs_dirent_t *entry = (ramfs_dirent_t *)kmalloc(sizeof(ramfs_dirent_t));
    if (!entry) {
        return -1;
    }
    
    strncpy(entry->name, name, 127);
    entry->name[127] = '\0';
    entry->node = node;
    entry->next = dir->entries;
    dir->entries = entry;
    dir->count++;
    
    return 0;
}

/**
 * 从目录中移除条目
 */
static int ramfs_remove_entry(ramfs_dir_t *dir, const char *name) {
    ramfs_dirent_t **current = &dir->entries;
    
    while (*current) {
        if (strcmp((*current)->name, name) == 0) {
            ramfs_dirent_t *to_remove = *current;
            *current = (*current)->next;
            kfree(to_remove);
            dir->count--;
            return 0;
        }
        current = &(*current)->next;
    }
    
    return -1;  // 未找到
}

// ============================================================================
// VFS 操作函数实现
// ============================================================================

// 前向声明
static int ramfs_unlink(fs_node_t *node, const char *name);

/**
 * 读取文件
 */
static uint32_t ramfs_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (node->type != FS_FILE) {
        return 0;
    }
    
    ramfs_file_t *file = (ramfs_file_t *)node->impl;
    if (!file || !file->data) {
        return 0;
    }
    
    // 检查偏移量
    if (offset >= file->size) {
        return 0;
    }
    
    // 调整读取大小
    uint32_t to_read = size;
    if (offset + to_read > file->size) {
        to_read = file->size - offset;
    }
    
    // 复制数据
    memcpy(buffer, file->data + offset, to_read);
    return to_read;
}

/**
 * 写入文件
 */
static uint32_t ramfs_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (node->type != FS_FILE) {
        return 0;
    }
    
    ramfs_file_t *file = (ramfs_file_t *)node->impl;
    if (!file) {
        return 0;
    }
    
    // 计算需要的总大小
    uint32_t new_size = offset + size;
    
    // 如果需要扩容
    if (new_size > file->capacity) {
        // 计算新容量（向上取整到 4KB 的倍数）
        uint32_t new_capacity = (new_size + 4095) & ~4095;
        
        // 重新分配内存
        uint8_t *new_data = (uint8_t *)kmalloc(new_capacity);
        if (!new_data) {
            return 0;  // 内存不足
        }
        
        // 复制旧数据
        if (file->data && file->size > 0) {
            memcpy(new_data, file->data, file->size);
        }
        
        // 释放旧内存
        if (file->data) {
            kfree(file->data);
        }
        
        file->data = new_data;
        file->capacity = new_capacity;
    }
    
    // 写入数据
    memcpy(file->data + offset, buffer, size);
    
    // 更新文件大小
    if (new_size > file->size) {
        file->size = new_size;
        node->size = new_size;
    }
    
    return size;
}

/**
 * 打开文件
 */
static void ramfs_open(fs_node_t *node, uint32_t flags) {
    // ramfs 不需要特殊的打开操作
    (void)node;
    (void)flags;
}

/**
 * 关闭文件
 */
static void ramfs_close(fs_node_t *node) {
    // ramfs 不需要特殊的关闭操作
    (void)node;
}

/**
 * 读取目录项
 */
static struct dirent *ramfs_readdir(fs_node_t *node, uint32_t index) {
    if (node->type != FS_DIRECTORY) {
        return NULL;
    }
    
    ramfs_dir_t *dir = (ramfs_dir_t *)node->impl;
    if (!dir) {
        return NULL;
    }
    
    // 遍历到指定索引
    ramfs_dirent_t *current = dir->entries;
    uint32_t i = 0;
    
    while (current && i < index) {
        current = current->next;
        i++;
    }
    
    if (!current) {
        return NULL;  // 索引超出范围
    }
    
    // 创建返回的 dirent（静态变量，下次调用会覆盖）
    static struct dirent dent;
    
    // 填充标准字段
    strncpy(dent.d_name, current->name, 255);
    dent.d_name[255] = '\0';
    dent.d_ino = current->node->inode;
    dent.d_reclen = sizeof(struct dirent);
    dent.d_off = index + 1;  // 下一个索引
    
    // 根据节点类型设置 d_type
    switch (current->node->type) {
        case FS_FILE:
            dent.d_type = DT_REG;
            break;
        case FS_DIRECTORY:
            dent.d_type = DT_DIR;
            break;
        case FS_CHARDEVICE:
            dent.d_type = DT_CHR;
            break;
        case FS_BLOCKDEVICE:
            dent.d_type = DT_BLK;
            break;
        case FS_PIPE:
            dent.d_type = DT_FIFO;
            break;
        case FS_SYMLINK:
            dent.d_type = DT_LNK;
            break;
        default:
            dent.d_type = DT_UNKNOWN;
            break;
    }
    
    return &dent;
}

/**
 * 在目录中查找文件
 */
static fs_node_t *ramfs_finddir(fs_node_t *node, const char *name) {
    if (node->type != FS_DIRECTORY) {
        return NULL;
    }
    
    ramfs_dir_t *dir = (ramfs_dir_t *)node->impl;
    if (!dir) {
        return NULL;
    }
    
    ramfs_dirent_t *entry = ramfs_find_entry(dir, name);
    return entry ? entry->node : NULL;
}

/**
 * 创建文件（VFS 操作函数）
 */
static int ramfs_create_file(fs_node_t *node, const char *name) {
    if (node->type != FS_DIRECTORY) {
        return -1;
    }
    
    ramfs_dir_t *dir = (ramfs_dir_t *)node->impl;
    if (!dir) {
        return -1;
    }
    
    // 检查文件是否已存在
    if (ramfs_find_entry(dir, name)) {
        return -1;
    }
    
    // 创建新文件节点
    fs_node_t *new_node = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!new_node) {
        return -1;
    }
    
    // 创建文件数据结构
    ramfs_file_t *file = (ramfs_file_t *)kmalloc(sizeof(ramfs_file_t));
    if (!file) {
        kfree(new_node);
        return -1;
    }
    
    // 初始化文件数据
    file->data = NULL;
    file->size = 0;
    file->capacity = 0;
    
    // 初始化文件节点
    memset(new_node, 0, sizeof(fs_node_t));
    strncpy(new_node->name, name, 127);
    new_node->name[127] = '\0';
    
    // 分配 inode（原子操作）
    spinlock_lock(&inode_alloc_lock);
    new_node->inode = next_inode++;
    spinlock_unlock(&inode_alloc_lock);
    
    new_node->type = FS_FILE;
    new_node->size = 0;
    new_node->permissions = FS_PERM_READ | FS_PERM_WRITE;
    new_node->impl = (uint32_t)file;
    
    // 设置操作函数
    new_node->read = ramfs_read;
    new_node->write = ramfs_write;
    new_node->open = ramfs_open;
    new_node->close = ramfs_close;
    
    // 添加到目录
    if (ramfs_add_entry(dir, name, new_node) != 0) {
        kfree(file);
        kfree(new_node);
        return -1;
    }
    
    return 0;
}

/**
 * 创建目录
 */
static int ramfs_mkdir(fs_node_t *node, const char *name, uint32_t permissions) {
    if (node->type != FS_DIRECTORY) {
        return -1;
    }
    
    ramfs_dir_t *parent_dir = (ramfs_dir_t *)node->impl;
    if (!parent_dir) {
        return -1;
    }
    
    // 检查目录是否已存在
    if (ramfs_find_entry(parent_dir, name)) {
        return -1;
    }
    
    // 创建新目录节点
    fs_node_t *new_node = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!new_node) {
        return -1;
    }
    
    // 创建目录数据结构
    ramfs_dir_t *new_dir = (ramfs_dir_t *)kmalloc(sizeof(ramfs_dir_t));
    if (!new_dir) {
        kfree(new_node);
        return -1;
    }
    
    // 初始化目录数据
    new_dir->entries = NULL;
    new_dir->count = 0;
    
    // 初始化目录节点
    memset(new_node, 0, sizeof(fs_node_t));
    strncpy(new_node->name, name, 127);
    new_node->name[127] = '\0';
    
    // 分配 inode（原子操作）
    spinlock_lock(&inode_alloc_lock);
    new_node->inode = next_inode++;
    spinlock_unlock(&inode_alloc_lock);
    
    new_node->type = FS_DIRECTORY;
    new_node->size = 0;
    new_node->permissions = permissions;
    new_node->impl = (uint32_t)new_dir;
    
    // 设置操作函数
    new_node->readdir = ramfs_readdir;
    new_node->finddir = ramfs_finddir;
    new_node->create = ramfs_create_file;
    new_node->mkdir = ramfs_mkdir;
    new_node->unlink = ramfs_unlink;
    
    // 添加到父目录
    if (ramfs_add_entry(parent_dir, name, new_node) != 0) {
        kfree(new_dir);
        kfree(new_node);
        return -1;
    }
    
    return 0;
}

/**
 * 删除文件或目录（VFS 操作函数）
 */
static int ramfs_unlink(fs_node_t *node, const char *name) {
    if (node->type != FS_DIRECTORY) {
        return -1;
    }
    
    ramfs_dir_t *dir = (ramfs_dir_t *)node->impl;
    if (!dir) {
        return -1;
    }
    
    // 查找要删除的条目
    ramfs_dirent_t *entry = ramfs_find_entry(dir, name);
    if (!entry) {
        return -1;  // 文件不存在
    }
    
    fs_node_t *target = entry->node;
    
    // 如果是目录，检查是否为空
    if (target->type == FS_DIRECTORY) {
        ramfs_dir_t *target_dir = (ramfs_dir_t *)target->impl;
        if (target_dir && target_dir->count > 0) {
            return -1;  // 目录不为空
        }
        
        // 释放目录数据
        if (target_dir) {
            kfree(target_dir);
        }
    }
    
    // 如果是文件，释放文件数据
    if (target->type == FS_FILE) {
        ramfs_file_t *file = (ramfs_file_t *)target->impl;
        if (file) {
            if (file->data) {
                kfree(file->data);
            }
            kfree(file);
        }
    }
    
    // 从目录中移除
    ramfs_remove_entry(dir, name);
    
    // 释放节点
    kfree(target);
    
    return 0;
}

// ============================================================================
// 公共接口
// ============================================================================

/**
 * 创建 ramfs 根目录
 */
fs_node_t *ramfs_create(const char *name) {
    // 创建根目录节点
    fs_node_t *root = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!root) {
        LOG_ERROR_MSG("RAMFS: Failed to allocate root node\n");
        return NULL;
    }
    
    // 创建根目录数据
    ramfs_dir_t *root_dir = (ramfs_dir_t *)kmalloc(sizeof(ramfs_dir_t));
    if (!root_dir) {
        kfree(root);
        LOG_ERROR_MSG("RAMFS: Failed to allocate root directory\n");
        return NULL;
    }
    
    // 初始化根目录数据
    root_dir->entries = NULL;
    root_dir->count = 0;
    
    // 初始化根目录节点
    memset(root, 0, sizeof(fs_node_t));
    strncpy(root->name, name ? name : "/", 127);
    root->name[127] = '\0';
    
    // 分配 inode（原子操作）
    spinlock_lock(&inode_alloc_lock);
    root->inode = next_inode++;
    spinlock_unlock(&inode_alloc_lock);
    
    root->type = FS_DIRECTORY;
    root->size = 0;
    root->permissions = FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC;
    root->impl = (uint32_t)root_dir;
    
    // 设置操作函数
    root->readdir = ramfs_readdir;
    root->finddir = ramfs_finddir;
    root->create = ramfs_create_file;
    root->mkdir = ramfs_mkdir;
    root->unlink = ramfs_unlink;
    
    return root;
}

/**
 * 初始化 ramfs（创建默认根文件系统）
 */
fs_node_t *ramfs_init(void) {
    LOG_INFO_MSG("RAMFS: Initializing RAM filesystem...\n");
    
    // 初始化 inode 分配锁
    spinlock_init(&inode_alloc_lock);
    
    fs_node_t *root = ramfs_create("/");
    if (!root) {
        LOG_ERROR_MSG("RAMFS: Failed to create root directory\n");
        return NULL;
    }
    
    LOG_INFO_MSG("RAMFS: Filesystem initialized successfully\n");
    return root;
}
