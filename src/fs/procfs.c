// ============================================================================
// procfs.c - 进程文件系统实现
// ============================================================================

#include <fs/procfs.h>
#include <fs/vfs.h>
#include <kernel/task.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <stdarg.h>

/* 前向声明 */
static struct dirent *procfs_root_readdir(fs_node_t *node, uint32_t index);
static fs_node_t *procfs_root_finddir(fs_node_t *node, const char *name);
static struct dirent *procfs_pid_readdir(fs_node_t *node, uint32_t index);
static fs_node_t *procfs_pid_finddir(fs_node_t *node, const char *name);
static uint32_t procfs_status_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static uint32_t procfs_meminfo_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);

/* /proc 根目录节点 */
static fs_node_t *procfs_root = NULL;
static fs_node_t *procfs_meminfo_file = NULL;
static uint32_t procfs_meminfo_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    
    if (!buffer || size == 0) {
        return 0;
    }
    
    pmm_info_t info = pmm_get_info();
    uint32_t total_kb = (info.total_frames * PAGE_SIZE) / 1024;
    uint32_t free_kb = (info.free_frames * PAGE_SIZE) / 1024;
    uint32_t used_kb = (info.used_frames * PAGE_SIZE) / 1024;
    
    char meminfo_buf[256];
    int len = ksnprintf(meminfo_buf, sizeof(meminfo_buf),
                        "MemTotal:\t%u kB\n"
                        "MemFree:\t%u kB\n"
                        "MemUsed:\t%u kB\n"
                        "PageSize:\t%u bytes\n",
                        (unsigned int)total_kb,
                        (unsigned int)free_kb,
                        (unsigned int)used_kb,
                        (unsigned int)PAGE_SIZE);
    
    if (len < 0 || len >= (int)sizeof(meminfo_buf)) {
        len = sizeof(meminfo_buf) - 1;
    }
    
    uint32_t file_size = (uint32_t)len;
    if (offset >= file_size) {
        return 0;
    }
    
    uint32_t bytes_to_read = size;
    if (offset + bytes_to_read > file_size) {
        bytes_to_read = file_size - offset;
    }
    
    memcpy(buffer, meminfo_buf + offset, bytes_to_read);
    return bytes_to_read;
}


/* 进程目录节点缓存（动态分配） */
#define MAX_PROC_DIRS 256
static fs_node_t *proc_dirs[MAX_PROC_DIRS];
static uint32_t proc_dir_count = 0;

/* 进程状态文件节点缓存 */
static fs_node_t *proc_status_files[MAX_PROC_DIRS];

/**
 * 获取进程状态字符串
 */
static const char *get_task_state_string(task_state_t state) {
    switch (state) {
        case TASK_READY:      return "R";  // Running/Ready
        case TASK_RUNNING:   return "R";  // Running
        case TASK_BLOCKED:   return "S";  // Sleeping
        case TASK_TERMINATED: return "Z"; // Zombie
        default:             return "?";  // Unknown
    }
}

/**
 * 读取 /proc/[pid]/status 文件
 */
static uint32_t procfs_status_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (!node || !buffer || size == 0) {
        return 0;
    }
    
    // 从 impl 中获取 PID（存储为指针值）
    uint32_t pid = (uint32_t)node->impl;
    task_t *task = task_get_by_pid(pid);
    
    if (!task || task->state == TASK_UNUSED) {
        return 0;  // 进程不存在
    }
    
    // 生成状态信息字符串
    char status_buf[512];
    int len = ksnprintf(status_buf, sizeof(status_buf),
        "Name:\t%s\n"
        "State:\t%s\n"
        "Pid:\t%u\n"
        "PPid:\t%u\n"
        "Priority:\t%u\n"
        "Runtime:\t%llu ms\n",
        task->name,
        get_task_state_string(task->state),
        (unsigned int)task->pid,
        task->parent ? (unsigned int)task->parent->pid : 0,
        (unsigned int)task->priority,
        (unsigned long long)(task->total_runtime * 10));  // 转换为毫秒
    
    if (len < 0 || len >= (int)sizeof(status_buf)) {
        len = sizeof(status_buf) - 1;
    }
    
    uint32_t file_size = (uint32_t)len;
    
    // 检查偏移量
    if (offset >= file_size) {
        return 0;  // 超出文件范围
    }
    
    // 计算可读取的字节数
    uint32_t bytes_to_read = size;
    if (offset + bytes_to_read > file_size) {
        bytes_to_read = file_size - offset;
    }
    
    // 复制数据到缓冲区
    memcpy(buffer, status_buf + offset, bytes_to_read);
    
    return bytes_to_read;
}

/**
 * 读取 /proc/[pid] 目录
 */
static struct dirent *procfs_pid_readdir(fs_node_t *node, uint32_t index) {
    (void)node;
    
    static struct dirent dirent;
    
