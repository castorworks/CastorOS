// ============================================================================
// ramfs.c - 基于 RAM 的简单文件系统实现
// ============================================================================

#include <fs/ramfs.h>
#include <fs/vfs.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <mm/heap.h>
#include <kernel/sync/spinlock.h>
#include <kernel/sync/mutex.h>

// ramfs 文件数据结构
typedef struct ramfs_file {
    uint8_t *data;        // 文件数据
    uint32_t size;        // 文件大小
    uint32_t capacity;    // 已分配容量
    sync::Mutex lock;         // 文件锁（保护文件数据）
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
    sync::Mutex lock;             // 目录锁（保护目录操作）
} ramfs_dir_t;

// 全局 inode 计数器
static uint32_t next_inode = 1;

// inode 分配锁（保护 next_inode 的并发访问）
static sync::Spinlock inode_alloc_lock;

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
static int ramfs_rename(fs_node_t *node, const char *old_name, const char *new_name);

/**
 * 读取文件
 */
static uint32_t ramfs_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (node->type != FS_FILE) {
        return 0;
    }
    
    ramfs_file_t *file = (ramfs_file_t *)node->impl;
    if (!file) {
        return 0;
    }
    
    // 加锁保护文件读取
    sync::MutexGuard guard(file->lock);
    
    if (!file->data) {
        return 0;
    }
    
    // 检查偏移量
    if (offset >= file->size) {
        return 0;
    }
    
    // 调整读取大小
    uint32_t to_read = size;
    if (to_read > file->size - offset) {
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
    
    // 加锁保护文件写入
    file->lock.lock();
    
    // 计算需要的总大小（回绕说明请求超出 32 位文件大小，拒绝）
    uint32_t new_size = offset + size;
    if (new_size < offset || new_size > 0xFFFFF000u) {
        file->lock.unlock();
        return 0;
    }
    
    // 如果需要扩容
    if (new_size > file->capacity) {
        // 计算新容量（向上取整到 4KB 的倍数）
        uint32_t new_capacity = (new_size + 4095) & ~4095;
        
        // 重新分配内存
        uint8_t *new_data = (uint8_t *)kmalloc(new_capacity);
        if (!new_data) {
            file->lock.unlock();
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
    
    // 写入位置超过文件末尾时，中间的空洞必须读出 0，而不是堆里的旧内容
    if (offset > file->size) {
        memset(file->data + file->size, 0, offset - file->size);
    }
    
    // 写入数据
    memcpy(file->data + offset, buffer, size);
    
    // 更新文件大小
    if (new_size > file->size) {
        file->size = new_size;
        node->size = new_size;
    }
    
    file->lock.unlock();
    return size;
}

/**
 * 截断或扩展文件
 *
 * 缩小时只改大小（容量保留，之后扩展会重新清零）；扩大时新增区域清零。
 */
static int ramfs_truncate(fs_node_t *node, uint32_t new_size) {
    if (node->type != FS_FILE) {
        return -1;
    }
    
    ramfs_file_t *file = (ramfs_file_t *)node->impl;
    if (!file || new_size > 0xFFFFF000u) {
        return -1;
    }
    
    sync::MutexGuard guard(file->lock);
    
    if (new_size > file->size) {
        if (new_size > file->capacity) {
            uint32_t new_capacity = (new_size + 4095) & ~4095u;
            uint8_t *new_data = (uint8_t *)kmalloc(new_capacity);
            if (!new_data) {
                return -1;
            }
            if (file->data && file->size > 0) {
                memcpy(new_data, file->data, file->size);
            }
            if (file->data) {
                kfree(file->data);
            }
            file->data = new_data;
            file->capacity = new_capacity;
        }
        memset(file->data + file->size, 0, new_size - file->size);
    }
    
    file->size = new_size;
    node->size = new_size;
    return 0;
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
    
    // 加锁保护目录读取
    dir->lock.lock();
    
    // 遍历到指定索引
    ramfs_dirent_t *current = dir->entries;
    uint32_t i = 0;
    
    while (current && i < index) {
        current = current->next;
        i++;
    }
    
    if (!current) {
        dir->lock.unlock();
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
    
    dir->lock.unlock();
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
    
    // 加锁保护目录查找
    fs_node_t *result;
    {
        sync::LockGuard guard(dir->lock);
        ramfs_dirent_t *entry = ramfs_find_entry(dir, name);
        result = entry ? entry->node : NULL;
    }
    
    // 增加引用计数
    if (result) {
        fs::Vfs::ref_node(result);
    }
    
    return result;
}

/**
 * 释放一个已从目录摘除的节点及其数据
 */
static void ramfs_destroy_node(fs_node_t *target) {
    if (target->type == FS_DIRECTORY) {
        ramfs_dir_t *target_dir = (ramfs_dir_t *)target->impl;
        if (target_dir) {
            kfree(target_dir);
        }
    } else if (target->type == FS_FILE) {
        ramfs_file_t *file = (ramfs_file_t *)target->impl;
        if (file) {
            if (file->data) {
                kfree(file->data);
            }
            kfree(file);
        }
    }
    kfree(target);
}

class RamfsFileOps final : public fs::NodeOps {
public:
    uint32_t supported() const override { return OP_READ | OP_WRITE | OP_OPEN | OP_CLOSE | OP_TRUNCATE; }
    uint32_t read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) const override {
        return ramfs_read(node, offset, size, buffer);
    }
    int truncate(fs_node_t *node, uint32_t new_size) const override {
        return ramfs_truncate(node, new_size);
    }
    uint32_t write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) const override {
        return ramfs_write(node, offset, size, buffer);
    }
    void open(fs_node_t *node, uint32_t flags) const override {
        ramfs_open(node, flags);
    }
    void close(fs_node_t *node) const override {
        ramfs_close(node);
    }
    void destroy(fs_node_t *node) const override {
        ramfs_destroy_node(node);
    }
};
static const RamfsFileOps ramfs_file_ops{};

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
    
    // 加锁保护目录修改
    sync::MutexGuard guard(dir->lock);
    
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
    file->lock.init();  // 初始化文件锁
    
    // 初始化文件节点
    memset(new_node, 0, sizeof(fs_node_t));
    strncpy(new_node->name, name, 127);
    new_node->name[127] = '\0';
    
    // 分配 inode（原子操作）
    {
        sync::LockGuard guard(inode_alloc_lock);
        new_node->inode = next_inode++;
    }
    
    new_node->type = FS_FILE;
    new_node->size = 0;
    new_node->permissions = FS_PERM_READ | FS_PERM_WRITE;
    new_node->impl = file;
    new_node->ref_count = 0;  // 初始化引用计数
    new_node->open_count = 0;
    new_node->flags = 0;  // RAMFS 节点不应该被自动释放
    
    // 设置操作函数
    new_node->ops = &ramfs_file_ops;
    
    // 添加到目录
    if (ramfs_add_entry(dir, name, new_node) != 0) {
        kfree(file);
        kfree(new_node);
        return -1;
    }
    
    return 0;
}

