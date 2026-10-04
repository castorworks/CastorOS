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

void kernel::FdTable::init(kernel::FdTable *table) {
    if (!table) {
        return;
    }
    
    // 初始化自旋锁
    table->lock.init();
    
    for (int i = 0; i < MAX_FDS; i++) {
        table->entries[i].node = NULL;
        table->entries[i].offset = 0;
        table->entries[i].flags = 0;
        table->entries[i].in_use = false;
    }
}

int32_t kernel::FdTable::alloc(kernel::FdTable *table, fs_node_t *node, int32_t flags) {
    if (!table || !node) {
        return -1;
    }
    
    table->lock.lock();
    
    // 查找第一个空闲的文件描述符
    int32_t result = -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (!table->entries[i].in_use) {
            table->entries[i].node = node;
            table->entries[i].offset = 0;
            table->entries[i].flags = flags;
            table->entries[i].in_use = true;
            
            // 增加节点引用计数
            fs::Vfs::ref_node(node);
            fs::Vfs::pin(node);
            
            result = i;
            break;
        }
    }
    
    table->lock.unlock();
    
    // 表满时 result 保持为 -1
    return result;
}

kernel::FdEntry *kernel::FdTable::get(kernel::FdTable *table, int32_t fd) {
    if (!table || fd < 0 || fd >= MAX_FDS) {
        return NULL;
    }
    
    sync::SpinlockGuard guard(table->lock);
    
    kernel::FdEntry *result = NULL;
    if (table->entries[fd].in_use) {
        result = &table->entries[fd];
    }
    
    return result;
}

int32_t kernel::FdTable::free(kernel::FdTable *table, int32_t fd) {
    if (!table || fd < 0 || fd >= MAX_FDS) {
        return -1;
    }
    
    fs_node_t *node;
    {
        sync::LockGuard guard(table->lock);
        if (!table->entries[fd].in_use) {
            return -1;
        }
    
        // 保存节点指针，在解锁后处理
        node = table->entries[fd].node;
    
        // 清理表项
        table->entries[fd].node = NULL;
        table->entries[fd].offset = 0;
        table->entries[fd].flags = 0;
        table->entries[fd].in_use = false;
    }
    
    // 在解锁后关闭文件和释放节点
    // 避免在持有锁的情况下调用可能阻塞的 VFS 操作
    if (node) {
        if (fs::node_supports(node, fs::NodeOps::OP_CLOSE)) {
            fs::Vfs::close(node);
        }
        // unpin 返回 true 表示节点已随最后一次关闭被销毁，不能再碰；
        // 否则释放动态分配的节点
        if (!fs::Vfs::unpin(node)) {
            fs::Vfs::release_node(node);
        }
    }
    
    return 0;
}

int32_t kernel::FdTable::copy(kernel::FdTable *src, kernel::FdTable *dst) {
    if (!src || !dst) {
        return -1;
    }
    
    // 按地址顺序加锁，避免死锁
    sync::Spinlock *first_lock, *second_lock;
    if ((uintptr_t)src < (uintptr_t)dst) {
        first_lock = &src->lock;
        second_lock = &dst->lock;
    } else {
        first_lock = &dst->lock;
        second_lock = &src->lock;
    }
    
    {
        sync::LockGuard guard(*first_lock);
        // 如果 src == dst，不要重复加锁
        if (src != dst) {
            second_lock->lock();
        }
    
        for (int i = 0; i < MAX_FDS; i++) {
            if (src->entries[i].in_use) {
                dst->entries[i].node = src->entries[i].node;
                dst->entries[i].offset = src->entries[i].offset;
                dst->entries[i].flags = src->entries[i].flags;
                dst->entries[i].in_use = true;
            
                // 关键修复：增加引用计数，因为现在有两个fd指向同一个节点
                if (dst->entries[i].node) {
                    fs::Vfs::ref_node(dst->entries[i].node);
                    fs::Vfs::pin(dst->entries[i].node);
                
                    // 如果是管道，还需要增加 readers/writers 计数
                    if (dst->entries[i].node->type == FS_PIPE) {
                        fs::Pipe::on_dup(dst->entries[i].node);
                    }
                }
            } else {
                dst->entries[i].in_use = false;
            }
        }
    
        // 解锁顺序与加锁顺序相反
        if (src != dst) {
            second_lock->unlock();
        }
    }
    
    return 0;
}