    /* 返回 . */
    if (index == 0) {
        strcpy(dirent.d_name, ".");
        dirent.d_ino = 0;
        dirent.d_reclen = sizeof(struct dirent);
        dirent.d_off = 1;
        dirent.d_type = DT_DIR;
        return &dirent;
    }
    
    /* 返回 .. */
    if (index == 1) {
        strcpy(dirent.d_name, "..");
        dirent.d_ino = 0;
        dirent.d_reclen = sizeof(struct dirent);
        dirent.d_off = 2;
        dirent.d_type = DT_DIR;
        return &dirent;
    }
    
    /* 返回 status 文件 */
    if (index == 2) {
        strcpy(dirent.d_name, "status");
        dirent.d_ino = 0;
        dirent.d_reclen = sizeof(struct dirent);
        dirent.d_off = 3;
        dirent.d_type = DT_REG;
        return &dirent;
    }
    
    return NULL;
}

/**
 * 在 /proc/[pid] 目录中查找文件
 */
static fs_node_t *procfs_pid_finddir(fs_node_t *node, const char *name) {
    if (!node || !name) {
        return NULL;
    }
    
    /* 处理 . 和 .. */
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return node;  // 返回当前目录节点
    }
    
    /* 查找 status 文件 */
    if (strcmp(name, "status") == 0) {
        uint32_t pid = (uint32_t)node->impl;
        
        // 查找或创建 status 文件节点
        for (uint32_t i = 0; i < proc_dir_count; i++) {
            if (proc_status_files[i] && (uint32_t)proc_status_files[i]->impl == pid) {
                return proc_status_files[i];
            }
        }
        
        // 创建新的 status 文件节点
        if (proc_dir_count < MAX_PROC_DIRS) {
            fs_node_t *status_file = (fs_node_t *)kmalloc(sizeof(fs_node_t));
            if (!status_file) {
                return NULL;
            }
            
            memset(status_file, 0, sizeof(fs_node_t));
            strcpy(status_file->name, "status");
            status_file->inode = 0;
            status_file->type = FS_FILE;
            status_file->size = 512;  // 估计大小
            status_file->permissions = FS_PERM_READ;
            status_file->impl = (uint32_t)pid;  // 存储 PID
            status_file->read = procfs_status_read;
            status_file->write = NULL;
            status_file->open = NULL;
            status_file->close = NULL;
            status_file->readdir = NULL;
            status_file->finddir = NULL;
            status_file->create = NULL;
            status_file->mkdir = NULL;
            status_file->unlink = NULL;
            status_file->ptr = NULL;
            
            proc_status_files[proc_dir_count] = status_file;
            return status_file;
        }
    }
    
    return NULL;
}

/**
 * 读取 /proc 根目录
 */
static struct dirent *procfs_root_readdir(fs_node_t *node, uint32_t index) {
    (void)node;
    
    static struct dirent dirent;
    
    /* 返回 . */
    if (index == 0) {
        strcpy(dirent.d_name, ".");
        dirent.d_ino = 0;
        dirent.d_reclen = sizeof(struct dirent);
        dirent.d_off = 1;
        dirent.d_type = DT_DIR;
        return &dirent;
    }
    
    /* 返回 .. */
    if (index == 1) {
        strcpy(dirent.d_name, "..");
        dirent.d_ino = 0;
        dirent.d_reclen = sizeof(struct dirent);
        dirent.d_off = 2;
        dirent.d_type = DT_DIR;
        return &dirent;
    }
    
    /* 返回 meminfo 文件 */
    if (index == 2) {
        strcpy(dirent.d_name, "meminfo");
        dirent.d_ino = 0;
        dirent.d_reclen = sizeof(struct dirent);
        dirent.d_off = 3;
        dirent.d_type = DT_REG;
        return &dirent;
    }
    
    /* 返回进程目录（PID 目录） */
    uint32_t pid_index = index - 3;
    
    // 遍历所有任务，找到第 pid_index 个有效进程
    uint32_t found_count = 0;
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        task_t *task = task_get_by_pid(i);
        if (task && task->state != TASK_UNUSED) {
            if (found_count == pid_index) {
                // 找到对应的进程，返回其 PID 目录名
                char pid_str[32];
                ksnprintf(pid_str, sizeof(pid_str), "%u", task->pid);
                strcpy(dirent.d_name, pid_str);
                dirent.d_ino = 0;
                dirent.d_reclen = sizeof(struct dirent);
                dirent.d_off = index + 1;
                dirent.d_type = DT_DIR;
                return &dirent;
            }
            found_count++;
        }
    }
    
    return NULL;
}

/**
 * 在 /proc 根目录中查找进程目录
 */
