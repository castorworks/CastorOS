# 文件系统

## 概述

CastorOS 使用 VFS（Virtual File System）抽象层统一不同文件系统的接口。VFS 提供统一的文件操作 API，具体的文件系统实现作为插件注册到 VFS。

## VFS 架构

```
应用程序
    │
    │  open/read/write/close
    ↓
+-------------------+
|       VFS         |  ← 统一接口层
+-------------------+
    │
    ├─→ ramfs      (内存文件系统)
    ├─→ devfs      (设备文件系统)
    ├─→ procfs     (进程文件系统)
    ├─→ fat32      (FAT32 文件系统)
    └─→ shmfs      (共享内存文件系统)
```

## 核心数据结构

### Inode（索引节点）

```c
typedef struct inode {
    uint32_t inode_num;         // inode 号
    uint32_t mode;              // 文件类型和权限
    uint32_t size;              // 文件大小
    uint32_t uid, gid;          // 所有者
    uint32_t atime, mtime, ctime; // 时间戳
    uint32_t nlink;             // 硬链接计数
    uint32_t refcount;          // 引用计数
    
    void *private;              // 文件系统私有数据
    struct superblock *sb;      // 所属超级块
    struct inode_ops *ops;      // inode 操作
    struct file_ops *fops;      // 文件操作
    
    spinlock_t lock;            // 保护锁
} inode_t;

// 文件类型
#define S_IFMT      0170000     // 类型掩码
#define S_IFREG     0100000     // 普通文件
#define S_IFDIR     0040000     // 目录
#define S_IFCHR     0020000     // 字符设备
#define S_IFBLK     0060000     // 块设备
#define S_IFIFO     0010000     // 管道
#define S_IFLNK     0120000     // 符号链接
#define S_IFSOCK    0140000     // 套接字
```

### File（文件对象）

```c
typedef struct file {
    inode_t *inode;             // 关联的 inode
    uint32_t offset;            // 当前偏移
    uint32_t flags;             // 打开标志
    uint32_t refcount;          // 引用计数
    struct file_ops *ops;       // 文件操作
    void *private;              // 私有数据
} file_t;
```

### Superblock（超级块）

```c
typedef struct superblock {
    char name[32];              // 文件系统名
    uint32_t block_size;        // 块大小
    uint32_t total_blocks;      // 总块数
    uint32_t free_blocks;       // 空闲块数
    
    inode_t *root;              // 根目录 inode
    void *private;              // 文件系统私有数据
    
    struct fs_type *type;       // 文件系统类型
    struct superblock_ops *ops; // 超级块操作
} superblock_t;
```

### 目录项（Dentry）

```c
typedef struct dentry {
    char name[256];             // 文件名
    inode_t *inode;             // 关联的 inode
    struct dentry *parent;      // 父目录
    struct dentry *children;    // 子项链表
    struct dentry *sibling;     // 兄弟链表
    uint32_t refcount;          // 引用计数
} dentry_t;
```

## 操作接口

### Inode 操作

```c
typedef struct inode_ops {
    // 目录操作
    inode_t* (*lookup)(inode_t *dir, const char *name);
    int (*create)(inode_t *dir, const char *name, uint32_t mode);
    int (*mkdir)(inode_t *dir, const char *name, uint32_t mode);
    int (*rmdir)(inode_t *dir, const char *name);
    int (*unlink)(inode_t *dir, const char *name);
    int (*rename)(inode_t *old_dir, const char *old_name,
                  inode_t *new_dir, const char *new_name);
    
    // 符号链接
    int (*symlink)(inode_t *dir, const char *name, const char *target);
    int (*readlink)(inode_t *inode, char *buf, size_t size);
} inode_ops_t;
```

### 文件操作

```c
typedef struct file_ops {
    int (*open)(file_t *file);
    int (*close)(file_t *file);
    ssize_t (*read)(file_t *file, void *buf, size_t size);
    ssize_t (*write)(file_t *file, const void *buf, size_t size);
    off_t (*lseek)(file_t *file, off_t offset, int whence);
    int (*ioctl)(file_t *file, unsigned long cmd, void *arg);
    int (*readdir)(file_t *file, struct dirent *entry);
    int (*mmap)(file_t *file, void *addr, size_t len, int prot);
} file_ops_t;
```

## VFS 核心函数

### 路径解析

