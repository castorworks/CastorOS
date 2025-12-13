/**
 * 文件系统相关系统调用实现
 * 
 * 实现 POSIX 标准的文件 I/O 系统调用：
 * - open(2), close(2), read(2), write(2), lseek(2)
 * - mkdir(2), unlink(2), chdir(2), getcwd(2)
 * - stat(2), fstat(2)
 */

#include <kernel/syscalls/fs.h>
#include <kernel/task.h>
#include <kernel/fd_table.h>
#include <fs/vfs.h>
#include <fs/pipe.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>

/* ============================================================================
 * stat/fstat 实现
 * ============================================================================ */

/**
 * fill_stat_from_node - 将 VFS 节点信息填充到 stat 结构体
 * @param node VFS 文件节点
 * @param buf  输出的 stat 结构体
 */
static void fill_stat_from_node(fs_node_t *node, struct stat *buf) {
    memset(buf, 0, sizeof(struct stat));
    
    buf->st_ino = node->inode;
    buf->st_size = node->size;
    buf->st_uid = node->uid;
    buf->st_gid = node->gid;
    buf->st_blksize = 512;
    buf->st_blocks = (node->size + 511) / 512;
    
    // 设置文件类型
    switch (node->type) {
        case FS_FILE:
            buf->st_mode = S_IFREG;
            break;
        case FS_DIRECTORY:
            buf->st_mode = S_IFDIR;
            break;
        case FS_CHARDEVICE:
            buf->st_mode = S_IFCHR;
            break;
        case FS_BLOCKDEVICE:
            buf->st_mode = S_IFBLK;
            break;
        case FS_PIPE:
            buf->st_mode = S_IFIFO;
            break;
        case FS_SYMLINK:
            buf->st_mode = S_IFLNK;
            break;
        default:
            buf->st_mode = S_IFREG;
    }
    
    // 添加权限位（从 VFS 节点的 permissions 字段）
    buf->st_mode |= (node->permissions & 0777);
    
    buf->st_nlink = 1;  // 简化实现，总是返回 1
    buf->st_dev = 0;    // 设备 ID（简化实现）
    buf->st_rdev = 0;   // 特殊设备类型
    buf->st_atime = 0;  // 时间戳（暂不支持）
    buf->st_mtime = 0;
    buf->st_ctime = 0;
}

/**
 * sys_stat - 获取文件状态信息
 */
