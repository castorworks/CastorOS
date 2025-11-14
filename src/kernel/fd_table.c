/**
 * 文件描述符表实现
 */

#include <kernel/fd_table.h>
#include <lib/string.h>
#include <lib/klog.h>

void fd_table_init(fd_table_t *table) {
    if (!table) {
        return;
    }
    
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
    
    // 查找第一个空闲的文件描述符
    for (int i = 0; i < MAX_FDS; i++) {
        if (!table->entries[i].in_use) {
            table->entries[i].node = node;
            table->entries[i].offset = 0;
            table->entries[i].flags = flags;
            table->entries[i].in_use = true;
            return i;
        }
    }
    
    // 表满
    return -1;
}

fd_entry_t *fd_table_get(fd_table_t *table, int32_t fd) {
    if (!table || fd < 0 || fd >= MAX_FDS) {
        return NULL;
    }
    
    if (!table->entries[fd].in_use) {
        return NULL;
    }
    
    return &table->entries[fd];
}

int32_t fd_table_free(fd_table_t *table, int32_t fd) {
    if (!table || fd < 0 || fd >= MAX_FDS) {
        return -1;
    }
    
    if (!table->entries[fd].in_use) {
        return -1;
    }
    
    // 关闭文件
    if (table->entries[fd].node && table->entries[fd].node->close) {
        vfs_close(table->entries[fd].node);
    }
    
    table->entries[fd].node = NULL;
    table->entries[fd].offset = 0;
    table->entries[fd].flags = 0;
    table->entries[fd].in_use = false;
    
    return 0;
}

int32_t fd_table_copy(fd_table_t *src, fd_table_t *dst) {
    if (!src || !dst) {
        return -1;
    }
    
    for (int i = 0; i < MAX_FDS; i++) {
        if (src->entries[i].in_use) {
            dst->entries[i].node = src->entries[i].node;
            dst->entries[i].offset = src->entries[i].offset;
            dst->entries[i].flags = src->entries[i].flags;
            dst->entries[i].in_use = true;
        } else {
            dst->entries[i].in_use = false;
        }
    }
    
    return 0;
}
