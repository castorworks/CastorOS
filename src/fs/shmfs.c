// ============================================================================
// shmfs.c - 共享内存文件系统实现
// ============================================================================
//
// 类似 Linux 的 /dev/shm，提供进程间共享内存的能力。
// 文件数据存储在物理内存中，可被多个进程共享。
//
// ============================================================================

#include <fs/shmfs.h>
#include <fs/vfs.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/mm_types.h>

// 全局 inode 计数器
static uint32_t shmfs_next_inode = 1;
static spinlock_t shmfs_inode_lock;

// 用于标识 shmfs 节点的魔数
#define SHMFS_MAGIC 0x53484D46  // "SHMF"

// ============================================================================
// 内部辅助函数
// ============================================================================

/**
 * 查找目录中的条目
 */
static shmfs_dirent_t *shmfs_find_entry(shmfs_dir_t *dir, const char *name) {
    shmfs_dirent_t *current = dir->entries;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

/**
 * 为共享内存文件分配物理页
 */
static int shmfs_alloc_pages(shmfs_file_t *file, uint32_t new_size) {
    uint32_t old_pages = file->num_pages;
    uint32_t new_pages = (new_size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    if (new_pages == 0) {
        new_pages = 1;  // 至少分配一页
    }
    
    if (new_pages <= old_pages) {
        return 0;  // 不需要分配新页
    }
    
    // 找到页链表末尾
    shmfs_page_t **last = &file->pages;
    while (*last) {
        last = &(*last)->next;
    }
    
    // 分配新的物理页
    for (uint32_t i = old_pages; i < new_pages; i++) {
        paddr_t phys = pmm_alloc_frame();
        if (phys == PADDR_INVALID) {
            LOG_ERROR_MSG("shmfs: out of physical memory\n");
            return -1;
        }
        
        shmfs_page_t *page = (shmfs_page_t *)kmalloc(sizeof(shmfs_page_t));
        if (!page) {
            pmm_free_frame(phys);
            return -1;
        }
        
        page->phys_addr = phys;
        page->next = NULL;
        *last = page;
        last = &page->next;
        file->num_pages++;
        
        // 清零新分配的页
        // 在内核中可以直接访问物理地址（通过恒等映射或内核虚拟地址）
        uint8_t *virt_ptr = (uint8_t *)PHYS_TO_VIRT(phys);
        memset(virt_ptr, 0, PAGE_SIZE);
    }
    
    return 0;
}

/**
 * 释放共享内存文件的所有物理页
 */
static void shmfs_free_pages(shmfs_file_t *file) {
    shmfs_page_t *page = file->pages;
    while (page) {
        shmfs_page_t *next = page->next;
        pmm_free_frame(page->phys_addr);
        kfree(page);
        page = next;
    }
    file->pages = NULL;
    file->num_pages = 0;
}

/**
 * 获取指定索引的页
 */
static shmfs_page_t *shmfs_get_page(shmfs_file_t *file, uint32_t page_idx) {
    shmfs_page_t *page = file->pages;
    for (uint32_t i = 0; i < page_idx && page; i++) {
        page = page->next;
    }
    return page;
}

// ============================================================================
// VFS 操作函数实现
// ============================================================================

// 前向声明
static int shmfs_unlink(fs_node_t *node, const char *name);
static int shmfs_create_file(fs_node_t *node, const char *name);
static struct dirent *shmfs_readdir(fs_node_t *node, uint32_t index);
static fs_node_t *shmfs_finddir(fs_node_t *node, const char *name);

/**
 * 读取共享内存文件
 */
static uint32_t shmfs_read(fs_node_t *node, uint32_t offset, 
                            uint32_t size, uint8_t *buffer) {
    if (node->type != FS_FILE) {
        return 0;
    }
    
    shmfs_file_t *file = (shmfs_file_t *)node->impl;
    if (!file) {
        return 0;
    }
    
    mutex_lock(&file->lock);
    
    // 检查偏移量
    if (offset >= file->size) {
        mutex_unlock(&file->lock);
        return 0;
    }
    
    // 调整读取大小
    uint32_t to_read = size;
    if (offset + to_read > file->size) {
        to_read = file->size - offset;
    }
    
    // 按页读取
    uint32_t bytes_read = 0;
    uint32_t page_idx = offset / PAGE_SIZE;
    uint32_t page_offset = offset % PAGE_SIZE;
    
    shmfs_page_t *page = shmfs_get_page(file, page_idx);
    
    while (bytes_read < to_read && page) {
        uint32_t chunk = PAGE_SIZE - page_offset;
        if (chunk > to_read - bytes_read) {
            chunk = to_read - bytes_read;
        }
        
        // 从物理页复制数据
        uint8_t *virt_ptr = (uint8_t *)PHYS_TO_VIRT(page->phys_addr);
        memcpy(buffer + bytes_read, virt_ptr + page_offset, chunk);
        
        bytes_read += chunk;
        page_offset = 0;  // 后续页从头开始
        page = page->next;
    }
    
    mutex_unlock(&file->lock);
    return bytes_read;
}

/**
 * 写入共享内存文件
 */
static uint32_t shmfs_write(fs_node_t *node, uint32_t offset, 
                             uint32_t size, uint8_t *buffer) {
    if (node->type != FS_FILE) {
        return 0;
    }
    
    shmfs_file_t *file = (shmfs_file_t *)node->impl;
    if (!file) {
        return 0;
    }
    
    mutex_lock(&file->lock);
    
    // 扩展文件大小（如果需要）
    uint32_t new_size = offset + size;
    if (new_size > file->size) {
        if (shmfs_alloc_pages(file, new_size) != 0) {
            mutex_unlock(&file->lock);
            return 0;
        }
        file->size = new_size;
        node->size = new_size;
    }
    
    // 按页写入
    uint32_t bytes_written = 0;
    uint32_t page_idx = offset / PAGE_SIZE;
    uint32_t page_offset = offset % PAGE_SIZE;
    
    shmfs_page_t *page = shmfs_get_page(file, page_idx);
    
    while (bytes_written < size && page) {
        uint32_t chunk = PAGE_SIZE - page_offset;
        if (chunk > size - bytes_written) {
            chunk = size - bytes_written;
        }
        
        // 写入物理页
        uint8_t *virt_ptr = (uint8_t *)PHYS_TO_VIRT(page->phys_addr);
        memcpy(virt_ptr + page_offset, buffer + bytes_written, chunk);
        
        bytes_written += chunk;
        page_offset = 0;
        page = page->next;
    }
    
    mutex_unlock(&file->lock);
    return bytes_written;
}

/**
 * 截断文件（ftruncate 支持）
 */
static int shmfs_truncate(fs_node_t *node, uint32_t new_size) {
    if (node->type != FS_FILE) {
        return -1;
    }
    
    shmfs_file_t *file = (shmfs_file_t *)node->impl;
    if (!file) {
        return -1;
    }
    
    mutex_lock(&file->lock);
    
    if (new_size > file->size) {
        // 扩展
        if (shmfs_alloc_pages(file, new_size) != 0) {
            mutex_unlock(&file->lock);
            return -1;
        }
    } else if (new_size < file->size) {
        // 收缩：释放多余的页
        uint32_t keep_pages = (new_size + PAGE_SIZE - 1) / PAGE_SIZE;
        if (keep_pages == 0 && new_size > 0) {
            keep_pages = 1;
        }
        
        shmfs_page_t *page = file->pages;
        shmfs_page_t *prev = NULL;
        uint32_t idx = 0;
        
        while (page) {
            if (idx >= keep_pages) {
                // 释放此页及后续所有页
                if (prev) {
                    prev->next = NULL;
                } else {
                    file->pages = NULL;
                }
                
                while (page) {
                    shmfs_page_t *next = page->next;
                    pmm_free_frame(page->phys_addr);
                    kfree(page);
                    file->num_pages--;
                    page = next;
                }
                break;
            }
            prev = page;
            page = page->next;
            idx++;
        }
    }
    
    file->size = new_size;
    node->size = new_size;
    
    mutex_unlock(&file->lock);
    return 0;
}

/**
 * 打开文件
 */
static void shmfs_open(fs_node_t *node, uint32_t flags) {
    (void)node;
    (void)flags;
    // shmfs 不需要特殊的打开操作
}

/**
 * 关闭文件
 */
static void shmfs_close(fs_node_t *node) {
    (void)node;
    // shmfs 不需要特殊的关闭操作
}

/**
 * 读取目录项
 */
static struct dirent *shmfs_readdir(fs_node_t *node, uint32_t index) {
    if (node->type != FS_DIRECTORY) {
        return NULL;
    }
    
    shmfs_dir_t *dir = (shmfs_dir_t *)node->impl;
    if (!dir) {
        return NULL;
    }
    
    mutex_lock(&dir->lock);
    
    // 遍历到指定索引
    shmfs_dirent_t *current = dir->entries;
    uint32_t i = 0;
    
    while (current && i < index) {
        current = current->next;
        i++;
    }
    
    if (!current) {
        mutex_unlock(&dir->lock);
        return NULL;
    }
    
    // 创建返回的 dirent（静态变量）
    static struct dirent dent;
    
    strncpy(dent.d_name, current->name, 255);
    dent.d_name[255] = '\0';
    dent.d_ino = current->node->inode;
    dent.d_reclen = sizeof(struct dirent);
    dent.d_off = index + 1;
    
    // 设置类型
    switch (current->node->type) {
        case FS_FILE:
            dent.d_type = DT_REG;
            break;
        case FS_DIRECTORY:
            dent.d_type = DT_DIR;
            break;
        default:
            dent.d_type = DT_UNKNOWN;
            break;
    }
    
    mutex_unlock(&dir->lock);
    return &dent;
}

/**
 * 在目录中查找文件
 */
static fs_node_t *shmfs_finddir(fs_node_t *node, const char *name) {
    if (node->type != FS_DIRECTORY) {
        return NULL;
    }
    
    shmfs_dir_t *dir = (shmfs_dir_t *)node->impl;
    if (!dir) {
        return NULL;
    }
    
    mutex_lock(&dir->lock);
    shmfs_dirent_t *entry = shmfs_find_entry(dir, name);
    fs_node_t *result = entry ? entry->node : NULL;
    mutex_unlock(&dir->lock);
    
    // 增加引用计数
    if (result) {
        vfs_ref_node(result);
    }
    
    return result;
}

/**
 * 创建共享内存文件
 */
static int shmfs_create_file(fs_node_t *node, const char *name) {
    if (node->type != FS_DIRECTORY) {
        return -1;
    }
    
    shmfs_dir_t *dir = (shmfs_dir_t *)node->impl;
    if (!dir) {
        return -1;
    }
    
    mutex_lock(&dir->lock);
    
    // 检查文件是否已存在
    if (shmfs_find_entry(dir, name)) {
        mutex_unlock(&dir->lock);
        return -1;  // 已存在
    }
    
    // 创建新文件节点
    fs_node_t *new_node = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!new_node) {
        mutex_unlock(&dir->lock);
        return -1;
    }
    
    // 创建文件数据结构
    shmfs_file_t *file = (shmfs_file_t *)kmalloc(sizeof(shmfs_file_t));
    if (!file) {
        kfree(new_node);
        mutex_unlock(&dir->lock);
        return -1;
    }
    
    // 初始化文件数据
    file->pages = NULL;
    file->size = 0;
    file->num_pages = 0;
    file->map_count = 0;
    mutex_init(&file->lock);
    
    // 初始化文件节点
    memset(new_node, 0, sizeof(fs_node_t));
    strncpy(new_node->name, name, 127);
    new_node->name[127] = '\0';
    
    spinlock_lock(&shmfs_inode_lock);
    new_node->inode = shmfs_next_inode++;
    spinlock_unlock(&shmfs_inode_lock);
    
    new_node->type = FS_FILE;
    new_node->size = 0;
    new_node->permissions = FS_PERM_READ | FS_PERM_WRITE;
    new_node->impl = file;
    new_node->impl_data = SHMFS_MAGIC;  // 用于标识 shmfs 节点
    new_node->ref_count = 0;
    new_node->flags = 0;
    
    // 设置操作函数
    new_node->read = shmfs_read;
    new_node->write = shmfs_write;
    new_node->open = shmfs_open;
    new_node->close = shmfs_close;
    new_node->truncate = shmfs_truncate;
    
    // 创建目录项
    shmfs_dirent_t *new_entry = (shmfs_dirent_t *)kmalloc(sizeof(shmfs_dirent_t));
    if (!new_entry) {
        kfree(file);
        kfree(new_node);
        mutex_unlock(&dir->lock);
        return -1;
    }
    
    strncpy(new_entry->name, name, 127);
    new_entry->name[127] = '\0';
    new_entry->node = new_node;
    new_entry->next = dir->entries;
    dir->entries = new_entry;
    dir->count++;
    
    LOG_DEBUG_MSG("shmfs: created file '%s'\n", name);
    
    mutex_unlock(&dir->lock);
    return 0;
}

/**
 * 删除共享内存文件
 */
static int shmfs_unlink(fs_node_t *node, const char *name) {
    if (node->type != FS_DIRECTORY) {
        return -1;
    }
    
    shmfs_dir_t *dir = (shmfs_dir_t *)node->impl;
    if (!dir) {
        return -1;
    }
    
    mutex_lock(&dir->lock);
    
    // 查找并删除
    shmfs_dirent_t **current = &dir->entries;
    while (*current) {
        if (strcmp((*current)->name, name) == 0) {
            shmfs_dirent_t *to_remove = *current;
            fs_node_t *target = to_remove->node;
            
            // 检查是否还有进程在映射
            if (target->type == FS_FILE) {
                shmfs_file_t *file = (shmfs_file_t *)target->impl;
                if (file && file->map_count > 0) {
                    // 有进程还在使用此共享内存
                    LOG_WARN_MSG("shmfs: cannot unlink '%s', map_count=%d\n", 
                                 name, file->map_count);
                    mutex_unlock(&dir->lock);
                    return -1;
                }
                
                // 释放物理页
                if (file) {
                    shmfs_free_pages(file);
                    kfree(file);
                }
            }
            
            *current = (*current)->next;
            kfree(to_remove);
            kfree(target);
            dir->count--;
            
            LOG_DEBUG_MSG("shmfs: unlinked file '%s'\n", name);
            
            mutex_unlock(&dir->lock);
            return 0;
        }
        current = &(*current)->next;
    }
    
    mutex_unlock(&dir->lock);
    return -1;  // 未找到
}

// ============================================================================
// 公共接口
// ============================================================================

/**
 * 获取共享内存文件的物理页列表（供 mmap 使用）
 */
uint32_t shmfs_get_phys_pages(fs_node_t *node, uint32_t offset, 
                               uint32_t num_pages, uint32_t *phys_pages) {
    if (!node || node->type != FS_FILE || !phys_pages) {
        return 0;
    }
    
    shmfs_file_t *file = (shmfs_file_t *)node->impl;
    if (!file) {
        return 0;
    }
    
    mutex_lock(&file->lock);
    
    uint32_t start_page = offset / PAGE_SIZE;
    uint32_t count = 0;
    
    shmfs_page_t *page = shmfs_get_page(file, start_page);
    
    while (page && count < num_pages) {
        phys_pages[count++] = page->phys_addr;
        page = page->next;
    }
    
    mutex_unlock(&file->lock);
    return count;
}

/**
 * 增加映射计数
 */
void shmfs_map_ref(fs_node_t *node) {
    if (!node || node->type != FS_FILE) {
        return;
    }
    
    shmfs_file_t *file = (shmfs_file_t *)node->impl;
    if (file) {
        mutex_lock(&file->lock);
        file->map_count++;
        LOG_DEBUG_MSG("shmfs: map_ref, count=%d\n", file->map_count);
        mutex_unlock(&file->lock);
    }
}

/**
 * 减少映射计数
 */
void shmfs_map_unref(fs_node_t *node) {
    if (!node || node->type != FS_FILE) {
        return;
    }
    
    shmfs_file_t *file = (shmfs_file_t *)node->impl;
    if (file) {
        mutex_lock(&file->lock);
        if (file->map_count > 0) {
            file->map_count--;
        }
        LOG_DEBUG_MSG("shmfs: map_unref, count=%d\n", file->map_count);
        mutex_unlock(&file->lock);
    }
}

/**
 * 检查节点是否为 shmfs 文件
 */
bool shmfs_is_shmfs_node(fs_node_t *node) {
    if (!node || node->type != FS_FILE) {
        return false;
    }
    return node->impl_data == SHMFS_MAGIC;
}

/**
 * 创建 shmfs 根目录
 */
fs_node_t *shmfs_create(const char *name) {
    // 创建根目录节点
    fs_node_t *root = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!root) {
        LOG_ERROR_MSG("SHMFS: Failed to allocate root node\n");
        return NULL;
    }
    
    // 创建根目录数据
    shmfs_dir_t *root_dir = (shmfs_dir_t *)kmalloc(sizeof(shmfs_dir_t));
    if (!root_dir) {
        kfree(root);
        LOG_ERROR_MSG("SHMFS: Failed to allocate root directory\n");
        return NULL;
    }
    
    // 初始化根目录数据
    root_dir->entries = NULL;
    root_dir->count = 0;
    mutex_init(&root_dir->lock);
    
    // 初始化根目录节点
    memset(root, 0, sizeof(fs_node_t));
    strncpy(root->name, name ? name : "shm", 127);
    root->name[127] = '\0';
    
    spinlock_lock(&shmfs_inode_lock);
    root->inode = shmfs_next_inode++;
    spinlock_unlock(&shmfs_inode_lock);
    
    root->type = FS_DIRECTORY;
    root->size = 0;
    root->permissions = FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC;
    root->impl = root_dir;
    root->impl_data = SHMFS_MAGIC;
    root->ref_count = 0;
    root->flags = 0;
    
    // 设置操作函数
    root->readdir = shmfs_readdir;
    root->finddir = shmfs_finddir;
    root->create = shmfs_create_file;
    root->unlink = shmfs_unlink;
    
    return root;
}

/**
 * 初始化 shmfs
 */
fs_node_t *shmfs_init(void) {
    LOG_INFO_MSG("SHMFS: Initializing shared memory filesystem...\n");
    
    // 初始化 inode 分配锁
    spinlock_init(&shmfs_inode_lock);
    
    fs_node_t *root = shmfs_create("shm");
    if (!root) {
        LOG_ERROR_MSG("SHMFS: Failed to create root directory\n");
        return NULL;
    }
    
    LOG_INFO_MSG("SHMFS: Filesystem initialized successfully\n");
    return root;
}