```c
inode_t *vfs_lookup(const char *path) {
    // 从根目录或当前目录开始
    inode_t *current = (path[0] == '/') ? root_inode : cwd_inode;
    inode_ref(current);
    
    // 逐级解析路径
    char *token = strtok(path_copy, "/");
    while (token) {
        if (!S_ISDIR(current->mode)) {
            inode_put(current);
            return NULL;
        }
        
        inode_t *next = current->ops->lookup(current, token);
        inode_put(current);
        
        if (!next) return NULL;
        current = next;
        
        token = strtok(NULL, "/");
    }
    
    return current;
}
```

### 打开文件

```c
int vfs_open(const char *path, int flags, mode_t mode) {
    // 解析路径
    inode_t *inode = vfs_lookup(path);
    
    // 处理文件创建
    if (!inode && (flags & O_CREAT)) {
        inode = vfs_create(path, mode);
    }
    
    if (!inode) return -ENOENT;
    
    // 分配文件描述符
    int fd = fd_alloc();
    if (fd < 0) {
        inode_put(inode);
        return -EMFILE;
    }
    
    // 创建文件对象
    file_t *file = file_alloc();
    file->inode = inode;
    file->flags = flags;
    file->offset = 0;
    file->ops = inode->fops;
    
    // 调用文件系统的 open
    if (file->ops->open) {
        int ret = file->ops->open(file);
        if (ret < 0) {
            file_free(file);
            inode_put(inode);
            return ret;
        }
    }
    
    // 关联到文件描述符
    current_task->fd_table[fd].file = file;
    current_task->fd_table[fd].valid = true;
    
    return fd;
}
```

### 读写文件

```c
ssize_t vfs_read(int fd, void *buf, size_t size) {
    file_t *file = get_file(fd);
    if (!file) return -EBADF;
    
    if (!file->ops->read) return -EINVAL;
    
    return file->ops->read(file, buf, size);
}

ssize_t vfs_write(int fd, const void *buf, size_t size) {
    file_t *file = get_file(fd);
    if (!file) return -EBADF;
    
    if (!file->ops->write) return -EINVAL;
    
    return file->ops->write(file, buf, size);
}
```

## 挂载系统

```c
typedef struct mount_point {
    char path[256];             // 挂载点路径
    superblock_t *sb;           // 超级块
    inode_t *mount_inode;       // 挂载点 inode
    struct mount_point *next;   // 链表
} mount_point_t;

int vfs_mount(const char *source, const char *target, 
              const char *fstype, unsigned long flags) {
    // 查找文件系统类型
    fs_type_t *type = find_fs_type(fstype);
    if (!type) return -ENODEV;
    
    // 创建超级块
    superblock_t *sb = type->mount(source, flags);
    if (!sb) return -EIO;
    
    // 查找挂载点
    inode_t *mount_point = vfs_lookup(target);
    if (!mount_point) return -ENOENT;
    
    // 创建挂载记录
    mount_point_t *mp = kmalloc(sizeof(mount_point_t));
    strcpy(mp->path, target);
    mp->sb = sb;
    mp->mount_inode = mount_point;
    
    // 添加到挂载列表
    add_mount_point(mp);
    
    return 0;
}
```

## 具体文件系统

### RAMFS（内存文件系统）

```c
// ramfs 节点
typedef struct ramfs_node {
    char name[256];
    uint32_t type;              // 文件/目录
    uint8_t *data;              // 文件内容
    size_t size;                // 文件大小
    size_t capacity;            // 分配容量
    struct ramfs_node *children;
    struct ramfs_node *sibling;
} ramfs_node_t;

// 读操作
ssize_t ramfs_read(file_t *file, void *buf, size_t size) {
    ramfs_node_t *node = file->inode->private;
    
    if (file->offset >= node->size) return 0;
    
    size_t to_read = min(size, node->size - file->offset);
    memcpy(buf, node->data + file->offset, to_read);
    file->offset += to_read;
    
    return to_read;
}

// 写操作
ssize_t ramfs_write(file_t *file, const void *buf, size_t size) {
    ramfs_node_t *node = file->inode->private;
    
    // 扩展容量
    if (file->offset + size > node->capacity) {
        size_t new_cap = (file->offset + size) * 2;
        node->data = krealloc(node->data, new_cap);
        node->capacity = new_cap;
    }
    
    memcpy(node->data + file->offset, buf, size);
    file->offset += size;
    
    if (file->offset > node->size) {
        node->size = file->offset;
    }
    
    return size;
}
```

### DevFS（设备文件系统）

