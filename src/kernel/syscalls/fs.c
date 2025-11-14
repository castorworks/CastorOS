/**
 * 文件系统相关系统调用实现
 * 
 * 实现 POSIX 标准的文件 I/O 系统调用：
 * - open(2), close(2), read(2), write(2), lseek(2)
 * - mkdir(2), unlink(2), chdir(2), getcwd(2)
 */

#include <kernel/syscalls/fs.h>
#include <kernel/task.h>
#include <kernel/fd_table.h>
#include <fs/vfs.h>
#include <lib/klog.h>
#include <lib/string.h>

/**
 * sys_open - 打开或创建文件
 */
uint32_t sys_open(const char *path, int32_t flags, uint32_t mode) {
    if (!path) {
        LOG_ERROR_MSG("sys_open: path is NULL\n");
        return (uint32_t)-1;
    }
    
    task_t *current = task_get_current();
    if (!current || !current->fd_table) {
        LOG_ERROR_MSG("sys_open: no current task or fd_table\n");
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("sys_open: path='%s', flags=0x%x, mode=0x%x\n", path, flags, mode);
    
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
        return (uint32_t)-1;
    }
    
    // 截断文件（如果指定了 O_TRUNC 且是写模式）
    if ((flags & O_TRUNC) && (flags & (O_WRONLY | O_RDWR))) {
        node->size = 0;
        // 这里应该调用文件系统特定的截断操作
        // 暂时只设置大小为0
    }
    
    // 打开文件
    vfs_open(node, flags);
    
    // 分配文件描述符
    int32_t fd = fd_table_alloc(current->fd_table, node, flags);
    if (fd < 0) {
        LOG_ERROR_MSG("sys_open: failed to allocate fd for '%s'\n", path);
        vfs_close(node);
        return (uint32_t)-1;
    }
    
    // 如果是追加模式，设置偏移量到文件末尾
    if (flags & O_APPEND) {
        fd_entry_t *entry = fd_table_get(current->fd_table, fd);
        if (entry) {
            entry->offset = node->size;
        }
    }
    
    LOG_DEBUG_MSG("sys_open: opened '%s' as fd %d\n", path, fd);
    return (uint32_t)fd;
}

/**
 * sys_close - 关闭文件描述符
 */
