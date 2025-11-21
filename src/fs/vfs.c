// ============================================================================
// vfs.c - 虚拟文件系统实现
// ============================================================================

#include <fs/vfs.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <mm/heap.h>
#include <kernel/sync/mutex.h>

static fs_node_t *fs_root = NULL;

/* 挂载表 - 记录所有挂载点 */
#define MAX_MOUNTS 32
#define MAX_MOUNT_PATH 256

typedef struct {
    char path[MAX_MOUNT_PATH];   /* 挂载点路径（如 "/dev"） */
    fs_node_t *root;             /* 挂载的文件系统根节点 */
} vfs_mount_entry_t;

static vfs_mount_entry_t mount_table[MAX_MOUNTS];
static uint32_t mount_count = 0;

/* VFS 挂载表互斥锁 - 保护 mount_table 和 mount_count */
static mutex_t vfs_mount_mutex;

void vfs_init(void) {
    LOG_INFO_MSG("VFS: Initializing virtual file system...\n");
    fs_root = NULL;
    mount_count = 0;
    mutex_init(&vfs_mount_mutex);
    LOG_INFO_MSG("VFS: Mount table mutex initialized\n");
}

/* 查询挂载表，根据路径获取挂载的根节点 */
static fs_node_t *vfs_get_mounted_root_by_path(const char *path) {
    if (!path) {
        return NULL;
    }
    
    LOG_DEBUG_MSG("VFS: get_mounted_root_by_path: checking '%s' against %u mounts\n", path, mount_count);
    
    mutex_lock(&vfs_mount_mutex);
    
    for (uint32_t i = 0; i < mount_count; i++) {
        LOG_DEBUG_MSG("VFS: get_mounted_root_by_path: mount[%u] = {%s, %p}\n", i, 
                     mount_table[i].path, mount_table[i].root);
        if (strcmp(mount_table[i].path, path) == 0) {
            LOG_DEBUG_MSG("VFS: found mounted root for '%s': %p\n", path, mount_table[i].root);
            fs_node_t *result = mount_table[i].root;
            mutex_unlock(&vfs_mount_mutex);
            return result;
        }
    }
    
    mutex_unlock(&vfs_mount_mutex);
    LOG_DEBUG_MSG("VFS: no mounted root found for '%s'\n", path);
    return NULL;
}

fs_node_t *vfs_get_root(void) {
    return fs_root;
}

void vfs_set_root(fs_node_t *root) {
    fs_root = root;
    LOG_INFO_MSG("VFS: Root filesystem set\n");
}

uint32_t vfs_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (!node || !node->read) {
        return 0;
    }
    return node->read(node, offset, size, buffer);
}

uint32_t vfs_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (!node || !node->write) {
        return 0;
    }
    return node->write(node, offset, size, buffer);
}

void vfs_open(fs_node_t *node, uint32_t flags) {
    if (!node) {
        return;
    }
    if (node->open) {
        node->open(node, flags);
    }
}

void vfs_close(fs_node_t *node) {
    if (!node) {
        return;
    }
    if (node->close) {
        node->close(node);
    }
}

void vfs_release_node(fs_node_t *node) {
    if (!node) {
        return;
    }
    
    // 只释放动态分配的节点
    if (node->flags & FS_NODE_FLAG_ALLOCATED) {
        // 释放实现相关的数据（如 fat32_file_t）
        if (node->impl) {
            kfree((void *)node->impl);
        }
        // 释放节点本身
        kfree(node);
    }
}

struct dirent *vfs_readdir(fs_node_t *node, uint32_t index) {
    if (!node || node->type != FS_DIRECTORY) {
        LOG_DEBUG_MSG("VFS: readdir: invalid node or not directory\n");
        return NULL;
    }
    
    LOG_DEBUG_MSG("VFS: readdir: index=%u, node=%p\n", index, node);
    
    /* 正常读取（挂载点切换在 vfs_path_to_node 中处理） */
    if (!node->readdir) {
        LOG_DEBUG_MSG("VFS: readdir: node has no readdir callback\n");
        return NULL;
    }
    LOG_DEBUG_MSG("VFS: readdir: calling node->readdir\n");
    return node->readdir(node, index);
}

