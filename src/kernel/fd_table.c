/**
 * 文件描述符表实现
 * 
 * 同步机制：使用 spinlock 保护每个 FD 表的并发访问
 * - 保护 FD 分配、释放和复制操作
 * - 防止多任务同时打开/关闭文件导致的 FD 泄漏或冲突
 * - 防止 fork() 时的竞态条件
 */

#include <kernel/fd_table.h>
#include <fs/pipe.h>
#include <lib/string.h>
#include <lib/klog.h>

void fd_table_init(fd_table_t *table) {
    if (!table) {
        return;
    }
    
    // 初始化自旋锁
    spinlock_init(&table->lock);
    
    for (int i = 0; i < MAX_FDS; i++) {
        table->entries[i].node = NULL;
        table->entries[i].offset = 0;
        table->entries[i].flags = 0;
        table->entries[i].in_use = false;
    }
}

int32_t fd_table_alloc(fd_table_t *table, fs_node_t *node, int32_t flags) {
    if (!table || !node) {
        return -1;
    }
    
    spinlock_lock(&table->lock);
    
    // 查找第一个空闲的文件描述符
    int32_t result = -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (!table->entries[i].in_use) {
            table->entries[i].node = node;
            table->entries[i].offset = 0;
            table->entries[i].flags = flags;
            table->entries[i].in_use = true;
            
            // 增加节点引用计数
            vfs_ref_node(node);
            
            result = i;
            break;
        }
    }
    
    spinlock_unlock(&table->lock);
    
    // 表满时 result 保持为 -1
    return result;
}

fd_entry_t *fd_table_get(fd_table_t *table, int32_t fd) {
    if (!table || fd < 0 || fd >= MAX_FDS) {
        return NULL;
    }
    
    spinlock_lock(&table->lock);
    
    fd_entry_t *result = NULL;
    if (table->entries[fd].in_use) {
        result = &table->entries[fd];
    }
    
    spinlock_unlock(&table->lock);
    
    return result;
}

int32_t fd_table_free(fd_table_t *table, int32_t fd) {
    if (!table || fd < 0 || fd >= MAX_FDS) {
        return -1;
    }
    
    spinlock_lock(&table->lock);
    
    if (!table->entries[fd].in_use) {
        spinlock_unlock(&table->lock);
        return -1;
    }
    
    // 保存节点指针，在解锁后处理
    fs_node_t *node = table->entries[fd].node;
    
    // 清理表项
    table->entries[fd].node = NULL;
    table->entries[fd].offset = 0;
    table->entries[fd].flags = 0;
    table->entries[fd].in_use = false;
    
    spinlock_unlock(&table->lock);
    
    // 在解锁后关闭文件和释放节点
    // 避免在持有锁的情况下调用可能阻塞的 VFS 操作
    if (node) {
        if (node->close) {
            vfs_close(node);
        }
        // 释放动态分配的节点
        vfs_release_node(node);
    }
    
    return 0;
}

int32_t fd_table_copy(fd_table_t *src, fd_table_t *dst) {
    if (!src || !dst) {
        return -1;
    }
    
    // 按地址顺序加锁，避免死锁
    spinlock_t *first_lock, *second_lock;
    if ((uint32_t)src < (uint32_t)dst) {
        first_lock = &src->lock;
        second_lock = &dst->lock;
    } else {
        first_lock = &dst->lock;
        second_lock = &src->lock;
    }
    
    spinlock_lock(first_lock);
    // 如果 src == dst，不要重复加锁
    if (src != dst) {
        spinlock_lock(second_lock);
    }
    
    for (int i = 0; i < MAX_FDS; i++) {
        if (src->entries[i].in_use) {
            dst->entries[i].node = src->entries[i].node;
            dst->entries[i].offset = src->entries[i].offset;
            dst->entries[i].flags = src->entries[i].flags;
            dst->entries[i].in_use = true;
            
            // 关键修复：增加引用计数，因为现在有两个fd指向同一个节点
            if (dst->entries[i].node) {
                vfs_ref_node(dst->entries[i].node);
                
                // 如果是管道，还需要增加 readers/writers 计数
                if (dst->entries[i].node->type == FS_PIPE) {
                    pipe_on_dup(dst->entries[i].node);
                }
            }
        } else {
            dst->entries[i].in_use = false;
        }
    }
    
    // 解锁顺序与加锁顺序相反
    if (src != dst) {
        spinlock_unlock(second_lock);
    }
    spinlock_unlock(first_lock);
    
    return 0;
}
