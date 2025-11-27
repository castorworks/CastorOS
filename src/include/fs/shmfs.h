#ifndef _FS_SHMFS_H_
#define _FS_SHMFS_H_

#include <fs/vfs.h>
#include <kernel/sync/mutex.h>
#include <kernel/sync/spinlock.h>

/**
 * SHMFS - 共享内存文件系统
 * 
 * 类似 Linux 的 /dev/shm，提供进程间共享内存的能力：
 * - 基于 VFS 接口，可挂载到 /shm 目录
 * - 支持 POSIX 风格的 shm_open()/shm_unlink() 语义
 * - 文件数据存储在物理内存中，可被多个进程共享
 * - 支持文件的创建、读写、删除操作
 * - 支持通过 mmap() 映射到进程地址空间
 */

// 共享内存页表项
typedef struct shmfs_page {
    uint32_t phys_addr;           // 物理页地址
    struct shmfs_page *next;      // 链表下一项
} shmfs_page_t;

// 共享内存文件数据结构
typedef struct shmfs_file {
    shmfs_page_t *pages;          // 物理页链表
    uint32_t size;                // 文件大小
    uint32_t num_pages;           // 页数
    uint32_t map_count;           // 映射计数（有多少进程映射了此文件）
    mutex_t lock;                 // 保护文件数据
} shmfs_file_t;

// 共享内存目录项
typedef struct shmfs_dirent {
    char name[128];               // 文件名
    fs_node_t *node;              // 指向文件节点
    struct shmfs_dirent *next;    // 链表下一项
} shmfs_dirent_t;

// 共享内存目录数据结构
typedef struct shmfs_dir {
    shmfs_dirent_t *entries;      // 目录项链表
    uint32_t count;               // 目录项数量
    mutex_t lock;                 // 目录锁
} shmfs_dir_t;

/**
 * 初始化 shmfs
 * @return 根目录节点
 */
fs_node_t *shmfs_init(void);

/**
 * 创建一个新的 shmfs 挂载点
 * @param name 挂载点名称
 * @return 根目录节点
 */
fs_node_t *shmfs_create(const char *name);

/**
 * 获取共享内存文件的物理页列表（供 mmap 使用）
 * @param node 文件节点
 * @param offset 起始偏移（页对齐）
 * @param num_pages 请求的页数
 * @param phys_pages 输出物理页地址数组
 * @return 实际获取的页数，失败返回 0
 */
uint32_t shmfs_get_phys_pages(fs_node_t *node, uint32_t offset, 
                               uint32_t num_pages, uint32_t *phys_pages);

/**
 * 增加映射计数
 * @param node 文件节点
 */
void shmfs_map_ref(fs_node_t *node);

/**
 * 减少映射计数
 * @param node 文件节点
 */
void shmfs_map_unref(fs_node_t *node);

/**
 * 检查节点是否为 shmfs 文件
 * @param node 文件节点
 * @return true 如果是 shmfs 文件
 */
bool shmfs_is_shmfs_node(fs_node_t *node);

#endif // _FS_SHMFS_H_

