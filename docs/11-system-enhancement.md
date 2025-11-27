# 阶段 11: 系统增强

本文档详细描述 CastorOS 后续功能增强的实现方案。

---

## 目录

- [优先级高：完善核心功能](#优先级高完善核心功能)
  - [SYS_GETPPID](#1-sys_getppid---获取父进程-pid)
  - [SYS_STAT/FSTAT](#2-sys_statfstat---获取文件状态信息)
  - [SYS_BRK](#3-sys_brk---堆内存管理)
  - [FAT32 写入支持](#4-fat32-写入支持)
  - [信号机制](#5-信号机制)
- [优先级中：增强系统能力](#优先级中增强系统能力)
  - [SYS_PIPE](#6-sys_pipe---管道)
  - [SYS_DUP/DUP2](#7-sys_dupdup2---文件描述符复制)
  - [SYS_MMAP/MUNMAP](#8-sys_mmapmunmap---内存映射)
  - [SYS_UNAME](#9-sys_uname---系统信息查询)
  - [SYS_RENAME](#10-sys_rename---文件重命名)
  - [SHMFS](#11-shmfs---共享内存文件系统)
  - [RTC 驱动](#12-rtc-驱动---实时时钟)

---

# 优先级高：完善核心功能

## 1. SYS_GETPPID - 获取父进程 PID

### 1.1 功能描述

返回当前进程的父进程 PID。如果父进程不存在（如 init 进程或孤儿进程），返回 0 或 1。

### 1.2 实现方案

#### 需要修改的文件

| 文件 | 修改内容 |
|------|----------|
| `src/include/kernel/syscall.h` | 系统调用号已定义（`SYS_GETPPID = 0x0005`） |
| `src/include/kernel/syscalls/process.h` | 添加函数声明 |
| `src/kernel/syscalls/process.c` | 实现 `sys_getppid()` |
| `src/kernel/syscall.c` | 注册系统调用 |
| `userland/lib/include/syscall.h` | 添加用户态封装 |
| `userland/lib/src/syscall.c` | 实现用户态封装 |

#### 内核实现

```c
// src/kernel/syscalls/process.c

/**
 * sys_getppid - 获取父进程 PID
 * @return 父进程 PID，如果没有父进程返回 0
 */
uint32_t sys_getppid(void) {
    task_t *current = task_get_current();
    if (!current) {
        LOG_ERROR_MSG("sys_getppid: no current task\n");
        return 0;
    }
    
    if (current->parent && current->parent->state != TASK_UNUSED) {
        return current->parent->pid;
    }
    
    // 没有父进程（如 init 进程或孤儿进程）
    return 0;
}
```

#### 注册系统调用

```c
// src/kernel/syscall.c

static uint32_t sys_getppid_wrapper(uint32_t *frame, uint32_t p1, uint32_t p2,
                                    uint32_t p3, uint32_t p4, uint32_t p5) {
    (void)frame; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_getppid();
}

// 在 syscall_init() 中添加
syscall_table[SYS_GETPPID] = sys_getppid_wrapper;
```

#### 用户态封装

```c
// userland/lib/src/syscall.c

int getppid(void) {
    return (int)syscall0(SYS_GETPPID);
}
```

### 1.3 测试用例

```c
// 在 shell 中添加测试命令或编写用户程序
int main() {
    printf("PID: %d, PPID: %d\n", getpid(), getppid());
    
    int pid = fork();
    if (pid == 0) {
        // 子进程
        printf("Child PID: %d, Parent PID: %d\n", getpid(), getppid());
        exit(0);
    }
    wait(NULL);
    return 0;
}
```

---

## 2. SYS_STAT/FSTAT - 获取文件状态信息

### 2.1 功能描述

- `stat(path, buf)`: 获取指定路径文件的状态信息
- `fstat(fd, buf)`: 获取文件描述符对应文件的状态信息

### 2.2 数据结构

#### stat 结构体定义

```c
// src/include/types.h 或 新建 src/include/kernel/stat.h

struct stat {
    uint32_t st_dev;      // 设备 ID
    uint32_t st_ino;      // inode 编号
    uint32_t st_mode;     // 文件类型和权限
    uint32_t st_nlink;    // 硬链接数
    uint32_t st_uid;      // 所有者用户 ID
    uint32_t st_gid;      // 所有者组 ID
    uint32_t st_rdev;     // 设备类型（如果是特殊文件）
    uint32_t st_size;     // 文件大小（字节）
    uint32_t st_blksize;  // 文件系统 I/O 块大小
    uint32_t st_blocks;   // 分配的 512B 块数
    uint32_t st_atime;    // 最后访问时间
    uint32_t st_mtime;    // 最后修改时间
    uint32_t st_ctime;    // 最后状态改变时间
};

// 文件类型掩码
#define S_IFMT   0170000   // 文件类型掩码
#define S_IFREG  0100000   // 普通文件
#define S_IFDIR  0040000   // 目录
#define S_IFCHR  0020000   // 字符设备
#define S_IFBLK  0060000   // 块设备
#define S_IFIFO  0010000   // FIFO（管道）
#define S_IFLNK  0120000   // 符号链接

// 权限位
#define S_IRUSR  0400      // 所有者读
#define S_IWUSR  0200      // 所有者写
#define S_IXUSR  0100      // 所有者执行

// 类型检查宏
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
```

### 2.3 实现方案

#### 需要修改的文件

| 文件 | 修改内容 |
|------|----------|
| `src/include/types.h` | 添加 `struct stat` 定义 |
| `src/include/kernel/syscalls/fs.h` | 添加函数声明 |
| `src/kernel/syscalls/fs.c` | 实现 `sys_stat()` 和 `sys_fstat()` |
| `src/kernel/syscall.c` | 注册系统调用 |
| `userland/lib/include/syscall.h` | 添加用户态声明 |

#### 内核实现

```c
// src/kernel/syscalls/fs.c

/**
 * 将 VFS 节点信息填充到 stat 结构体
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
    
    // 添加权限位
    buf->st_mode |= (node->permissions & 0777);
    
    buf->st_nlink = 1;  // 简化实现，总是返回 1
}

/**
 * sys_stat - 获取文件状态信息
 */
uint32_t sys_stat(const char *path, struct stat *buf) {
    if (!path || !buf) {
        return (uint32_t)-1;
    }
    
    fs_node_t *node = vfs_path_to_node(path);
    if (!node) {
        LOG_ERROR_MSG("sys_stat: file '%s' not found\n", path);
        return (uint32_t)-1;
    }
    
    fill_stat_from_node(node, buf);
    vfs_release_node(node);
    
    return 0;
}

/**
 * sys_fstat - 获取文件描述符状态信息
 */
uint32_t sys_fstat(int32_t fd, struct stat *buf) {
    if (!buf) {
        return (uint32_t)-1;
    }
    
    task_t *current = task_get_current();
    if (!current || !current->fd_table) {
        return (uint32_t)-1;
    }
    
    fd_entry_t *entry = fd_table_get(current->fd_table, fd);
    if (!entry || !entry->node) {
        LOG_ERROR_MSG("sys_fstat: invalid fd %d\n", fd);
        return (uint32_t)-1;
    }
    
    fill_stat_from_node(entry->node, buf);
    
    return 0;
}
```

### 2.4 用户态封装

```c
// userland/lib/src/syscall.c

int stat(const char *path, struct stat *buf) {
    return (int)syscall2(SYS_STAT, (uint32_t)path, (uint32_t)buf);
}

int fstat(int fd, struct stat *buf) {
    return (int)syscall2(SYS_FSTAT, (uint32_t)fd, (uint32_t)buf);
}
```

### 2.5 测试用例

```c
// 在 shell 中可以添加 stat 命令
void cmd_stat(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        printf("Error: cannot stat '%s'\n", path);
        return;
    }
    
    printf("  File: %s\n", path);
    printf("  Size: %u bytes\n", st.st_size);
    printf("  Type: ");
    if (S_ISDIR(st.st_mode)) printf("directory\n");
    else if (S_ISREG(st.st_mode)) printf("regular file\n");
    else if (S_ISCHR(st.st_mode)) printf("character device\n");
    else printf("unknown\n");
    printf("  Inode: %u\n", st.st_ino);
}
```

---

## 3. SYS_BRK - 堆内存管理

### 3.1 功能描述

`brk(addr)` 系统调用用于调整进程的数据段（堆）边界。这是用户空间 `malloc()` 实现的基础。

- 如果 `addr` 为 0，返回当前堆结束地址
- 如果 `addr` 有效，扩展或收缩堆到该地址

### 3.2 设计方案

#### 用户空间内存布局

```
0x00000000  +------------------+
            |   NULL Page      |  <- 不可访问，用于捕获空指针
0x00001000  +------------------+
            |   .text (代码)   |
            |   .rodata        |
            |   .data          |
            |   .bss           |
            +------------------+
            |   Heap ↓         |  <- brk 管理的区域
            |                  |
            |   (可扩展空间)   |
            |                  |
            |   Stack ↑        |
0x7FFFFFFF  +------------------+
```

#### 进程控制块扩展

```c
// src/include/kernel/task.h

typedef struct task {
    // ... 现有字段 ...
    
    /* 堆管理 */
    uint32_t heap_start;         // 堆起始地址（初始 brk）
    uint32_t heap_end;           // 当前堆结束地址（当前 brk）
    uint32_t heap_max;           // 堆最大地址（防止与栈冲突）
    
    // ... 其他字段 ...
} task_t;
```

### 3.3 实现方案

#### 需要修改的文件

| 文件 | 修改内容 |
|------|----------|
| `src/include/kernel/task.h` | 添加堆管理字段 |
| `src/kernel/task.c` | 初始化堆字段 |
| `src/include/kernel/syscalls/mm.h` | 新建，声明内存管理系统调用 |
| `src/kernel/syscalls/mm.c` | 新建，实现 `sys_brk()` |
| `src/kernel/syscall.c` | 注册系统调用 |

#### 内核实现

```c
// src/kernel/syscalls/mm.c

#include <kernel/task.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <lib/klog.h>

/**
 * sys_brk - 调整堆边界
 * @param addr 新的堆结束地址（0 表示查询当前值）
 * @return 成功返回新的堆结束地址，失败返回 (uint32_t)-1
 */
uint32_t sys_brk(uint32_t addr) {
    task_t *current = task_get_current();
    if (!current) {
        return (uint32_t)-1;
    }
    
    // 如果 addr 为 0，返回当前堆结束地址
    if (addr == 0) {
        return current->heap_end;
    }
    
    // 验证地址范围
    if (addr < current->heap_start) {
        LOG_ERROR_MSG("sys_brk: addr %x below heap_start %x\n", 
                      addr, current->heap_start);
        return (uint32_t)-1;
    }
    
    if (addr > current->heap_max) {
        LOG_ERROR_MSG("sys_brk: addr %x exceeds heap_max %x\n", 
                      addr, current->heap_max);
        return (uint32_t)-1;
    }
    
    uint32_t old_end = current->heap_end;
    uint32_t new_end = PAGE_ALIGN_UP(addr);
    
    if (new_end > old_end) {
        // 扩展堆：分配并映射新页面
        for (uint32_t page = PAGE_ALIGN_UP(old_end); 
             page < new_end; 
             page += PAGE_SIZE) {
            
            // 分配物理页
            uint32_t phys = pmm_alloc_frame();
            if (!phys) {
                LOG_ERROR_MSG("sys_brk: out of memory\n");
                return (uint32_t)-1;
            }
            
            // 映射到用户空间（可读写）
            if (!vmm_map_page_in_directory(current->page_dir_phys, page, phys,
                                           PAGE_PRESENT | PAGE_WRITE | PAGE_USER)) {
                pmm_free_frame(phys);
                LOG_ERROR_MSG("sys_brk: failed to map page %x\n", page);
                return (uint32_t)-1;
            }
            
            // 清零新页面
            memset((void *)page, 0, PAGE_SIZE);
        }
    } else if (new_end < old_end) {
        // 收缩堆：取消映射并释放页面
        for (uint32_t page = new_end; 
             page < PAGE_ALIGN_UP(old_end); 
             page += PAGE_SIZE) {
            
            uint32_t phys = vmm_unmap_page_in_directory(current->page_dir_phys, page);
            if (phys) {
                pmm_free_frame(phys);
            }
        }
    }
    
    current->heap_end = addr;
    
    LOG_DEBUG_MSG("sys_brk: heap extended from %x to %x\n", old_end, addr);
    
    return addr;
}
```

#### 进程初始化时设置堆

```c
// src/kernel/loader.c 或 task.c

// 在加载 ELF 后设置堆
void setup_user_heap(task_t *task, uint32_t program_end) {
    // 堆从程序结束后的下一页开始
    task->heap_start = PAGE_ALIGN_UP(program_end);
    task->heap_end = task->heap_start;
    
    // 堆最大值：留出 8MB 给栈
    task->heap_max = task->user_stack_base - (8 * 1024 * 1024);
}
```

### 3.4 用户态 malloc 实现

```c
// userland/lib/src/malloc.c

static uint32_t heap_start = 0;
static uint32_t heap_end = 0;

void *sbrk(int increment) {
    if (heap_end == 0) {
        // 首次调用，获取当前堆位置
        heap_end = syscall1(SYS_BRK, 0);
        heap_start = heap_end;
    }
    
    uint32_t old_end = heap_end;
    uint32_t new_end = heap_end + increment;
    
    if (increment > 0) {
        uint32_t result = syscall1(SYS_BRK, new_end);
        if (result == (uint32_t)-1) {
            return (void *)-1;  // 失败
        }
        heap_end = result;
    } else if (increment < 0) {
        uint32_t result = syscall1(SYS_BRK, new_end);
        if (result != (uint32_t)-1) {
            heap_end = new_end;
        }
    }
    
    return (void *)old_end;
}

// 简单的 malloc 实现（可以后续实现更复杂的分配器）
void *malloc(size_t size) {
    if (size == 0) return NULL;
    
    // 简单实现：每次 sbrk 分配
    // 实际应该实现空闲链表等数据结构
    size = (size + 7) & ~7;  // 8 字节对齐
    return sbrk(size);
}

void free(void *ptr) {
    // 简单实现暂不回收
    // 完整实现需要空闲链表管理
    (void)ptr;
}
```

---

## 4. FAT32 写入支持

### 4.1 功能描述

为 FAT32 文件系统添加写入功能：
- 创建文件/目录
- 写入文件内容
- 删除文件/目录
- 修改文件大小

### 4.2 当前实现状态

已实现的功能（在 `src/fs/fat32.c` 中）：
- ✅ FAT 表读取/写入
- ✅ 簇分配/释放
- ✅ 目录项创建
- ✅ 文件创建（`fat32_dir_create`）
- ✅ 目录创建（`fat32_dir_mkdir`）
- ✅ 文件/目录删除（`fat32_dir_unlink`）
- ⚠️ 文件写入（需要完善）

### 4.3 需要完善的部分

#### 4.3.1 文件写入实现

```c
// src/fs/fat32.c

/**
 * fat32_file_write - 写入文件数据
 * @param node 文件节点
 * @param offset 写入偏移量
 * @param size 写入大小
 * @param buffer 数据缓冲区
 * @return 实际写入的字节数
 */
static uint32_t fat32_file_write(fs_node_t *node, uint32_t offset, 
                                  uint32_t size, uint8_t *buffer) {
    if (!node || !node->impl || !buffer) {
        return 0;
    }
    
    fat32_file_t *file = (fat32_file_t *)node->impl;
    fat32_fs_t *fs = file->fs;
    
    if (file->is_dir) {
        LOG_ERROR_MSG("fat32: Cannot write to directory\n");
        return 0;
    }
    
    mutex_lock(&fs->fs_lock);
    
    uint32_t bytes_written = 0;
    uint32_t cluster_size = fs->bytes_per_cluster;
    
    // 如果文件为空，需要分配第一个簇
    if (file->start_cluster == 0 && size > 0) {
        uint32_t new_cluster = fat32_alloc_cluster(fs);
        if (new_cluster == 0) {
            mutex_unlock(&fs->fs_lock);
            return 0;
        }
        file->start_cluster = new_cluster;
        
        // 更新目录项
        fat32_update_dirent(fs, file, 0, new_cluster);
    }
    
    // 扩展文件（如果需要）
    uint32_t required_size = offset + size;
    if (required_size > file->size) {
        // 计算需要的簇数
        uint32_t required_clusters = (required_size + cluster_size - 1) / cluster_size;
        uint32_t current_clusters = file->size > 0 ? 
            (file->size + cluster_size - 1) / cluster_size : 0;
        
        // 分配额外的簇
        if (required_clusters > current_clusters) {
            if (!fat32_extend_cluster_chain(fs, file->start_cluster, 
                                            required_clusters - current_clusters)) {
                mutex_unlock(&fs->fs_lock);
                return 0;
            }
        }
    }
    
    // 定位到写入位置
    uint32_t cluster_offset = offset / cluster_size;
    uint32_t byte_offset = offset % cluster_size;
    uint32_t current_cluster = file->start_cluster;
    
    // 跳过前面的簇
    for (uint32_t i = 0; i < cluster_offset && current_cluster >= 2; i++) {
        current_cluster = fat32_read_fat_entry(fs, current_cluster);
    }
    
    // 分配临时缓冲区
    uint8_t *cluster_buf = kmalloc(cluster_size);
    if (!cluster_buf) {
        mutex_unlock(&fs->fs_lock);
        return 0;
    }
    
    // 写入数据
    while (bytes_written < size && current_cluster >= 2 && 
           current_cluster < FAT32_CLUSTER_EOF_MIN) {
        
        // 计算本次写入量
        uint32_t write_size = cluster_size - byte_offset;
        if (write_size > size - bytes_written) {
            write_size = size - bytes_written;
        }
        
        // 如果不是完整簇写入，先读取原有数据
        if (byte_offset != 0 || write_size != cluster_size) {
            fat32_read_cluster(fs, current_cluster, cluster_buf);
        }
        
        // 复制数据到缓冲区
        memcpy(cluster_buf + byte_offset, buffer + bytes_written, write_size);
        
        // 写入簇
        if (fat32_write_cluster(fs, current_cluster, cluster_buf) != 0) {
            break;
        }
        
        bytes_written += write_size;
        byte_offset = 0;  // 后续簇从头开始
        
        // 移动到下一个簇
        current_cluster = fat32_read_fat_entry(fs, current_cluster);
    }
    
    kfree(cluster_buf);
    
    // 更新文件大小
    if (offset + bytes_written > file->size) {
        file->size = offset + bytes_written;
        node->size = file->size;
        fat32_update_dirent_size(fs, file, file->size);
    }
    
    mutex_unlock(&fs->fs_lock);
    
    return bytes_written;
}
```

#### 4.3.2 辅助函数

```c
/**
 * 写入簇数据
 */
static int fat32_write_cluster(fat32_fs_t *fs, uint32_t cluster, uint8_t *buffer) {
    uint32_t sector = fat32_cluster_to_sector(fs, cluster);
    return blockdev_write(fs->dev, sector, fs->bpb.sectors_per_cluster, buffer);
}

/**
 * 扩展簇链
 */
static bool fat32_extend_cluster_chain(fat32_fs_t *fs, uint32_t start_cluster, 
                                        uint32_t count) {
    // 找到链的最后一个簇
    uint32_t last_cluster = start_cluster;
    uint32_t next = fat32_read_fat_entry(fs, last_cluster);
    
    while (next >= 2 && next < FAT32_CLUSTER_EOF_MIN) {
        last_cluster = next;
        next = fat32_read_fat_entry(fs, last_cluster);
    }
    
    // 分配新簇
    for (uint32_t i = 0; i < count; i++) {
        uint32_t new_cluster = fat32_alloc_cluster(fs);
        if (new_cluster == 0) {
            return false;
        }
        
        // 链接到前一个簇
        fat32_write_fat_entry(fs, last_cluster, new_cluster);
        last_cluster = new_cluster;
    }
    
    return true;
}

/**
 * 更新目录项中的文件大小
 */
static int fat32_update_dirent_size(fat32_fs_t *fs, fat32_file_t *file, 
                                     uint32_t new_size) {
    // 读取目录项所在簇
    uint8_t *cluster_buf = kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) return -1;
    
    if (fat32_read_cluster(fs, file->dirent_cluster, cluster_buf) != 0) {
        kfree(cluster_buf);
        return -1;
    }
    
    // 更新目录项
    fat32_dirent_t *dirent = (fat32_dirent_t *)(cluster_buf + file->dirent_offset);
    dirent->file_size = new_size;
    
    // 写回
    int result = fat32_write_cluster(fs, file->dirent_cluster, cluster_buf);
    kfree(cluster_buf);
    
    return result;
}
```

### 4.4 VFS 集成

确保 VFS 的 `write` 操作能正确调用 FAT32 的写入函数：

```c
// 在创建 FAT32 节点时设置写入函数
fs_node_t *fat32_create_node(fat32_fs_t *fs, fat32_dirent_t *dirent, ...) {
    fs_node_t *node = kmalloc(sizeof(fs_node_t));
    // ... 设置其他字段 ...
    
    node->write = fat32_file_write;  // 设置写入函数
    node->create = fat32_dir_create;
    node->mkdir = fat32_dir_mkdir;
    node->unlink = fat32_dir_unlink;
    
    return node;
}
```

### 4.5 测试用例

```bash
# 在 shell 中测试
touch /fat32/test.txt
write /fat32/test.txt Hello World
cat /fat32/test.txt
rm /fat32/test.txt
mkdir /fat32/newdir
rmdir /fat32/newdir
```

---

## 5. 信号机制

### 5.1 功能描述

实现 POSIX 风格的信号机制：
- `sigaction`: 设置信号处理函数
- `sigprocmask`: 修改信号掩码
- `sigreturn`: 从信号处理返回
- `kill`: 向进程发送信号（已部分实现）

### 5.2 数据结构

```c
// src/include/kernel/signal.h

#ifndef _KERNEL_SIGNAL_H_
#define _KERNEL_SIGNAL_H_

#include <types.h>

// 标准信号定义
#define SIGHUP    1   // 挂起
#define SIGINT    2   // 中断（Ctrl+C）
#define SIGQUIT   3   // 退出
#define SIGILL    4   // 非法指令
#define SIGTRAP   5   // 跟踪陷阱
#define SIGABRT   6   // 中止
#define SIGBUS    7   // 总线错误
#define SIGFPE    8   // 浮点异常
#define SIGKILL   9   // 强制终止（不可捕获）
#define SIGUSR1   10  // 用户定义信号 1
#define SIGSEGV   11  // 段错误
#define SIGUSR2   12  // 用户定义信号 2
#define SIGPIPE   13  // 管道破裂
#define SIGALRM   14  // 定时器
#define SIGTERM   15  // 终止
#define SIGCHLD   17  // 子进程状态改变
#define SIGCONT   18  // 继续执行
#define SIGSTOP   19  // 停止（不可捕获）
#define SIGTSTP   20  // 终端停止

#define NSIG      32  // 最大信号数

// 特殊信号处理值
#define SIG_DFL   ((sighandler_t)0)   // 默认处理
#define SIG_IGN   ((sighandler_t)1)   // 忽略信号
#define SIG_ERR   ((sighandler_t)-1)  // 错误

// 信号处理函数类型
typedef void (*sighandler_t)(int);

// sigaction 结构
struct sigaction {
    sighandler_t sa_handler;   // 信号处理函数
    uint32_t sa_mask;          // 处理时阻塞的信号掩码
    uint32_t sa_flags;         // 标志
};

// 信号掩码操作
#define SIG_BLOCK   0  // 阻塞信号
#define SIG_UNBLOCK 1  // 解除阻塞
#define SIG_SETMASK 2  // 设置掩码

// 进程信号状态
typedef struct signal_state {
    uint32_t pending;                      // 待处理信号位图
    uint32_t blocked;                      // 阻塞信号位图
    struct sigaction handlers[NSIG];       // 信号处理函数表
} signal_state_t;

// 信号帧（保存在用户栈上）
typedef struct signal_frame {
    uint32_t signum;           // 信号号
    uint32_t eax;              // 保存的寄存器
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t eip;              // 返回地址
    uint32_t eflags;
    uint32_t esp;              // 用户栈指针
    uint32_t retcode[2];       // sigreturn 代码
} signal_frame_t;

#endif // _KERNEL_SIGNAL_H_
```

### 5.3 进程控制块扩展

```c
// src/include/kernel/task.h

typedef struct task {
    // ... 现有字段 ...
    
    /* 信号处理 */
    signal_state_t signals;      // 信号状态
    bool in_signal_handler;      // 是否在信号处理中
    
    // ... 其他字段 ...
} task_t;
```

### 5.4 实现方案

#### 5.4.1 sys_sigaction

```c
// src/kernel/syscalls/signal.c

/**
 * sys_sigaction - 设置信号处理函数
 * @param signum 信号号
 * @param act 新的处理方式
 * @param oldact 返回旧的处理方式
 */
uint32_t sys_sigaction(int signum, const struct sigaction *act, 
                        struct sigaction *oldact) {
    if (signum < 1 || signum >= NSIG) {
        return (uint32_t)-1;
    }
    
    // SIGKILL 和 SIGSTOP 不能被捕获或忽略
    if (signum == SIGKILL || signum == SIGSTOP) {
        return (uint32_t)-1;
    }
    
    task_t *current = task_get_current();
    if (!current) {
        return (uint32_t)-1;
    }
    
    // 返回旧的处理方式
    if (oldact) {
        *oldact = current->signals.handlers[signum];
    }
    
    // 设置新的处理方式
    if (act) {
        current->signals.handlers[signum] = *act;
    }
    
    return 0;
}
```

#### 5.4.2 信号投递

```c
/**
 * signal_deliver - 向进程投递信号
 */
int signal_deliver(task_t *task, int signum) {
    if (!task || signum < 1 || signum >= NSIG) {
        return -1;
    }
    
    // 如果信号被阻塞，添加到待处理
    if (task->signals.blocked & (1 << signum)) {
        task->signals.pending |= (1 << signum);
        return 0;
    }
    
    // 获取信号处理方式
    struct sigaction *sa = &task->signals.handlers[signum];
    
    if (sa->sa_handler == SIG_IGN) {
        // 忽略信号
        return 0;
    }
    
    if (sa->sa_handler == SIG_DFL) {
        // 默认处理
        return signal_default_action(task, signum);
    }
    
    // 标记待处理，等待进程返回用户态时处理
    task->signals.pending |= (1 << signum);
    
    // 如果进程在睡眠，唤醒它
    if (task->state == TASK_BLOCKED) {
        task->state = TASK_READY;
        ready_queue_add(task);
    }
    
    return 0;
}
```

#### 5.4.3 信号处理（返回用户态时）

```c
/**
 * signal_handle_pending - 处理待处理信号
 * 在从内核态返回用户态前调用
 */
void signal_handle_pending(uint32_t *frame) {
    task_t *current = task_get_current();
    if (!current || !current->is_user_process) {
        return;
    }
    
    // 查找第一个待处理且未阻塞的信号
    uint32_t deliverable = current->signals.pending & ~current->signals.blocked;
    if (deliverable == 0) {
        return;
    }
    
    // 找到最低位的信号
    int signum = 0;
    for (int i = 1; i < NSIG; i++) {
        if (deliverable & (1 << i)) {
            signum = i;
            break;
        }
    }
    
    if (signum == 0) return;
    
    // 清除待处理标志
    current->signals.pending &= ~(1 << signum);
    
    struct sigaction *sa = &current->signals.handlers[signum];
    
    if (sa->sa_handler == SIG_DFL || sa->sa_handler == SIG_IGN) {
        return;
    }
    
    // 设置信号帧
    setup_signal_frame(current, frame, signum, sa);
}

/**
 * 在用户栈上设置信号帧
 */
static void setup_signal_frame(task_t *task, uint32_t *frame, 
                                int signum, struct sigaction *sa) {
    // 获取用户栈指针
    uint32_t user_esp = frame[11];  // 用户 ESP
    
    // 在用户栈上分配信号帧
    user_esp -= sizeof(signal_frame_t);
    signal_frame_t *sf = (signal_frame_t *)user_esp;
    
    // 保存当前上下文
    sf->signum = signum;
    sf->eax = frame[1];
    sf->ebx = frame[2];
    sf->ecx = frame[3];
    sf->edx = frame[4];
    sf->esi = frame[5];
    sf->edi = frame[6];
    sf->ebp = frame[7];
    sf->eip = frame[8];     // 原返回地址
    sf->eflags = frame[10];
    sf->esp = frame[11];    // 原用户栈指针
    
    // 设置 sigreturn 代码（在用户栈上）
    // mov eax, SYS_SIGRETURN
    // int 0x80
    sf->retcode[0] = 0xB8 | (SYS_SIGRETURN << 8);  // mov eax, imm32
    sf->retcode[1] = 0x80CD0000;                    // int 0x80
    
    // 修改返回地址到信号处理函数
    frame[8] = (uint32_t)sa->sa_handler;  // EIP = 处理函数
    frame[11] = user_esp;                  // ESP = 新栈顶
    
    // 在栈上放置参数和返回地址
    user_esp -= 4;
    *(uint32_t *)user_esp = signum;        // 参数：信号号
    user_esp -= 4;
    *(uint32_t *)user_esp = (uint32_t)sf->retcode;  // 返回地址：sigreturn
    
    frame[11] = user_esp;
    
    // 阻塞当前信号（防止嵌套）
    task->signals.blocked |= sa->sa_mask | (1 << signum);
    task->in_signal_handler = true;
}
```

#### 5.4.4 sys_sigreturn

```c
/**
 * sys_sigreturn - 从信号处理返回
 */
uint32_t sys_sigreturn(uint32_t *frame) {
    task_t *current = task_get_current();
    if (!current) {
        return (uint32_t)-1;
    }
    
    // 获取信号帧
    uint32_t user_esp = frame[11];
    signal_frame_t *sf = (signal_frame_t *)(user_esp + 8);  // 跳过参数和返回地址
    
    // 恢复上下文
    frame[1] = sf->eax;
    frame[2] = sf->ebx;
    frame[3] = sf->ecx;
    frame[4] = sf->edx;
    frame[5] = sf->esi;
    frame[6] = sf->edi;
    frame[7] = sf->ebp;
    frame[8] = sf->eip;
    frame[10] = sf->eflags;
    frame[11] = sf->esp;
    
    // 恢复信号掩码
    // (实际实现需要保存和恢复原掩码)
    current->in_signal_handler = false;
    
    return sf->eax;  // 返回原来的 eax
}
```

### 5.5 测试用例

```c
// 用户程序测试
#include <signal.h>

volatile int got_signal = 0;

void handler(int signum) {
    got_signal = signum;
}

int main() {
    struct sigaction sa;
    sa.sa_handler = handler;
    sa.sa_mask = 0;
    sa.sa_flags = 0;
    
    sigaction(SIGUSR1, &sa, NULL);
    
    printf("PID: %d, waiting for signal...\n", getpid());
    
    while (!got_signal) {
        // 等待信号
    }
    
    printf("Got signal %d!\n", got_signal);
    return 0;
}
```

---

# 优先级中：增强系统能力

## 6. SYS_PIPE - 管道

### 6.1 功能描述

创建一个单向数据通道：
- `pipe(int fd[2])`: 创建管道，`fd[0]` 为读端，`fd[1]` 为写端
- 支持阻塞读写
- 当写端关闭时，读端返回 EOF

### 6.2 数据结构

```c
// src/include/fs/pipe.h

#ifndef _FS_PIPE_H_
#define _FS_PIPE_H_

#include <types.h>
#include <fs/vfs.h>
#include <kernel/sync/mutex.h>
#include <kernel/sync/semaphore.h>

#define PIPE_BUFFER_SIZE 4096

typedef struct pipe {
    uint8_t buffer[PIPE_BUFFER_SIZE];
    uint32_t read_pos;       // 读位置
    uint32_t write_pos;      // 写位置
    uint32_t count;          // 缓冲区中的数据量
    
    uint32_t readers;        // 读端引用计数
    uint32_t writers;        // 写端引用计数
    
    mutex_t lock;            // 保护缓冲区
    semaphore_t read_sem;    // 读信号量（数据可用）
    semaphore_t write_sem;   // 写信号量（空间可用）
    
    bool read_closed;        // 读端是否关闭
    bool write_closed;       // 写端是否关闭
} pipe_t;

// 创建管道
int pipe_create(fs_node_t **read_node, fs_node_t **write_node);

#endif // _FS_PIPE_H_
```

### 6.3 实现方案

```c
// src/fs/pipe.c

#include <fs/pipe.h>
#include <mm/heap.h>
#include <lib/string.h>

static uint32_t pipe_read(fs_node_t *node, uint32_t offset, 
                          uint32_t size, uint8_t *buffer);
static uint32_t pipe_write(fs_node_t *node, uint32_t offset, 
                           uint32_t size, uint8_t *buffer);
static void pipe_close(fs_node_t *node);

/**
 * 创建管道
 */
int pipe_create(fs_node_t **read_node, fs_node_t **write_node) {
    // 分配管道结构
    pipe_t *pipe = kmalloc(sizeof(pipe_t));
    if (!pipe) return -1;
    
    memset(pipe, 0, sizeof(pipe_t));
    mutex_init(&pipe->lock);
    semaphore_init(&pipe->read_sem, 0);           // 初始无数据
    semaphore_init(&pipe->write_sem, PIPE_BUFFER_SIZE);  // 初始全部空间可用
    pipe->readers = 1;
    pipe->writers = 1;
    
    // 创建读端节点
    fs_node_t *rnode = kmalloc(sizeof(fs_node_t));
    if (!rnode) {
        kfree(pipe);
        return -1;
    }
    memset(rnode, 0, sizeof(fs_node_t));
    strcpy(rnode->name, "pipe_read");
    rnode->type = FS_PIPE;
    rnode->impl = pipe;
    rnode->impl_data = 0;  // 标记为读端
    rnode->read = pipe_read;
    rnode->close = pipe_close;
    rnode->flags = FS_NODE_FLAG_ALLOCATED;
    rnode->ref_count = 1;
    
    // 创建写端节点
    fs_node_t *wnode = kmalloc(sizeof(fs_node_t));
    if (!wnode) {
        kfree(rnode);
        kfree(pipe);
        return -1;
    }
    memset(wnode, 0, sizeof(fs_node_t));
    strcpy(wnode->name, "pipe_write");
    wnode->type = FS_PIPE;
    wnode->impl = pipe;
    wnode->impl_data = 1;  // 标记为写端
    wnode->write = pipe_write;
    wnode->close = pipe_close;
    wnode->flags = FS_NODE_FLAG_ALLOCATED;
    wnode->ref_count = 1;
    
    *read_node = rnode;
    *write_node = wnode;
    
    return 0;
}

/**
 * 管道读取
 */
static uint32_t pipe_read(fs_node_t *node, uint32_t offset, 
                          uint32_t size, uint8_t *buffer) {
    (void)offset;
    
    pipe_t *pipe = (pipe_t *)node->impl;
    uint32_t bytes_read = 0;
    
    mutex_lock(&pipe->lock);
    
    while (bytes_read < size) {
        // 等待数据可用
        while (pipe->count == 0) {
            // 如果写端已关闭，返回 EOF
            if (pipe->write_closed) {
                mutex_unlock(&pipe->lock);
                return bytes_read;
            }
            
            // 释放锁，等待信号量
            mutex_unlock(&pipe->lock);
            semaphore_wait(&pipe->read_sem);
            mutex_lock(&pipe->lock);
        }
        
        // 读取数据
        buffer[bytes_read] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % PIPE_BUFFER_SIZE;
        pipe->count--;
        bytes_read++;
        
        // 通知写端有空间
        semaphore_signal(&pipe->write_sem);
    }
    
    mutex_unlock(&pipe->lock);
    return bytes_read;
}

/**
 * 管道写入
 */
static uint32_t pipe_write(fs_node_t *node, uint32_t offset, 
                           uint32_t size, uint8_t *buffer) {
    (void)offset;
    
    pipe_t *pipe = (pipe_t *)node->impl;
    uint32_t bytes_written = 0;
    
    mutex_lock(&pipe->lock);
    
    // 如果读端已关闭，返回错误（SIGPIPE）
    if (pipe->read_closed) {
        mutex_unlock(&pipe->lock);
        return 0;  // 实际应该发送 SIGPIPE
    }
    
    while (bytes_written < size) {
        // 等待空间可用
        while (pipe->count == PIPE_BUFFER_SIZE) {
            if (pipe->read_closed) {
                mutex_unlock(&pipe->lock);
                return bytes_written;
            }
            
            mutex_unlock(&pipe->lock);
            semaphore_wait(&pipe->write_sem);
            mutex_lock(&pipe->lock);
        }
        
        // 写入数据
        pipe->buffer[pipe->write_pos] = buffer[bytes_written];
        pipe->write_pos = (pipe->write_pos + 1) % PIPE_BUFFER_SIZE;
        pipe->count++;
        bytes_written++;
        
        // 通知读端有数据
        semaphore_signal(&pipe->read_sem);
    }
    
    mutex_unlock(&pipe->lock);
    return bytes_written;
}

/**
 * 关闭管道端
 */
static void pipe_close(fs_node_t *node) {
    pipe_t *pipe = (pipe_t *)node->impl;
    bool is_write_end = (node->impl_data == 1);
    
    mutex_lock(&pipe->lock);
    
    if (is_write_end) {
        pipe->writers--;
        if (pipe->writers == 0) {
            pipe->write_closed = true;
            // 唤醒所有等待读取的进程
            semaphore_signal(&pipe->read_sem);
        }
    } else {
        pipe->readers--;
        if (pipe->readers == 0) {
            pipe->read_closed = true;
            // 唤醒所有等待写入的进程
            semaphore_signal(&pipe->write_sem);
        }
    }
    
    // 如果两端都关闭，释放管道
    bool should_free = (pipe->readers == 0 && pipe->writers == 0);
    
    mutex_unlock(&pipe->lock);
    
    if (should_free) {
        kfree(pipe);
    }
}
```

### 6.4 系统调用

```c
// src/kernel/syscalls/fs.c

uint32_t sys_pipe(int32_t *fds) {
    if (!fds) return (uint32_t)-1;
    
    task_t *current = task_get_current();
    if (!current || !current->fd_table) return (uint32_t)-1;
    
    fs_node_t *read_node, *write_node;
    if (pipe_create(&read_node, &write_node) != 0) {
        return (uint32_t)-1;
    }
    
    // 分配文件描述符
    int32_t read_fd = fd_table_alloc(current->fd_table, read_node, O_RDONLY);
    if (read_fd < 0) {
        vfs_release_node(read_node);
        vfs_release_node(write_node);
        return (uint32_t)-1;
    }
    
    int32_t write_fd = fd_table_alloc(current->fd_table, write_node, O_WRONLY);
    if (write_fd < 0) {
        fd_table_free(current->fd_table, read_fd);
        vfs_release_node(write_node);
        return (uint32_t)-1;
    }
    
    fds[0] = read_fd;
    fds[1] = write_fd;
    
    return 0;
}
```

---

## 7. SYS_DUP/DUP2 - 文件描述符复制

### 7.1 功能描述

- `dup(oldfd)`: 复制文件描述符，返回新的最小可用 fd
- `dup2(oldfd, newfd)`: 复制文件描述符到指定编号

### 7.2 实现方案

```c
// src/kernel/syscalls/fs.c

/**
 * sys_dup - 复制文件描述符
 */
uint32_t sys_dup(int32_t oldfd) {
    task_t *current = task_get_current();
    if (!current || !current->fd_table) {
        return (uint32_t)-1;
    }
    
    fd_entry_t *old_entry = fd_table_get(current->fd_table, oldfd);
    if (!old_entry || !old_entry->node) {
        return (uint32_t)-1;
    }
    
    // 分配新的文件描述符（会自动增加引用计数）
    int32_t newfd = fd_table_alloc(current->fd_table, old_entry->node, old_entry->flags);
    if (newfd < 0) {
        return (uint32_t)-1;
    }
    
    // 复制偏移量
    fd_entry_t *new_entry = fd_table_get(current->fd_table, newfd);
    if (new_entry) {
        new_entry->offset = old_entry->offset;
    }
    
    return (uint32_t)newfd;
}

/**
 * sys_dup2 - 复制文件描述符到指定编号
 */
uint32_t sys_dup2(int32_t oldfd, int32_t newfd) {
    if (oldfd == newfd) {
        return (uint32_t)newfd;  // 相同则直接返回
    }
    
    task_t *current = task_get_current();
    if (!current || !current->fd_table) {
        return (uint32_t)-1;
    }
    
    if (newfd < 0 || newfd >= MAX_FDS) {
        return (uint32_t)-1;
    }
    
    fd_entry_t *old_entry = fd_table_get(current->fd_table, oldfd);
    if (!old_entry || !old_entry->node) {
        return (uint32_t)-1;
    }
    
    // 如果 newfd 已打开，先关闭它
    fd_entry_t *existing = fd_table_get(current->fd_table, newfd);
    if (existing && existing->in_use) {
        fd_table_free(current->fd_table, newfd);
    }
    
    // 手动设置新的文件描述符
    spinlock_lock(&current->fd_table->lock);
    
    current->fd_table->entries[newfd].node = old_entry->node;
    current->fd_table->entries[newfd].offset = old_entry->offset;
    current->fd_table->entries[newfd].flags = old_entry->flags;
    current->fd_table->entries[newfd].in_use = true;
    
    // 增加引用计数
    vfs_ref_node(old_entry->node);
    
    spinlock_unlock(&current->fd_table->lock);
    
    return (uint32_t)newfd;
}
```

### 7.3 用户态封装

```c
int dup(int oldfd) {
    return (int)syscall1(SYS_DUP, (uint32_t)oldfd);
}

int dup2(int oldfd, int newfd) {
    return (int)syscall2(SYS_DUP2, (uint32_t)oldfd, (uint32_t)newfd);
}
```

---

## 8. SYS_MMAP/MUNMAP - 内存映射

### 8.1 功能描述

- `mmap`: 映射文件或匿名内存到进程地址空间
- `munmap`: 取消映射

### 8.2 简化实现（匿名映射）

先实现匿名映射（不涉及文件），用于动态内存分配。

```c
// src/kernel/syscalls/mm.c

/**
 * sys_mmap - 内存映射（简化版：仅支持匿名映射）
 */
uint32_t sys_mmap(uint32_t addr, uint32_t length, uint32_t prot,
                   uint32_t flags, int32_t fd, uint32_t offset) {
    (void)fd; (void)offset;  // 暂不支持文件映射
    
    task_t *current = task_get_current();
    if (!current) return (uint32_t)-1;
    
    // 只支持匿名映射
    if (!(flags & MAP_ANONYMOUS)) {
        LOG_ERROR_MSG("sys_mmap: only anonymous mapping supported\n");
        return (uint32_t)-1;
    }
    
    // 对齐长度
    length = PAGE_ALIGN_UP(length);
    if (length == 0) return (uint32_t)-1;
    
    // 查找空闲虚拟地址空间
    uint32_t vaddr = find_free_vaddr(current, addr, length);
    if (vaddr == 0) return (uint32_t)-1;
    
    // 确定页面标志
    uint32_t page_flags = PAGE_PRESENT | PAGE_USER;
    if (prot & PROT_WRITE) {
        page_flags |= PAGE_WRITE;
    }
    
    // 分配并映射页面
    for (uint32_t page = vaddr; page < vaddr + length; page += PAGE_SIZE) {
        uint32_t phys = pmm_alloc_frame();
        if (!phys) {
            // 回滚已分配的页面
            for (uint32_t p = vaddr; p < page; p += PAGE_SIZE) {
                uint32_t pf = vmm_unmap_page_in_directory(current->page_dir_phys, p);
                if (pf) pmm_free_frame(pf);
            }
            return (uint32_t)-1;
        }
        
        vmm_map_page_in_directory(current->page_dir_phys, page, phys, page_flags);
        
        // 清零页面
        memset((void *)page, 0, PAGE_SIZE);
    }
    
    return vaddr;
}

/**
 * sys_munmap - 取消内存映射
 */
uint32_t sys_munmap(uint32_t addr, uint32_t length) {
    task_t *current = task_get_current();
    if (!current) return (uint32_t)-1;
    
    // 对齐
    addr = PAGE_ALIGN_DOWN(addr);
    length = PAGE_ALIGN_UP(length);
    
    // 取消映射并释放物理页
    for (uint32_t page = addr; page < addr + length; page += PAGE_SIZE) {
        uint32_t phys = vmm_unmap_page_in_directory(current->page_dir_phys, page);
        if (phys) {
            pmm_free_frame(phys);
        }
    }
    
    return 0;
}
```

---

## 9. SYS_UNAME - 系统信息查询

### 9.1 功能描述

返回系统信息（内核名称、版本、机器类型等）。

### 9.2 实现方案

```c
// src/include/kernel/utsname.h

struct utsname {
    char sysname[65];     // 操作系统名称
    char nodename[65];    // 网络节点名称
    char release[65];     // 内核版本
    char version[65];     // 版本信息
    char machine[65];     // 硬件类型
};

// src/kernel/syscalls/system.c

#include <kernel/version.h>

uint32_t sys_uname(struct utsname *buf) {
    if (!buf) return (uint32_t)-1;
    
    strcpy(buf->sysname, "CastorOS");
    strcpy(buf->nodename, "castor");
    strcpy(buf->release, KERNEL_VERSION);
    snprintf(buf->version, sizeof(buf->version), 
             "#1 %s %s", __DATE__, __TIME__);
    strcpy(buf->machine, "i386");
    
    return 0;
}
```

---

## 10. SYS_RENAME - 文件重命名

### 10.1 功能描述

重命名文件或目录，或移动文件到其他目录。

### 10.2 实现方案

需要在 VFS 层添加 rename 操作，并在各文件系统中实现：

```c
// src/include/fs/vfs.h
typedef int (*rename_type_t)(fs_node_t *old_dir, const char *old_name,
                              fs_node_t *new_dir, const char *new_name);

// src/kernel/syscalls/fs.c
uint32_t sys_rename(const char *oldpath, const char *newpath) {
    // 1. 解析 oldpath，获取父目录和文件名
    // 2. 解析 newpath，获取目标目录和新文件名
    // 3. 检查是否同一文件系统
    // 4. 调用文件系统的 rename 操作
    // 5. 如果跨目录，需要更新目录项
    
    // 简化实现：仅支持同目录重命名
    // 完整实现需要处理跨目录移动
}
```

---

## 11. SHMFS - 共享内存文件系统

> **状态**：已实现

### 11.1 功能描述

类似 Linux `/dev/shm` 的共享内存文件系统，提供进程间共享内存的能力。

### 11.2 已实现功能

| 功能 | 状态 | 说明 |
|------|------|------|
| 文件创建/删除 | ✅ | `touch /shm/xxx`, `rm /shm/xxx` |
| 文件读写 | ✅ | `cat`, `write` 命令 |
| 目录列表 | ✅ | `ls /shm` |
| 文件截断 | ✅ | `ftruncate()` 支持 |
| 物理页管理 | ✅ | 自动分配/释放物理内存 |
| 映射计数 | ✅ | 供 mmap 集成使用 |
| 系统启动挂载 | ✅ | 自动挂载到 `/shm` |

### 11.3 文件位置

| 文件 | 说明 |
|------|------|
| `src/include/fs/shmfs.h` | 头文件，数据结构和接口定义 |
| `src/fs/shmfs.c` | 实现文件 |
| `src/kernel/fs_bootstrap.c` | 系统启动时挂载到 `/shm` |

### 11.4 使用方法

```bash
# Shell 命令测试
touch /shm/test              # 创建共享内存
write /shm/test "Hello!"     # 写入数据
cat /shm/test                # 读取数据
ls /shm                      # 列出所有共享内存
rm /shm/test                 # 删除共享内存
```

### 11.5 待完成（依赖 SYS_MMAP）

- 用户态 `shm_open()`/`shm_unlink()` 封装
- 与 `mmap()` 集成实现真正的共享内存映射

---

## 12. RTC 驱动 - 实时时钟

### 12.1 功能描述

读取 CMOS RTC（实时时钟）获取真实日期时间。

### 12.2 实现方案

```c
// src/drivers/rtc.c

#include <kernel/io.h>

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

// RTC 寄存器
#define RTC_SECONDS   0x00
#define RTC_MINUTES   0x02
#define RTC_HOURS     0x04
#define RTC_DAY       0x07
#define RTC_MONTH     0x08
#define RTC_YEAR      0x09
#define RTC_STATUS_A  0x0A
#define RTC_STATUS_B  0x0B

// 读取 CMOS 寄存器
static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

// 等待 RTC 更新完成
static void rtc_wait_update(void) {
    while (cmos_read(RTC_STATUS_A) & 0x80) {
        // 等待更新完成
    }
}

// BCD 转二进制
static uint8_t bcd_to_bin(uint8_t bcd) {
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

// 获取当前时间
void rtc_get_time(uint8_t *hours, uint8_t *minutes, uint8_t *seconds) {
    rtc_wait_update();
    
    uint8_t status_b = cmos_read(RTC_STATUS_B);
    bool is_bcd = !(status_b & 0x04);
    bool is_24h = status_b & 0x02;
    
    *seconds = cmos_read(RTC_SECONDS);
    *minutes = cmos_read(RTC_MINUTES);
    *hours = cmos_read(RTC_HOURS);
    
    if (is_bcd) {
        *seconds = bcd_to_bin(*seconds);
        *minutes = bcd_to_bin(*minutes);
        *hours = bcd_to_bin(*hours & 0x7F);
        
        if (!is_24h && (*hours & 0x80)) {
            *hours = (*hours + 12) % 24;
        }
    }
}

// 获取当前日期
void rtc_get_date(uint16_t *year, uint8_t *month, uint8_t *day) {
    rtc_wait_update();
    
    uint8_t status_b = cmos_read(RTC_STATUS_B);
    bool is_bcd = !(status_b & 0x04);
    
    *day = cmos_read(RTC_DAY);
    *month = cmos_read(RTC_MONTH);
    uint8_t year_byte = cmos_read(RTC_YEAR);
    
    if (is_bcd) {
        *day = bcd_to_bin(*day);
        *month = bcd_to_bin(*month);
        year_byte = bcd_to_bin(year_byte);
    }
    
    // 假设 21 世纪
    *year = 2000 + year_byte;
}

// 初始化 RTC
void rtc_init(void) {
    LOG_INFO_MSG("RTC initialized\n");
    
    uint16_t year;
    uint8_t month, day, hours, minutes, seconds;
    
    rtc_get_date(&year, &month, &day);
    rtc_get_time(&hours, &minutes, &seconds);
    
    LOG_INFO_MSG("Current time: %04u-%02u-%02u %02u:%02u:%02u\n",
                 year, month, day, hours, minutes, seconds);
}
```

### 12.3 集成到 gettimeofday

```c
// src/kernel/syscalls/time.c

uint32_t sys_gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;  // 时区暂不支持
    
    if (!tv) return (uint32_t)-1;
    
    uint16_t year;
    uint8_t month, day, hours, minutes, seconds;
    
    rtc_get_date(&year, &month, &day);
    rtc_get_time(&hours, &minutes, &seconds);
    
    // 转换为 Unix 时间戳（简化计算）
    // 完整实现需要考虑闰年等
    tv->tv_sec = calc_unix_time(year, month, day, hours, minutes, seconds);
    tv->tv_usec = 0;
    
    return 0;
}
```