fs_node_t *vfs_finddir(fs_node_t *node, const char *name) {
    if (!node || node->type != FS_DIRECTORY) {
        LOG_DEBUG_MSG("VFS: finddir: invalid node or not directory\n");
        return NULL;
    }
    
    /* 处理特殊目录条目 '.' - 直接返回当前节点 */
    if (strcmp(name, ".") == 0) {
        return node;
    }
    
    /* 处理特殊目录条目 '..' - 让文件系统处理，如果文件系统不支持则回退 */
    if (strcmp(name, "..") == 0) {
        /* 首先尝试让文件系统处理 */
        if (node->finddir) {
            fs_node_t *parent = node->finddir(node, "..");
            if (parent) {
                return parent;
            }
        }
        /* 如果文件系统不支持 '..'，且当前是根目录，返回根目录 */
        if (node == fs_root) {
            return fs_root;
        }
        /* 对于其他情况，如果文件系统不支持，返回 NULL */
        /* 注意：这要求文件系统正确实现 '..' 查找 */
        return NULL;
    }
    
    /* 正常查找（挂载点切换在 vfs_path_to_node 中处理） */
    if (!node->finddir) {
        LOG_DEBUG_MSG("VFS: finddir: node has no finddir callback\n");
        return NULL;
    }
    LOG_DEBUG_MSG("VFS: finddir: calling node->finddir for '%s'\n", name);
    return node->finddir(node, name);
}

// 路径解析：将路径字符串转换为文件节点
fs_node_t *vfs_path_to_node(const char *path) {
    if (!path || !fs_root) {
        return NULL;
    }
    
    LOG_DEBUG_MSG("VFS: path_to_node: resolving '%s'\n", path);
    
    // 处理根目录
    if (strcmp(path, "/") == 0) {
        return fs_root;
    }
    
    /* 检查是否是挂载点或其子路径 */
    fs_node_t *mounted = vfs_get_mounted_root_by_path(path);
    if (mounted != NULL) {
        LOG_DEBUG_MSG("VFS: path_to_node: '%s' is a mount point, returning root %p\n", path, mounted);
        return mounted;
    }
    
    /* 检查是否是挂载点的子路径（如 /dev/zero） */
    mutex_lock(&vfs_mount_mutex);
    for (uint32_t i = 0; i < mount_count; i++) {
        const char *mount_path = mount_table[i].path;
        uint32_t mount_len = strlen(mount_path);
        
        /* 检查路径是否以挂载点开头，且后面是 / */
        if (strncmp(path, mount_path, mount_len) == 0 && 
            (path[mount_len] == '/' || path[mount_len] == '\0')) {
            
            LOG_DEBUG_MSG("VFS: path_to_node: '%s' is under mount point '%s'\n", path, mount_path);
            
            /* 如果正好是挂载点，返回根 */
            if (path[mount_len] == '\0') {
                mutex_unlock(&vfs_mount_mutex);
                return mount_table[i].root;
            }
            
            /* 否则，在挂载的根中继续解析剩余路径 */
            const char *remaining = path + mount_len + 1;  /* 跳过 '/' */
            LOG_DEBUG_MSG("VFS: path_to_node: resolving '%s' in mounted filesystem\n", remaining);
            
            fs_node_t *current = mount_table[i].root;
            char token[128];
            uint32_t j = 0;
            
            while (*remaining) {
                /* 提取路径组件 */
                j = 0;
                while (*remaining && *remaining != '/' && j < 127) {
                    token[j++] = *remaining++;
                }
                
                if (*remaining && *remaining != '/') {
                    LOG_ERROR_MSG("VFS: Path component too long\n");
                    vfs_release_node(current);  // 释放中间节点
                    mutex_unlock(&vfs_mount_mutex);
                    return NULL;
                }
                
                token[j] = '\0';
                
                /* 跳过连续的 '/' */
                while (*remaining == '/') {
                    remaining++;
                }
                
                if (token[0] == '\0') {
                    continue;
                }
                
                /* 处理 '.' - 跳过，保持在当前目录 */
                if (strcmp(token, ".") == 0) {
                    continue;
                }
                
                /* 处理 '..' - 转到父目录 */
                if (strcmp(token, "..") == 0) {
                    fs_node_t *parent = vfs_finddir(current, "..");
                    if (!parent) {
                        LOG_DEBUG_MSG("VFS: path_to_node: failed to find parent for '..' in mounted fs\n");
                        vfs_release_node(current);  // 释放中间节点
                        mutex_unlock(&vfs_mount_mutex);
                        return NULL;
                    }
                    // 释放旧的 current（如果它是动态分配的且不是根）
                    if (current != mount_table[i].root) {
                        vfs_release_node(current);
                    }
                    current = parent;
                    continue;
                }
                
                /* 查找下一个节点 */
                LOG_DEBUG_MSG("VFS: path_to_node: looking for '%s' in mounted fs\n", token);
                fs_node_t *next = vfs_finddir(current, token);
                if (!next) {
                    LOG_DEBUG_MSG("VFS: path_to_node: failed to find '%s' in mounted fs\n", token);
                    // 释放中间节点
                    if (current != mount_table[i].root) {
                        vfs_release_node(current);
                    }
                    mutex_unlock(&vfs_mount_mutex);
                    return NULL;
                }
                
                // 释放旧的 current（如果它是动态分配的且不是根）
                if (current != mount_table[i].root) {
                    vfs_release_node(current);
                }
                current = next;
            }
            
            LOG_DEBUG_MSG("VFS: path_to_node: resolved to %p in mounted fs\n", current);
            mutex_unlock(&vfs_mount_mutex);
            return current;
        }
    }
    mutex_unlock(&vfs_mount_mutex);
    
    /* 正常路径解析（不在任何挂载点下） */
    // 跳过开头的 '/'
    if (path[0] == '/') {
        path++;
    }
    
    fs_node_t *current = fs_root;
    char token[128];
    uint32_t i = 0;
    
    while (*path) {
        // 提取路径的一部分
        i = 0;
        while (*path && *path != '/' && i < 127) {
            token[i++] = *path++;
        }
        
        // 检查路径组件是否过长
        if (*path && *path != '/') {
            LOG_ERROR_MSG("VFS: Path component too long\n");
            // 释放中间节点
            if (current != fs_root) {
                vfs_release_node(current);
            }
            return NULL;
        }
        
        token[i] = '\0';
        
        // 跳过连续的 '/'
        while (*path == '/') {
            path++;
        }
        
        // 如果是空字符串，跳过
        if (token[0] == '\0') {
            continue;
        }
        
        /* 处理 '.' - 跳过，保持在当前目录 */
        if (strcmp(token, ".") == 0) {
            continue;
        }
        
        /* 处理 '..' - 转到父目录 */
        if (strcmp(token, "..") == 0) {
            fs_node_t *parent = vfs_finddir(current, "..");
            if (!parent) {
                LOG_DEBUG_MSG("VFS: path_to_node: failed to find parent for '..'\n");
                // 释放中间节点
                if (current != fs_root) {
                    vfs_release_node(current);
                }
                return NULL;
            }
            // 释放旧的 current（如果它是动态分配的且不是根）
            if (current != fs_root) {
                vfs_release_node(current);
            }
            current = parent;
            continue;
        }
        
        /* 查找下一个节点 */
        LOG_DEBUG_MSG("VFS: path_to_node: looking for '%s' in %p\n", token, current);
        fs_node_t *next = vfs_finddir(current, token);
        if (!next) {
            LOG_DEBUG_MSG("VFS: path_to_node: failed to find '%s'\n", token);
            // 释放中间节点
            if (current != fs_root) {
                vfs_release_node(current);
            }
            return NULL;  /* 路径不存在 */
        }
        
        LOG_DEBUG_MSG("VFS: path_to_node: found '%s' at %p (type=%u)\n", token, next, next->type);
        // 释放旧的 current（如果它是动态分配的且不是根）
        if (current != fs_root) {
            vfs_release_node(current);
        }
        current = next;
    }
    
    LOG_DEBUG_MSG("VFS: path_to_node: resolved to %p\n", current);
    return current;
}

