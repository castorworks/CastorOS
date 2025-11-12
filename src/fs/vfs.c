// ============================================================================
// vfs.c - 虚拟文件系统实现
// ============================================================================

#include <fs/vfs.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <mm/heap.h>

static fs_node_t *fs_root = NULL;

void vfs_init(void) {
    LOG_INFO_MSG("VFS: Initializing virtual file system...\n");
    fs_root = NULL;
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

struct dirent *vfs_readdir(fs_node_t *node, uint32_t index) {
    if (!node || node->type != FS_DIRECTORY) {
        return NULL;
    }
    
    // 检查是否是挂载点
    if (node->ptr != NULL) {
        // 这是一个挂载点，在挂载的文件系统中读取
        fs_node_t *mounted = (fs_node_t *)node->ptr;
        if (mounted->readdir) {
            return mounted->readdir(mounted, index);
        }
        return NULL;
    }
    
    // 正常读取
    if (!node->readdir) {
        return NULL;
    }
    return node->readdir(node, index);
}

fs_node_t *vfs_finddir(fs_node_t *node, const char *name) {
    if (!node || node->type != FS_DIRECTORY) {
        return NULL;
    }
    
    // 检查是否是挂载点
    if (node->ptr != NULL) {
        // 这是一个挂载点，在挂载的文件系统中查找
        fs_node_t *mounted = (fs_node_t *)node->ptr;
        if (mounted->finddir) {
            return mounted->finddir(mounted, name);
        }
        return NULL;
    }
    
    // 正常查找
    if (!node->finddir) {
        return NULL;
    }
    return node->finddir(node, name);
}

// 路径解析：将路径字符串转换为文件节点
fs_node_t *vfs_path_to_node(const char *path) {
    if (!path || !fs_root) {
        return NULL;
    }
    
    // 处理根目录
    if (strcmp(path, "/") == 0) {
        return fs_root;
    }
    
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
        
        // 查找下一个节点
        current = vfs_finddir(current, token);
        if (!current) {
            return NULL;  // 路径不存在
        }
        
        // 检查是否是挂载点
        if (current->ptr != NULL) {
            // 这是一个挂载点，切换到挂载的文件系统
            current = (fs_node_t *)current->ptr;
        }
    }
    
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
        return -1;
    }
    
    // 调用父目录的 create 操作
    if (!parent->create) {
        return -1;
    }
    
    return parent->create(parent, file_name);
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
        return -1;
    }
    
    // 调用父目录的 mkdir 操作
    if (!parent->mkdir) {
        return -1;
    }
    
    return parent->mkdir(parent, dir_name, permissions);
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
        return -1;
    }
    
    // 调用父目录的 unlink 操作
    if (!parent->unlink) {
        return -1;
    }
    
    return parent->unlink(parent, file_name);
}

// 挂载文件系统到指定路径
int vfs_mount(const char *path, fs_node_t *root) {
    if (!path || !root || !fs_root) {
        return -1;
    }
    
    // 查找挂载点
    fs_node_t *mount_point = vfs_path_to_node(path);
    if (!mount_point) {
        LOG_ERROR_MSG("VFS: Mount point '%s' not found\n", path);
        return -1;
    }
    
    if (mount_point->type != FS_DIRECTORY) {
        LOG_ERROR_MSG("VFS: Mount point '%s' is not a directory\n", path);
        return -1;
    }
    
    // 检查是否已经挂载了文件系统
    if (mount_point->ptr != NULL) {
        LOG_ERROR_MSG("VFS: Mount point '%s' is already mounted\n", path);
        return -1;
    }
    
    // 设置挂载点
    mount_point->ptr = root;
    LOG_INFO_MSG("VFS: Filesystem mounted at '%s'\n", path);
    
    return 0;
}