uint32_t sys_stat(const char *path, struct stat *buf) {
    if (!path || !buf) {
        LOG_ERROR_MSG("sys_stat: invalid arguments (path=%p, buf=%p)\n", path, buf);
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("sys_stat: path='%s'\n", path);
    
    // 查找文件节点
    fs_node_t *node = vfs_path_to_node(path);
    if (!node) {
        LOG_ERROR_MSG("sys_stat: file '%s' not found\n", path);
        return (uint32_t)-1;
    }
    
    // 填充 stat 结构体
    fill_stat_from_node(node, buf);
    
    // 释放节点引用
    vfs_release_node(node);
    
    return 0;
}

/**
 * sys_fstat - 获取文件描述符状态信息
 */
uint32_t sys_fstat(int32_t fd, struct stat *buf) {
    if (!buf) {
        LOG_ERROR_MSG("sys_fstat: buf is NULL\n");
        return (uint32_t)-1;
    }
    
    task_t *current = task_get_current();
    if (!current || !current->fd_table) {
        LOG_ERROR_MSG("sys_fstat: no current task or fd_table\n");
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("sys_fstat: fd=%d\n", fd);
    
    // 获取文件描述符表项
    fd_entry_t *entry = fd_table_get(current->fd_table, fd);
    if (!entry || !entry->node) {
        LOG_ERROR_MSG("sys_fstat: invalid fd %d\n", fd);
        return (uint32_t)-1;
    }
    
    // 填充 stat 结构体
    fill_stat_from_node(entry->node, buf);
    
    return 0;
}

/**
 * sys_open - 打开或创建文件
 */
uint32_t sys_open(const char *path, int32_t flags, uint32_t mode) {
    (void)mode;
    if (!path) {
        LOG_ERROR_MSG("sys_open: path is NULL\n");
        return (uint32_t)-1;
    }
    
    task_t *current = task_get_current();
    if (!current || !current->fd_table) {
        LOG_ERROR_MSG("sys_open: no current task or fd_table\n");
        return (uint32_t)-1;
    }
    
    // 查找文件
    fs_node_t *node = vfs_path_to_node(path);
    
    // 如果文件不存在且指定了 O_CREAT，创建文件
    if (!node && (flags & O_CREAT)) {
        if (vfs_create(path) != 0) {
            LOG_ERROR_MSG("sys_open: failed to create file '%s'\n", path);
            return (uint32_t)-1;
        }
        node = vfs_path_to_node(path);
    }
    
    if (!node) {
        LOG_ERROR_MSG("sys_open: file '%s' not found\n", path);
        return (uint32_t)-1;
    }
    
    // 检查 O_EXCL 标志
    if ((flags & O_CREAT) && (flags & O_EXCL)) {
        LOG_ERROR_MSG("sys_open: file '%s' exists but O_EXCL specified\n", path);
        vfs_release_node(node);  // 释放节点，修复内存泄漏
        return (uint32_t)-1;
    }
    
    // 截断文件（如果指定了 O_TRUNC 且是写模式）
    if ((flags & O_TRUNC) && (flags & (O_WRONLY | O_RDWR))) {
        if (node->type == FS_FILE) {
            if (vfs_truncate(node, 0) != 0) {
                LOG_WARN_MSG("sys_open: failed to truncate file '%s'\n", path);
                // 继续，不视为致命错误
            }
        }
    }
    
    // 打开文件
    vfs_open(node, flags);
    
    // 分配文件描述符
    int32_t fd = fd_table_alloc(current->fd_table, node, flags);
    if (fd < 0) {
        LOG_ERROR_MSG("sys_open: failed to allocate fd for '%s'\n", path);
        vfs_close(node);
        vfs_release_node(node);  // 释放节点
        return (uint32_t)-1;
    }
    
    // 关键修复：释放 vfs_path_to_node 的初始引用
    // fd_table_alloc 已经增加了引用计数，现在 fd 持有唯一引用
    vfs_release_node(node);
    
    // 如果是追加模式，设置偏移量到文件末尾
    if (flags & O_APPEND) {
        fd_entry_t *entry = fd_table_get(current->fd_table, fd);
        if (entry) {
            entry->offset = node->size;
        }
    }
    
    return (uint32_t)fd;
}

/**
 * sys_close - 关闭文件描述符
 * 
 * 注意：vfs_close() 和 vfs_release_node() 由 fd_table_free() 统一处理
 * 避免双重 close 问题
 */
uint32_t sys_close(int32_t fd) {
    task_t *current = task_get_current();
    if (!current || !current->fd_table) {
        LOG_ERROR_MSG("sys_close: no current task or fd_table\n");
        return (uint32_t)-1;
    }
    
    // 释放文件描述符（fd_table_free 会处理 vfs_close 和 vfs_release_node）
    if (fd_table_free(current->fd_table, fd) != 0) {
        LOG_ERROR_MSG("sys_close: failed to free fd %d\n", fd);
        return (uint32_t)-1;
    }
    
    return 0;
}

/**
 * sys_read - 从文件描述符读取数据
 */
uint32_t sys_read(int32_t fd, void *buf, uint32_t count) {
    if (!buf) {
        LOG_ERROR_MSG("sys_read: buffer is NULL\n");
        return (uint32_t)-1;
    }
    
    task_t *current = task_get_current();
    if (!current || !current->fd_table) {
        LOG_ERROR_MSG("sys_read: no current task or fd_table\n");
        return (uint32_t)-1;
    }
    
    // 获取文件描述符表项
    fd_entry_t *entry = fd_table_get(current->fd_table, fd);
    if (!entry || !entry->node) {
        LOG_ERROR_MSG("sys_read: invalid fd %d\n", fd);
        return (uint32_t)-1;
    }
    
    // 检查读权限
    if ((entry->flags & O_WRONLY) && !(entry->flags & O_RDWR)) {
        LOG_ERROR_MSG("sys_read: fd %d is write-only\n", fd);
        return (uint32_t)-1;
    }
    
    // 读取数据
    uint32_t bytes_read = vfs_read(entry->node, entry->offset, count, (uint8_t *)buf);
    
    // 更新文件偏移量
    entry->offset += bytes_read;
    
    return bytes_read;
}

/**
 * sys_write - 向文件描述符写入数据
 */
uint32_t sys_write(int32_t fd, const void *buf, uint32_t count) {
    if (!buf) {
        LOG_ERROR_MSG("sys_write: buffer is NULL\n");
        return (uint32_t)-1;
    }
    
    task_t *current = task_get_current();
    if (!current || !current->fd_table) {
        LOG_ERROR_MSG("sys_write: no current task or fd_table\n");
        return (uint32_t)-1;
    }
    
    // 获取文件描述符表项
    fd_entry_t *entry = fd_table_get(current->fd_table, fd);
    if (!entry || !entry->node) {
        LOG_ERROR_MSG("sys_write: invalid fd %d\n", fd);
        return (uint32_t)-1;
    }
    
    // 检查写权限
    if ((entry->flags & O_RDONLY) && !(entry->flags & O_RDWR)) {
        LOG_ERROR_MSG("sys_write: fd %d is read-only\n", fd);
        return (uint32_t)-1;
    }
    
    // 如果是追加模式，设置偏移量到文件末尾
    if (entry->flags & O_APPEND) {
        entry->offset = entry->node->size;
    }
    
    // 重要：将用户空间缓冲区复制到内核空间
    // 避免在VFS/设备驱动中直接访问用户空间指针
    // 
    // 注意：不能在栈上分配大缓冲区（内核栈只有8KB）
    // 使用小缓冲区或动态分配
    #define MAX_WRITE_SIZE 512  // 每次最多写512字节（避免栈溢出）
    uint8_t kernel_buf[MAX_WRITE_SIZE];
    uint32_t total_written = 0;
    
    while (total_written < count) {
        uint32_t chunk_size = count - total_written;
        if (chunk_size > MAX_WRITE_SIZE) {
            chunk_size = MAX_WRITE_SIZE;
        }
        
        // 复制用户空间数据到内核缓冲区
        const uint8_t *user_ptr = (const uint8_t *)buf + total_written;
        for (uint32_t i = 0; i < chunk_size; i++) {
            kernel_buf[i] = user_ptr[i];
        }

        // 写入数据（使用内核缓冲区）
        uint32_t bytes_written = vfs_write(entry->node, entry->offset, chunk_size, kernel_buf);
        
        if (bytes_written == 0) {
            LOG_WARN_MSG("sys_write: vfs_write returned 0, breaking\n");
            break;  // 写入失败或已满
        }
        
        // 更新偏移量和计数
        entry->offset += bytes_written;
        total_written += bytes_written;
        
        if (bytes_written < chunk_size) {
            LOG_WARN_MSG("sys_write: partial write %u/%u, breaking\n", bytes_written, chunk_size);
            break;  // 部分写入，停止
        }
    }
    
    return total_written;
}

/**
 * sys_lseek - 移动文件指针
 */
uint32_t sys_lseek(int32_t fd, int32_t offset, int32_t whence) {
    task_t *current = task_get_current();
    if (!current || !current->fd_table) {
        LOG_ERROR_MSG("sys_lseek: no current task or fd_table\n");
        return (uint32_t)-1;
    }
    
    // 获取文件描述符表项
    fd_entry_t *entry = fd_table_get(current->fd_table, fd);
    if (!entry || !entry->node) {
        LOG_ERROR_MSG("sys_lseek: invalid fd %d\n", fd);
        return (uint32_t)-1;
    }
    
    uint32_t new_offset;
    
    switch (whence) {
        case SEEK_SET:
            new_offset = (uint32_t)offset;
            break;
        case SEEK_CUR:
            new_offset = entry->offset + offset;
            break;
        case SEEK_END:
            new_offset = entry->node->size + offset;
            break;
        default:
            LOG_ERROR_MSG("sys_lseek: invalid whence %d\n", whence);
            return (uint32_t)-1;
    }
    
    // 检查新位置是否有效（不能为负）
    if ((int32_t)new_offset < 0) {
        LOG_ERROR_MSG("sys_lseek: negative offset %d\n", (int32_t)new_offset);
        return (uint32_t)-1;
    }
    
    entry->offset = new_offset;
    
    return new_offset;
}

/**
 * sys_mkdir - 创建目录
 */
uint32_t sys_mkdir(const char *path, uint32_t mode) {
    if (!path) {
        LOG_ERROR_MSG("sys_mkdir: path is NULL\n");
        return (uint32_t)-1;
    }
    
    // 创建目录
    if (vfs_mkdir(path, mode) != 0) {
        LOG_ERROR_MSG("sys_mkdir: failed to create directory '%s'\n", path);
        return (uint32_t)-1;
    }
    
    return 0;
}

/**
 * sys_unlink - 删除文件或目录
 */
uint32_t sys_unlink(const char *path) {
    if (!path) {
        LOG_ERROR_MSG("sys_unlink: path is NULL\n");
        return (uint32_t)-1;
    }
    
    // 删除文件或目录
    if (vfs_unlink(path) != 0) {
        LOG_ERROR_MSG("sys_unlink: failed to unlink '%s'\n", path);
        return (uint32_t)-1;
    }
    
    return 0;
}

/**
 * 规范化路径，移除 . 和 .. 组件
 * @param path 输入路径
 * @param normalized 输出缓冲区
 * @param size 缓冲区大小
 * @return 0 成功，-1 失败
 */
static int normalize_path(const char *path, char *normalized, size_t size) {
    if (!path || !normalized || size == 0) {
        return -1;
    }
    
    // 路径组件栈（最多支持 64 层）
    const size_t MAX_COMPONENTS = 64;
    char components[MAX_COMPONENTS][128];
    size_t component_count = 0;
    
    const char *p = path;
    char current_component[128];
    size_t comp_len = 0;
    bool is_absolute = (path[0] == '/');
    
    // 解析路径组件
    while (*p != '\0') {
        // 跳过连续的 '/'
        while (*p == '/') {
            p++;
        }
        
        if (*p == '\0') {
            break;
        }
        
        // 提取一个组件
        comp_len = 0;
        while (*p != '\0' && *p != '/' && comp_len < 127) {
            current_component[comp_len++] = *p++;
        }
        current_component[comp_len] = '\0';
        
        // 处理组件
        if (comp_len == 0) {
            continue;  // 空组件，跳过
        } else if (strcmp(current_component, ".") == 0) {
            // 当前目录，忽略
            continue;
        } else if (strcmp(current_component, "..") == 0) {
            // 父目录
            if (component_count > 0) {
                // 有组件可以回退
                component_count--;
            }
            // 绝对路径中，.. 在根目录时保持在根目录（不添加组件）
        } else {
            // 普通组件，添加到栈中
            if (component_count >= MAX_COMPONENTS) {
                return -1;  // 路径太深
            }
            strncpy(components[component_count], current_component, 127);
            components[component_count][127] = '\0';
            component_count++;
        }
    }
    
    // 构建规范化路径
    size_t pos = 0;
    
    // 绝对路径以 '/' 开头
    if (is_absolute) {
        if (pos < size - 1) {
            normalized[pos++] = '/';
        }
    }
    
    // 添加所有组件
    for (size_t i = 0; i < component_count; i++) {
        size_t comp_len = strlen(components[i]);
        
        // 添加 '/'（除了第一个组件在绝对路径时）
        if (pos > 0 && normalized[pos - 1] != '/') {
            if (pos < size - 1) {
                normalized[pos++] = '/';
            }
        }
        
        // 添加组件名
        for (size_t j = 0; j < comp_len && pos < size - 1; j++) {
            normalized[pos++] = components[i][j];
        }
    }
    
    // 如果路径为空，至少要有 '/'
    if (pos == 0) {
        if (is_absolute) {
            normalized[pos++] = '/';
        } else {
            normalized[pos++] = '.';
        }
    }
    
    normalized[pos] = '\0';
    
    return 0;
}

/**
 * sys_chdir - 切换当前工作目录
 */
uint32_t sys_chdir(const char *path) {
    if (!path) {
        LOG_ERROR_MSG("sys_chdir: path is NULL\n");
        return (uint32_t)-1;
    }
    
    task_t *current = task_get_current();
    if (!current) {
        LOG_ERROR_MSG("sys_chdir: no current task\n");
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("sys_chdir: path='%s'\n", path);
    
    // 构建绝对路径：如果是相对路径，则相对于当前工作目录
    char abs_path[512];
    if (path[0] == '/') {
        // 绝对路径，直接使用
        if (strlen(path) >= sizeof(abs_path)) {
            LOG_ERROR_MSG("sys_chdir: path too long\n");
            return (uint32_t)-1;
        }
        strcpy(abs_path, path);
    } else {
        // 相对路径，相对于当前工作目录
        uint32_t cwd_len = strlen(current->cwd);
        uint32_t path_len = strlen(path);
        
        if (cwd_len + 1 + path_len >= sizeof(abs_path)) {
            LOG_ERROR_MSG("sys_chdir: path too long\n");
            return (uint32_t)-1;
        }
        
        strcpy(abs_path, current->cwd);
        // 如果当前工作目录不是根目录，添加 '/'
        if (cwd_len > 1 && abs_path[cwd_len - 1] != '/') {
            strcat(abs_path, "/");
        }
        // 追加路径
        strcat(abs_path, path);
    }
    
    LOG_DEBUG_MSG("sys_chdir: resolved path='%s'\n", abs_path);
    
    // 验证目标路径存在且为目录
    fs_node_t *node = vfs_path_to_node(abs_path);
    if (!node) {
        LOG_ERROR_MSG("sys_chdir: path '%s' not found\n", abs_path);
        return (uint32_t)-1;
    }
    
    if (node->type != FS_DIRECTORY) {
        LOG_ERROR_MSG("sys_chdir: '%s' is not a directory\n", abs_path);
        vfs_release_node(node);  // 释放节点
        return (uint32_t)-1;
    }
    
    // 节点已验证，释放它（我们只需要验证路径，不需要保留节点）
    vfs_release_node(node);
    
    // 规范化路径，移除 . 和 .. 组件
    char normalized_path[512];
    if (normalize_path(abs_path, normalized_path, sizeof(normalized_path)) != 0) {
        LOG_ERROR_MSG("sys_chdir: failed to normalize path\n");
        return (uint32_t)-1;
    }
    
    // 检查规范化后的路径长度
    uint32_t normalized_len = strlen(normalized_path);
    if (normalized_len >= sizeof(current->cwd)) {
        LOG_ERROR_MSG("sys_chdir: normalized path too long (%u >= %u)\n", normalized_len, (uint32_t)sizeof(current->cwd));
        return (uint32_t)-1;
    }
    
    // 更新当前工作目录（使用规范化后的路径）
    strcpy(current->cwd, normalized_path);
    
    LOG_DEBUG_MSG("sys_chdir: changed to '%s'\n", normalized_path);
    return 0;
}

/**
 * sys_getcwd - 获取当前工作目录
 */
uintptr_t sys_getcwd(char *buffer, size_t size) {
    if (!buffer) {
        LOG_ERROR_MSG("sys_getcwd: buffer is NULL\n");
        return (uintptr_t)-1;
    }
    
    task_t *current = task_get_current();
    if (!current) {
        LOG_ERROR_MSG("sys_getcwd: no current task\n");
        return (uintptr_t)-1;
    }
    
    LOG_DEBUG_MSG("sys_getcwd: size=%zu\n", size);
    
    size_t cwd_len = strlen(current->cwd);
    
    // 检查缓冲区大小
    if (size <= cwd_len) {
        LOG_ERROR_MSG("sys_getcwd: buffer too small (%zu <= %zu)\n", size, cwd_len);
        return (uintptr_t)-1;
    }
    
    // 复制当前工作目录到用户缓冲区
    strcpy(buffer, current->cwd);
    
    LOG_DEBUG_MSG("sys_getcwd: returned '%s'\n", current->cwd);
    return (uintptr_t)buffer;
}

/**
 * sys_ftruncate - 截断文件到指定大小
 */
uint32_t sys_ftruncate(int32_t fd, uint32_t length) {
    task_t *current = task_get_current();
    if (!current || !current->fd_table) {
        LOG_ERROR_MSG("sys_ftruncate: no current task or fd_table\n");
        return (uint32_t)-1;
    }
    
    // 获取文件描述符表项
    fd_entry_t *entry = fd_table_get(current->fd_table, fd);
    if (!entry || !entry->node) {
        LOG_ERROR_MSG("sys_ftruncate: invalid fd %d\n", fd);
        return (uint32_t)-1;
    }
    
    // 检查是否为文件
    if (entry->node->type != FS_FILE) {
        LOG_ERROR_MSG("sys_ftruncate: fd %d is not a regular file\n", fd);
        return (uint32_t)-1;
    }
    
    // 检查写权限
    if ((entry->flags & O_RDONLY) && !(entry->flags & O_RDWR)) {
        LOG_ERROR_MSG("sys_ftruncate: fd %d is read-only\n", fd);
        return (uint32_t)-1;
    }
    
    // 调用 VFS truncate
    if (vfs_truncate(entry->node, length) != 0) {
        LOG_ERROR_MSG("sys_ftruncate: failed to truncate fd %d to %u bytes\n", fd, length);
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("sys_ftruncate: fd %d truncated to %u bytes\n", fd, length);
    return 0;
}

/**
 * sys_getdents - 读取目录项（简化版本）
 * 
 * 注意：这是简化版本，与 Linux 标准 getdents 接口不同。
 * Linux 的 getdents 是批量读取多个目录项到缓冲区，而这里按索引读取单个目录项。
 */
uint32_t sys_getdents(int32_t fd, uint32_t index, void *dirent) {
    if (!dirent) {
        LOG_ERROR_MSG("sys_getdents: dirent is NULL\n");
        return (uint32_t)-1;
    }
    
    task_t *current = task_get_current();
    if (!current || !current->fd_table) {
        LOG_ERROR_MSG("sys_getdents: no current task or fd_table\n");
        return (uint32_t)-1;
    }
        
    // 获取文件描述符表项
    fd_entry_t *entry = fd_table_get(current->fd_table, fd);
    if (!entry || !entry->node) {
        LOG_ERROR_MSG("sys_getdents: invalid fd %d\n", fd);
        return (uint32_t)-1;
    }
    
    // 检查是否为目录
    if (entry->node->type != FS_DIRECTORY) {
        LOG_ERROR_MSG("sys_getdents: fd %d is not a directory\n", fd);
        return (uint32_t)-1;
    }
    
    // 读取目录项
    struct dirent *dir_entry = vfs_readdir(entry->node, index);
    if (!dir_entry) {
        return (uint32_t)-1;
    }
    
    // 复制目录项到用户空间
    // 注意：这里应该进行用户空间内存检查，简化实现直接复制
    memcpy(dirent, dir_entry, sizeof(struct dirent));

    return 0;
}

/* ============================================================================
 * 管道系统调用
 * ============================================================================ */

/**
 * sys_pipe - 创建管道
 * @fds: 用户空间数组，fds[0] 为读端，fds[1] 为写端
 */
uint32_t sys_pipe(int32_t *fds) {
    if (!fds) {
        LOG_ERROR_MSG("sys_pipe: fds is NULL\n");
        return (uint32_t)-1;
    }
    
    task_t *current = task_get_current();
    if (!current || !current->fd_table) {
        LOG_ERROR_MSG("sys_pipe: no current task or fd_table\n");
        return (uint32_t)-1;
    }
    
    // 创建管道
    fs_node_t *read_node = NULL;
    fs_node_t *write_node = NULL;
    
    if (pipe_create(&read_node, &write_node) != 0) {
        LOG_ERROR_MSG("sys_pipe: failed to create pipe\n");
        return (uint32_t)-1;
    }
    
    // 分配读端文件描述符
    int32_t read_fd = fd_table_alloc(current->fd_table, read_node, O_RDONLY);
    if (read_fd < 0) {
        LOG_ERROR_MSG("sys_pipe: failed to allocate read fd\n");
        vfs_release_node(read_node);
        vfs_release_node(write_node);
        return (uint32_t)-1;
    }
    
    // 释放 pipe_create 的初始引用（fd_table_alloc 已增加引用）
    vfs_release_node(read_node);
    
    // 分配写端文件描述符
    int32_t write_fd = fd_table_alloc(current->fd_table, write_node, O_WRONLY);
    if (write_fd < 0) {
        LOG_ERROR_MSG("sys_pipe: failed to allocate write fd\n");
        fd_table_free(current->fd_table, read_fd);
        vfs_release_node(write_node);
        return (uint32_t)-1;
    }
    
    // 释放 pipe_create 的初始引用
    vfs_release_node(write_node);
    
    // 设置返回值
    fds[0] = read_fd;
    fds[1] = write_fd;
    
    LOG_DEBUG_MSG("sys_pipe: created pipe (read_fd=%d, write_fd=%d)\n", read_fd, write_fd);
    
    return 0;
}

/* ============================================================================
 * 文件描述符复制系统调用
 * ============================================================================ */

/**
 * sys_dup - 复制文件描述符
 * @oldfd: 要复制的文件描述符
 */
uint32_t sys_dup(int32_t oldfd) {
    task_t *current = task_get_current();
    if (!current || !current->fd_table) {
        LOG_ERROR_MSG("sys_dup: no current task or fd_table\n");
        return (uint32_t)-1;
    }
    
    // 获取旧的文件描述符表项
    fd_entry_t *old_entry = fd_table_get(current->fd_table, oldfd);
    if (!old_entry || !old_entry->node) {
        LOG_ERROR_MSG("sys_dup: invalid fd %d\n", oldfd);
        return (uint32_t)-1;
    }
    
    // 分配新的文件描述符（fd_table_alloc 会自动增加引用计数）
    int32_t newfd = fd_table_alloc(current->fd_table, old_entry->node, old_entry->flags);
    if (newfd < 0) {
        LOG_ERROR_MSG("sys_dup: failed to allocate new fd\n");
        return (uint32_t)-1;
    }
    
    // 如果是管道，增加 readers/writers 计数
    if (old_entry->node->type == FS_PIPE) {
        pipe_on_dup(old_entry->node);
    }
    
    // 复制偏移量
    fd_entry_t *new_entry = fd_table_get(current->fd_table, newfd);
    if (new_entry) {
        new_entry->offset = old_entry->offset;
    }
    
    LOG_DEBUG_MSG("sys_dup: duplicated fd %d -> %d\n", oldfd, newfd);
    
    return (uint32_t)newfd;
}

/**
 * sys_dup2 - 复制文件描述符到指定编号
 * @oldfd: 要复制的文件描述符
 * @newfd: 目标文件描述符编号
 */
uint32_t sys_dup2(int32_t oldfd, int32_t newfd) {
    // 如果 oldfd 和 newfd 相同，直接返回
    if (oldfd == newfd) {
        // 验证 oldfd 有效
        task_t *current = task_get_current();
        if (!current || !current->fd_table) {
            return (uint32_t)-1;
        }
        fd_entry_t *entry = fd_table_get(current->fd_table, oldfd);
        if (!entry || !entry->node) {
            return (uint32_t)-1;
        }
        return (uint32_t)newfd;
    }
    
    task_t *current = task_get_current();
    if (!current || !current->fd_table) {
        LOG_ERROR_MSG("sys_dup2: no current task or fd_table\n");
        return (uint32_t)-1;
    }
    
    // 检查 newfd 范围
    if (newfd < 0 || newfd >= MAX_FDS) {
        LOG_ERROR_MSG("sys_dup2: newfd %d out of range\n", newfd);
        return (uint32_t)-1;
    }
    
    // 获取旧的文件描述符表项
    fd_entry_t *old_entry = fd_table_get(current->fd_table, oldfd);
    if (!old_entry || !old_entry->node) {
        LOG_ERROR_MSG("sys_dup2: invalid oldfd %d\n", oldfd);
        return (uint32_t)-1;
    }
    
    // 如果 newfd 已打开，先关闭它
    fd_entry_t *existing = fd_table_get(current->fd_table, newfd);
    if (existing && existing->in_use) {
        fd_table_free(current->fd_table, newfd);
    }
    
    // 手动设置新的文件描述符（直接操作表项，绕过 fd_table_alloc）
    spinlock_lock(&current->fd_table->lock);
    
    current->fd_table->entries[newfd].node = old_entry->node;
    current->fd_table->entries[newfd].offset = old_entry->offset;
    current->fd_table->entries[newfd].flags = old_entry->flags;
    current->fd_table->entries[newfd].in_use = true;
    
    // 增加引用计数
    vfs_ref_node(old_entry->node);
    
    // 如果是管道，增加 readers/writers 计数
    if (old_entry->node->type == FS_PIPE) {
        pipe_on_dup(old_entry->node);
    }
    
    spinlock_unlock(&current->fd_table->lock);
    
    LOG_DEBUG_MSG("sys_dup2: duplicated fd %d -> %d\n", oldfd, newfd);
    
    return (uint32_t)newfd;
}

/* ============================================================================
 * 文件重命名系统调用
 * ============================================================================ */

/**
 * sys_rename - 重命名文件或目录
 * @oldpath: 原路径
 * @newpath: 新路径
 */
uint32_t sys_rename(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) {
        LOG_ERROR_MSG("sys_rename: invalid arguments (oldpath=%p, newpath=%p)\n", 
                      oldpath, newpath);
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("sys_rename: '%s' -> '%s'\n", oldpath, newpath);
    
    // 调用 VFS 层的重命名函数
    if (vfs_rename(oldpath, newpath) != 0) {
        LOG_ERROR_MSG("sys_rename: failed to rename '%s' to '%s'\n", oldpath, newpath);
        return (uint32_t)-1;
    }
    
    return 0;
}