uint32_t sys_close(int32_t fd) {
    task_t *current = task_get_current();
    if (!current || !current->fd_table) {
        LOG_ERROR_MSG("sys_close: no current task or fd_table\n");
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("sys_close: fd=%d\n", fd);
    
    // 获取文件描述符表项
    fd_entry_t *entry = fd_table_get(current->fd_table, fd);
    if (!entry) {
        LOG_ERROR_MSG("sys_close: invalid fd %d\n", fd);
        return (uint32_t)-1;
    }
    
    // 关闭文件
    if (entry->node) {
        vfs_close(entry->node);
    }
    
    // 释放文件描述符
    if (fd_table_free(current->fd_table, fd) != 0) {
        LOG_ERROR_MSG("sys_close: failed to free fd %d\n", fd);
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("sys_close: closed fd %d\n", fd);
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
    
    // 写入数据
    uint32_t bytes_written = vfs_write(entry->node, entry->offset, count, (uint8_t *)buf);
    
    // 更新文件偏移量
    entry->offset += bytes_written;
    
    return bytes_written;
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
    
    LOG_DEBUG_MSG("sys_lseek: fd=%d, offset=%d, whence=%d\n", fd, offset, whence);
    
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
    
    LOG_DEBUG_MSG("sys_lseek: new offset %u for fd %d\n", new_offset, fd);
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
    
    LOG_DEBUG_MSG("sys_mkdir: path='%s', mode=0x%x\n", path, mode);
    
    // 创建目录
    if (vfs_mkdir(path, mode) != 0) {
        LOG_ERROR_MSG("sys_mkdir: failed to create directory '%s'\n", path);
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("sys_mkdir: created directory '%s'\n", path);
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
    
    LOG_DEBUG_MSG("sys_unlink: path='%s'\n", path);
    
    // 删除文件或目录
    if (vfs_unlink(path) != 0) {
        LOG_ERROR_MSG("sys_unlink: failed to unlink '%s'\n", path);
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("sys_unlink: unlinked '%s'\n", path);
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
    
    // 验证目标路径存在且为目录
    fs_node_t *node = vfs_path_to_node(path);
    if (!node) {
        LOG_ERROR_MSG("sys_chdir: path '%s' not found\n", path);
        return (uint32_t)-1;
    }
    
    if (node->type != FS_DIRECTORY) {
        LOG_ERROR_MSG("sys_chdir: '%s' is not a directory\n", path);
        return (uint32_t)-1;
    }
    
    // 检查路径长度
    uint32_t path_len = strlen(path);
    if (path_len >= sizeof(current->cwd)) {
        LOG_ERROR_MSG("sys_chdir: path too long (%u >= %u)\n", path_len, (uint32_t)sizeof(current->cwd));
        return (uint32_t)-1;
    }
    
    // 更新当前工作目录
    strcpy(current->cwd, path);
    
    LOG_DEBUG_MSG("sys_chdir: changed to '%s'\n", path);
    return 0;
}

/**
 * sys_getcwd - 获取当前工作目录
 */
uint32_t sys_getcwd(char *buffer, uint32_t size) {
    if (!buffer) {
        LOG_ERROR_MSG("sys_getcwd: buffer is NULL\n");
        return (uint32_t)-1;
    }
    
    task_t *current = task_get_current();
    if (!current) {
        LOG_ERROR_MSG("sys_getcwd: no current task\n");
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("sys_getcwd: size=%u\n", size);
    
    uint32_t cwd_len = strlen(current->cwd);
    
    // 检查缓冲区大小
    if (size <= cwd_len) {
        LOG_ERROR_MSG("sys_getcwd: buffer too small (%u <= %u)\n", size, cwd_len);
        return (uint32_t)-1;
    }
    
    // 复制当前工作目录到用户缓冲区
    strcpy(buffer, current->cwd);
    
    LOG_DEBUG_MSG("sys_getcwd: returned '%s'\n", current->cwd);
    return (uint32_t)buffer;
}

/**
 * sys_readdir - 读取目录项
 */
uint32_t sys_readdir(int32_t fd, uint32_t index, void *dirent) {
    if (!dirent) {
        LOG_ERROR_MSG("sys_readdir: dirent is NULL\n");
        return (uint32_t)-1;
    }
    
    task_t *current = task_get_current();
    if (!current || !current->fd_table) {
        LOG_ERROR_MSG("sys_readdir: no current task or fd_table\n");
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("sys_readdir: fd=%d, index=%u\n", fd, index);
    
    // 获取文件描述符表项
    fd_entry_t *entry = fd_table_get(current->fd_table, fd);
    if (!entry || !entry->node) {
        LOG_ERROR_MSG("sys_readdir: invalid fd %d\n", fd);
        return (uint32_t)-1;
    }
    
    // 检查是否为目录
    if (entry->node->type != FS_DIRECTORY) {
        LOG_ERROR_MSG("sys_readdir: fd %d is not a directory\n", fd);
        return (uint32_t)-1;
    }
    
    // 读取目录项
    struct dirent *dir_entry = vfs_readdir(entry->node, index);
    if (!dir_entry) {
        LOG_DEBUG_MSG("sys_readdir: no more entries at index %u\n", index);
        return (uint32_t)-1;
    }
    
    // 复制目录项到用户空间
    // 注意：这里应该进行用户空间内存检查，简化实现直接复制
    memcpy(dirent, dir_entry, sizeof(struct dirent));
    
    LOG_DEBUG_MSG("sys_readdir: returned entry '%s'\n", dir_entry->d_name);
    return 0;
}