// 创建文件
int vfs_create(const char *path) {
    if (!path || !fs_root) {
        return -1;
    }
    
    // 找到最后一个 '/' 分隔符
    const char *last_slash = NULL;
    const char *p = path;
    while (*p) {
        if (*p == '/') {
            last_slash = p;
        }
        p++;
    }
    
    // 获取父目录路径和新文件名
    char parent_path[256];
    const char *file_name;
    
    if (last_slash == NULL || last_slash == path) {
        // 在根目录下创建
        parent_path[0] = '/';
        parent_path[1] = '\0';
        file_name = (last_slash == path) ? path + 1 : path;
    } else {
        // 在子目录下创建
        uint32_t len = last_slash - path;
        if (len >= 256) {
            return -1;
        }
        strncpy(parent_path, path, len);
        parent_path[len] = '\0';
        file_name = last_slash + 1;
    }
    
    // 查找父目录
    fs_node_t *parent = vfs_path_to_node(parent_path);
    if (!parent || parent->type != FS_DIRECTORY) {
        vfs_release_node(parent);  // 释放节点（即使为NULL也安全）
        return -1;
    }
    
    // 调用父目录的 create 操作
    if (!parent->create) {
        vfs_release_node(parent);  // 释放节点
        return -1;
    }
    
    int result = parent->create(parent, file_name);
    vfs_release_node(parent);  // 释放节点
    return result;
}