static fs_node_t *procfs_root_finddir(fs_node_t *node, const char *name) {
    (void)node;
    
    if (!name) {
        return NULL;
    }
    
    /* 处理 . 和 .. */
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return node;  // 返回当前目录节点（procfs_root）
    }
    
    /* meminfo 文件 */
    if (strcmp(name, "meminfo") == 0) {
        return procfs_meminfo_file;
    }
    
    /* 尝试解析为 PID */
    uint32_t pid = 0;
    const char *p = name;
    while (*p >= '0' && *p <= '9') {
        pid = pid * 10 + (*p - '0');
        p++;
    }
    
    // 如果解析成功且进程存在
    if (*p == '\0' && pid > 0) {
        task_t *task = task_get_by_pid(pid);
        if (task && task->state != TASK_UNUSED) {
            // 查找或创建进程目录节点
            for (uint32_t i = 0; i < proc_dir_count; i++) {
                if (proc_dirs[i] && (uint32_t)proc_dirs[i]->impl == pid) {
                    return proc_dirs[i];
                }
            }
            
            // 创建新的进程目录节点
            if (proc_dir_count < MAX_PROC_DIRS) {
                fs_node_t *pid_dir = (fs_node_t *)kmalloc(sizeof(fs_node_t));
                if (!pid_dir) {
                    return NULL;
                }
                
                memset(pid_dir, 0, sizeof(fs_node_t));
                ksnprintf(pid_dir->name, sizeof(pid_dir->name), "%u", pid);
                pid_dir->inode = 0;
                pid_dir->type = FS_DIRECTORY;
                pid_dir->size = 0;
                pid_dir->permissions = FS_PERM_READ | FS_PERM_EXEC;
                pid_dir->impl = (uint32_t)pid;  // 存储 PID
                pid_dir->read = NULL;
                pid_dir->write = NULL;
                pid_dir->open = NULL;
                pid_dir->close = NULL;
                pid_dir->readdir = procfs_pid_readdir;
                pid_dir->finddir = procfs_pid_finddir;
                pid_dir->create = NULL;
                pid_dir->mkdir = NULL;
                pid_dir->unlink = NULL;
                pid_dir->ptr = NULL;
                
                proc_dirs[proc_dir_count++] = pid_dir;
                return pid_dir;
            }
        }
    }
    
    return NULL;
}

/**
 * 初始化 procfs
 */
fs_node_t *procfs_init(void) {
    LOG_INFO_MSG("procfs: Initializing process filesystem...\n");
    
    // 清空缓存
    memset(proc_dirs, 0, sizeof(proc_dirs));
    memset(proc_status_files, 0, sizeof(proc_status_files));
    proc_dir_count = 0;
    
    // 创建 /proc 根目录节点
    procfs_root = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!procfs_root) {
        LOG_ERROR_MSG("procfs: Failed to allocate root node\n");
        return NULL;
    }
    
    memset(procfs_root, 0, sizeof(fs_node_t));
    strcpy(procfs_root->name, "proc");
    procfs_root->inode = 0;
    procfs_root->type = FS_DIRECTORY;
    procfs_root->size = 0;
    procfs_root->permissions = FS_PERM_READ | FS_PERM_EXEC;
    procfs_root->uid = 0;
    procfs_root->gid = 0;
    procfs_root->flags = 0;
    procfs_root->read = NULL;
    procfs_root->write = NULL;
    procfs_root->open = NULL;
    procfs_root->close = NULL;
    procfs_root->readdir = procfs_root_readdir;
    procfs_root->finddir = procfs_root_finddir;
    procfs_root->create = NULL;  // 不支持创建文件
    procfs_root->mkdir = NULL;   // 不支持创建目录
    procfs_root->unlink = NULL;  // 不支持删除
    procfs_root->ptr = NULL;
    
    /* 创建 meminfo 文件节点 */
    procfs_meminfo_file = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!procfs_meminfo_file) {
        LOG_ERROR_MSG("procfs: Failed to allocate meminfo node\n");
        return procfs_root;
    }
    
    memset(procfs_meminfo_file, 0, sizeof(fs_node_t));
    strcpy(procfs_meminfo_file->name, "meminfo");
    procfs_meminfo_file->inode = 0;
    procfs_meminfo_file->type = FS_FILE;
    procfs_meminfo_file->size = 256;
    procfs_meminfo_file->permissions = FS_PERM_READ;
    procfs_meminfo_file->read = procfs_meminfo_read;
    procfs_meminfo_file->write = NULL;
    procfs_meminfo_file->open = NULL;
    procfs_meminfo_file->close = NULL;
    procfs_meminfo_file->readdir = NULL;
    procfs_meminfo_file->finddir = NULL;
    procfs_meminfo_file->create = NULL;
    procfs_meminfo_file->mkdir = NULL;
    procfs_meminfo_file->unlink = NULL;
    procfs_meminfo_file->ptr = NULL;
    
    LOG_INFO_MSG("procfs: Initialized\n");
    
    return procfs_root;
}

