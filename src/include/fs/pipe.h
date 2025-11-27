/**
 * 管道（Pipe）实现
 * 
 * 提供进程间单向数据通道：
 * - pipe(int fd[2]): 创建管道，fd[0] 为读端，fd[1] 为写端
 * - 支持阻塞读写
 * - 当写端关闭时，读端返回 EOF
 * - 当读端关闭时，写端收到 SIGPIPE（简化实现：返回错误）
 */

#ifndef _FS_PIPE_H_
#define _FS_PIPE_H_

#include <types.h>
#include <fs/vfs.h>
#include <kernel/sync/mutex.h>
#include <kernel/sync/semaphore.h>

/* 管道缓冲区大小 */
#define PIPE_BUFFER_SIZE 4096

/**
 * 管道结构
 */
typedef struct pipe {
    uint8_t buffer[PIPE_BUFFER_SIZE];   // 环形缓冲区
    uint32_t read_pos;                   // 读位置
    uint32_t write_pos;                  // 写位置
    uint32_t count;                      // 缓冲区中的数据量
    
    uint32_t readers;                    // 读端引用计数
    uint32_t writers;                    // 写端引用计数
    
    mutex_t lock;                        // 保护缓冲区
    semaphore_t read_sem;                // 读信号量（数据可用）
    semaphore_t write_sem;               // 写信号量（空间可用）
    
    bool read_closed;                    // 读端是否关闭
    bool write_closed;                   // 写端是否关闭
} pipe_t;

/**
 * 创建管道
 * @param read_node 输出读端节点
 * @param write_node 输出写端节点
 * @return 0 成功，-1 失败
 */
int pipe_create(fs_node_t **read_node, fs_node_t **write_node);

/**
 * 初始化管道子系统
 */
void pipe_init(void);

/**
 * 当管道文件描述符被复制时调用（fork, dup, dup2）
 * 增加相应端的引用计数
 * @param node 管道节点
 */
void pipe_on_dup(fs_node_t *node);

#endif // _FS_PIPE_H_