static int ramfs_mkdir(fs_node_t *node, const char *name, uint32_t permissions);

class RamfsDirOps final : public fs::NodeOps {
public:
    uint32_t supported() const override { return OP_READDIR | OP_FINDDIR | OP_CREATE | OP_MKDIR | OP_UNLINK | OP_RENAME; }
    struct dirent *readdir(fs_node_t *node, uint32_t index) const override {
        return ramfs_readdir(node, index);
    }
    fs_node_t *finddir(fs_node_t *node, const char *name) const override {
        return ramfs_finddir(node, name);
    }
    int create(fs_node_t *node, const char *name) const override {
        return ramfs_create_file(node, name);
    }
    int mkdir(fs_node_t *node, const char *name, uint32_t permissions) const override {
        return ramfs_mkdir(node, name, permissions);
    }
    void destroy(fs_node_t *node) const override {
        ramfs_destroy_node(node);
    }
    int unlink(fs_node_t *node, const char *name) const override {
        return ramfs_unlink(node, name);
    }
    int rename(fs_node_t *node, const char *old_name, const char *new_name) const override {
        return ramfs_rename(node, old_name, new_name);
    }
};
static const RamfsDirOps ramfs_dir_ops{};

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
    
    // 加锁保护父目录修改
    sync::MutexGuard guard(parent_dir->lock);
    
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
    new_dir->lock.init();  // 初始化新目录的锁
    
    // 初始化目录节点
    memset(new_node, 0, sizeof(fs_node_t));
    strncpy(new_node->name, name, 127);
    new_node->name[127] = '\0';
    
    // 分配 inode（原子操作）
    {
        sync::LockGuard guard(inode_alloc_lock);
        new_node->inode = next_inode++;
    }
    
    new_node->type = FS_DIRECTORY;
    new_node->size = 0;
    new_node->permissions = permissions;
    new_node->impl = new_dir;
    new_node->ref_count = 0;  // 初始化引用计数
    new_node->open_count = 0;
    new_node->flags = 0;  // RAMFS 节点不应该被自动释放
    
    // 设置操作函数
    new_node->ops = &ramfs_dir_ops;
    
    // 添加到父目录
    if (ramfs_add_entry(parent_dir, name, new_node) != 0) {
        kfree(new_dir);
        kfree(new_node);
        return -1;
    }
    
    return 0;
}