// 创建目录
int vfs_mkdir(const char *path, uint32_t permissions) {
    if (!path || !fs_root) {
        return -1;
    }
    
    // 找到最后一个 '/' 分隔符
    const char *last_slash = NULL;
    const char *p = path;
    while (*p) {
        if (*p == '/') {
            last_slash = p;
        }
        p++;
    }
    
    // 获取父目录路径和新目录名
    char parent_path[256];
    const char *dir_name;
    
    if (last_slash == NULL || last_slash == path) {
        // 在根目录下创建
        parent_path[0] = '/';
        parent_path[1] = '\0';
        dir_name = (last_slash == path) ? path + 1 : path;
    } else {
        // 在子目录下创建
        uint32_t len = last_slash - path;
        if (len >= 256) {
            return -1;
        }
        strncpy(parent_path, path, len);
        parent_path[len] = '\0';
        dir_name = last_slash + 1;
    }
    
    // 查找父目录
    fs_node_t *parent = vfs_path_to_node(parent_path);
    if (!parent || parent->type != FS_DIRECTORY) {
        vfs_release_node(parent);  // 释放节点
        return -1;
    }
    
    // 调用父目录的 mkdir 操作
    if (!parent->mkdir) {
        vfs_release_node(parent);  // 释放节点
        return -1;
    }
    
    int result = parent->mkdir(parent, dir_name, permissions);
    vfs_release_node(parent);  // 释放节点
    return result;
}

// 删除文件或目录
int vfs_unlink(const char *path) {
    if (!path || !fs_root) {
        return -1;
    }
    
    // 不能删除根目录
    if (strcmp(path, "/") == 0) {
        return -1;
    }
    
    // 找到最后一个 '/' 分隔符
    const char *last_slash = NULL;
    const char *p = path;
    while (*p) {
        if (*p == '/') {
            last_slash = p;
        }
        p++;
    }
    
    // 获取父目录路径和文件名
    char parent_path[256];
    const char *file_name;
    
    if (last_slash == NULL || last_slash == path) {
        // 在根目录下删除
        parent_path[0] = '/';
        parent_path[1] = '\0';
        file_name = (last_slash == path) ? path + 1 : path;
    } else {
        // 在子目录下删除
        uint32_t len = last_slash - path;
        if (len >= 256) {
            return -1;
        }
        strncpy(parent_path, path, len);
        parent_path[len] = '\0';
        file_name = last_slash + 1;
    }
    
    // 查找父目录
    fs_node_t *parent = vfs_path_to_node(parent_path);
    if (!parent || parent->type != FS_DIRECTORY) {
        vfs_release_node(parent);  // 释放节点
        return -1;
    }
    
    // 调用父目录的 unlink 操作
    if (!parent->unlink) {
        vfs_release_node(parent);  // 释放节点
        return -1;
    }
    
    int result = parent->unlink(parent, file_name);
    vfs_release_node(parent);  // 释放节点
    return result;
}

// 挂载文件系统到指定路径
int vfs_mount(const char *path, fs_node_t *root) {
    if (!path || !root || !fs_root) {
        LOG_ERROR_MSG("VFS: mount: invalid arguments (path=%p, root=%p, fs_root=%p)\n", path, root, fs_root);
        return -1;
    }
    
    LOG_DEBUG_MSG("VFS: mount: mounting filesystem at '%s' (root=%p)\n", path, root);
    
    /* 查找挂载点 */
    fs_node_t *mount_point = vfs_path_to_node(path);
    if (!mount_point) {
        LOG_ERROR_MSG("VFS: Mount point '%s' not found\n", path);
        return -1;
    }
    
    LOG_DEBUG_MSG("VFS: mount: found mount_point=%p (type=%u)\n", mount_point, mount_point->type);
    
    if (mount_point->type != FS_DIRECTORY) {
        LOG_ERROR_MSG("VFS: Mount point '%s' is not a directory\n", path);
        vfs_release_node(mount_point);  // 释放节点
        return -1;
    }
    
    // 验证完成，释放挂载点节点（我们只需要验证路径，不需要保留节点）
    vfs_release_node(mount_point);
    
    /* 获取挂载表锁，保护后续的检查和修改操作 */
    mutex_lock(&vfs_mount_mutex);
    
    /* 检查是否已经挂载了文件系统 */
    bool already_mounted = false;
    for (uint32_t i = 0; i < mount_count; i++) {
        if (strcmp(mount_table[i].path, path) == 0) {
            already_mounted = true;
            break;
        }
    }
    
    if (already_mounted) {
        mutex_unlock(&vfs_mount_mutex);
        LOG_ERROR_MSG("VFS: Mount point '%s' is already mounted\n", path);
        return -1;
    }
    
    /* 检查挂载表是否满 */
    if (mount_count >= MAX_MOUNTS) {
        mutex_unlock(&vfs_mount_mutex);
        LOG_ERROR_MSG("VFS: Mount table is full (max %u mounts)\n", MAX_MOUNTS);
        return -1;
    }
    
    /* 添加到挂载表 */
    strncpy(mount_table[mount_count].path, path, MAX_MOUNT_PATH - 1);
    mount_table[mount_count].path[MAX_MOUNT_PATH - 1] = '\0';
    mount_table[mount_count].root = root;
    mount_count++;
    
    mutex_unlock(&vfs_mount_mutex);
    
    LOG_INFO_MSG("VFS: Filesystem mounted at '%s' (root=%p, total_mounts=%u)\n", 
                 path, root, mount_count);
    
    return 0;
}