```c
// 设备注册
int devfs_register(const char *name, int major, int minor, 
                   file_ops_t *ops) {
    devfs_node_t *node = kmalloc(sizeof(devfs_node_t));
    strcpy(node->name, name);
    node->major = major;
    node->minor = minor;
    node->ops = ops;
    
    // 创建 /dev/name
    inode_t *inode = devfs_create_inode(node);
    devfs_link(devfs_root, name, inode);
    
    return 0;
}

// 设备文件读写
ssize_t devfs_read(file_t *file, void *buf, size_t size) {
    devfs_node_t *dev = file->inode->private;
    if (dev->ops->read) {
        return dev->ops->read(file, buf, size);
    }
    return -EINVAL;
}
```

### ProcFS（进程文件系统）

```c
// /proc/<pid>/status
ssize_t procfs_read_status(file_t *file, void *buf, size_t size) {
    task_t *task = file->inode->private;
    char status[512];
    
    int len = snprintf(status, sizeof(status),
        "Name:\t%s\n"
        "State:\t%s\n"
        "Pid:\t%d\n"
        "PPid:\t%d\n"
        "Memory:\t%u KB\n",
        task->name,
        state_names[task->state],
        task->pid,
        task->parent ? task->parent->pid : 0,
        task_get_memory_usage(task) / 1024);
    
    return simple_read(buf, size, file->offset, status, len);
}
```

### FAT32

```c
// FAT32 引导扇区
typedef struct {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    // ... 更多字段
} __attribute__((packed)) fat32_bpb_t;

// 读取簇
int fat32_read_cluster(fat32_sb_t *sb, uint32_t cluster, void *buf) {
    uint32_t first_sector = sb->data_start + 
                           (cluster - 2) * sb->sectors_per_cluster;
    
    for (int i = 0; i < sb->sectors_per_cluster; i++) {
        ata_read_sector(sb->device, first_sector + i,
                       buf + i * sb->bytes_per_sector);
    }
    
    return 0;
}

// 获取下一个簇
uint32_t fat32_next_cluster(fat32_sb_t *sb, uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = sb->fat_start + (fat_offset / sb->bytes_per_sector);
    uint32_t offset = fat_offset % sb->bytes_per_sector;
    
    uint8_t sector[512];
    ata_read_sector(sb->device, fat_sector, sector);
    
    uint32_t next = *(uint32_t*)(sector + offset);
    next &= 0x0FFFFFFF;  // FAT32 使用 28 位
    
    if (next >= 0x0FFFFFF8) return 0;  // 簇链结束
    return next;
}
```

## 文件描述符表

```c
typedef struct {
    file_t *file;
    bool valid;
    int flags;
} fd_entry_t;

// 每个进程的文件描述符表
#define MAX_FDS 256

int fd_alloc(void) {
    task_t *task = current_task;
    for (int i = 0; i < MAX_FDS; i++) {
        if (!task->fd_table[i].valid) {
            task->fd_table[i].valid = true;
            return i;
        }
    }
    return -EMFILE;
}

void fd_free(int fd) {
    task_t *task = current_task;
    if (fd >= 0 && fd < MAX_FDS) {
        task->fd_table[fd].valid = false;
        task->fd_table[fd].file = NULL;
    }
}

// fork 时复制文件描述符表
void fd_table_clone(task_t *parent, task_t *child) {
    for (int i = 0; i < MAX_FDS; i++) {
        if (parent->fd_table[i].valid) {
            child->fd_table[i] = parent->fd_table[i];
            file_ref(child->fd_table[i].file);
        }
    }
}
```

## 管道（Pipe）

```c
typedef struct pipe {
    char buffer[PIPE_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
    
    int readers;
    int writers;
    
    spinlock_t lock;
    task_t *read_waiters;
    task_t *write_waiters;
} pipe_t;

ssize_t pipe_read(file_t *file, void *buf, size_t size) {
    pipe_t *pipe = file->inode->private;
    
    spinlock_lock(&pipe->lock);
    
    while (pipe->count == 0) {
        if (pipe->writers == 0) {
            spinlock_unlock(&pipe->lock);
            return 0;  // EOF
        }
        // 等待数据
        wait_on(&pipe->read_waiters, &pipe->lock);
    }
    
    size_t to_read = min(size, pipe->count);
    for (size_t i = 0; i < to_read; i++) {
        ((char*)buf)[i] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % PIPE_SIZE;
    }
    pipe->count -= to_read;
    
    wake_up(&pipe->write_waiters);
    spinlock_unlock(&pipe->lock);
    
    return to_read;
}
```

## 最佳实践

1. **引用计数**: inode 和 file 都使用引用计数，防止提前释放
2. **锁粒度**: 使用细粒度锁提高并发性
3. **缓存**: 使用页缓存加速文件访问
4. **延迟写入**: 写操作先写入缓存，定期同步到磁盘
5. **路径缓存**: 使用 dentry 缓存加速路径解析