/**
 * 重命名文件或目录（VFS 操作函数）
 */
static int ramfs_rename(fs_node_t *node, const char *old_name, const char *new_name) {
    if (node->type != FS_DIRECTORY) {
        return -1;
    }
    
    ramfs_dir_t *dir = (ramfs_dir_t *)node->impl;
    if (!dir) {
        return -1;
    }
    
    // 检查参数有效性
    if (!old_name || !new_name || old_name[0] == '\0' || new_name[0] == '\0') {
        return -1;
    }
    
    // 如果新旧名字相同，直接返回成功
    if (strcmp(old_name, new_name) == 0) {
        return 0;
    }
    
    // 加锁保护目录修改
    dir->lock.lock();
    
    // 查找要重命名的条目
    ramfs_dirent_t *entry = ramfs_find_entry(dir, old_name);
    if (!entry) {
        dir->lock.unlock();
        LOG_ERROR_MSG("ramfs_rename: '%s' not found\n", old_name);
        return -1;  // 源文件不存在
    }
    
    // 检查目标名字是否已存在
    if (ramfs_find_entry(dir, new_name)) {
        dir->lock.unlock();
        LOG_ERROR_MSG("ramfs_rename: '%s' already exists\n", new_name);
        return -1;  // 目标文件已存在
    }
    
    // 更新目录项的名字
    strncpy(entry->name, new_name, 127);
    entry->name[127] = '\0';
    
    // 更新节点的名字
    strncpy(entry->node->name, new_name, 127);
    entry->node->name[127] = '\0';
    
    dir->lock.unlock();
    
    LOG_DEBUG_MSG("ramfs_rename: '%s' -> '%s' success\n", old_name, new_name);
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
    
    // 加锁保护父目录修改
    dir->lock.lock();
    
    // 查找要删除的条目
    ramfs_dirent_t *entry = ramfs_find_entry(dir, name);
    if (!entry) {
        dir->lock.unlock();
        return -1;  // 文件不存在
    }
    
    fs_node_t *target = entry->node;
    
    // 如果是目录，检查是否为空
    if (target->type == FS_DIRECTORY) {
        ramfs_dir_t *target_dir = (ramfs_dir_t *)target->impl;
        if (target_dir && target_dir->count > 0) {
            dir->lock.unlock();
            return -1;  // 目录不为空
        }
    }
    
    // 从目录中移除：之后路径查找再也找不到它
    ramfs_remove_entry(dir, name);
    
    if (fs::Vfs::is_pinned(target)) {
        // 仍有文件描述符指向它：只标记，最后一次关闭时由 destroy() 释放
        target->flags |= FS_NODE_FLAG_UNLINKED;
    } else {
        ramfs_destroy_node(target);
    }
    
    dir->lock.unlock();
    return 0;
}

// ============================================================================
// 公共接口
// ============================================================================

/**
 * 创建 ramfs 根目录
 */
fs_node_t *fs::Ramfs::create(const char *name) {
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
    root_dir->lock.init();  // 初始化根目录的锁
    
    // 初始化根目录节点
    memset(root, 0, sizeof(fs_node_t));
    strncpy(root->name, name ? name : "/", 127);
    root->name[127] = '\0';
    
    // 分配 inode（原子操作）
    {
        sync::LockGuard guard(inode_alloc_lock);
        root->inode = next_inode++;
    }
    
    root->type = FS_DIRECTORY;
    root->size = 0;
    root->permissions = FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC;
    root->impl = root_dir;
    root->ref_count = 0;  // 初始化引用计数
    root->open_count = 0;
    root->flags = 0;  // RAMFS 节点不应该被自动释放
    
    // 设置操作函数
    root->ops = &ramfs_dir_ops;
    
    return root;
}

/**
 * 初始化 ramfs（创建默认根文件系统）
 */
fs_node_t *fs::Ramfs::init() {
    LOG_INFO_MSG("RAMFS: Initializing RAM filesystem...\n");
    
    // 初始化 inode 分配锁
    inode_alloc_lock.init();
    
    fs_node_t *root = fs::Ramfs::create("/");
    if (!root) {
        LOG_ERROR_MSG("RAMFS: Failed to create root directory\n");
        return NULL;
    }
    
    LOG_INFO_MSG("RAMFS: Filesystem initialized successfully\n");
    return root;
}
