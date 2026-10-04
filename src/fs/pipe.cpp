/**
 * 管道（Pipe）实现
 * 
 * 提供进程间单向数据通道：
 * - pipe(int fd[2]): 创建管道，fd[0] 为读端，fd[1] 为写端
 * - 支持阻塞读写
 * - 当写端关闭时，读端返回 EOF
 * - 当读端关闭时，写端收到错误
 */

#include <fs/pipe.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/klog.h>

/* 前向声明 */
static uint32_t pipe_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static uint32_t pipe_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static void pipe_close(fs_node_t *node);

/* 全局 inode 计数器 */
static uint32_t pipe_inode_counter = 0x10000;  // 从高位开始，避免与其他文件系统冲突

/**
 * 初始化管道子系统
 */
void fs::Pipe::init() {
    LOG_INFO_MSG("Pipe subsystem initialized\n");
}

/**
 * 当管道文件描述符被复制时调用（fork, dup, dup2）
 * 增加相应端的引用计数
 */
void fs::Pipe::on_dup(fs_node_t *node) {
    if (!node || node->type != FS_PIPE || !node->impl) {
        return;
    }
    
    pipe_t *pipe = (pipe_t *)node->impl;
    bool is_write_end = (node->impl_data == 1);
    
    sync::MutexGuard guard(pipe->lock);
    
    if (is_write_end) {
        pipe->writers++;
        LOG_DEBUG_MSG("fs::Pipe::on_dup: writers=%u\n", pipe->writers);
    } else {
        pipe->readers++;
        LOG_DEBUG_MSG("fs::Pipe::on_dup: readers=%u\n", pipe->readers);
    }
}

class PipeReadOps final : public fs::NodeOps {
public:
    uint32_t supported() const override { return OP_READ | OP_CLOSE; }
    uint32_t read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) const override {
        return pipe_read(node, offset, size, buffer);
    }
    void close(fs_node_t *node) const override {
        pipe_close(node);
    }
    /* pipe_t 由两端节点共享，生命周期由 readers/writers 计数决定（见 pipe_close），
     * 不能在某一端节点释放时跟着释放 */
    void release_impl(fs_node_t *) const override {}
};
static const PipeReadOps pipe_read_ops{};

class PipeWriteOps final : public fs::NodeOps {
public:
    uint32_t supported() const override { return OP_WRITE | OP_CLOSE; }
    uint32_t write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) const override {
        return pipe_write(node, offset, size, buffer);
    }
    void close(fs_node_t *node) const override {
        pipe_close(node);
    }
    /* pipe_t 由两端节点共享，生命周期由 readers/writers 计数决定（见 pipe_close），
     * 不能在某一端节点释放时跟着释放 */
    void release_impl(fs_node_t *) const override {}
};
static const PipeWriteOps pipe_write_ops{};

/**
 * 创建管道
 * @param read_node 输出读端节点
 * @param write_node 输出写端节点
 * @return 0 成功，-1 失败
 */
int fs::Pipe::create(fs_node_t **read_node, fs_node_t **write_node) {
    if (!read_node || !write_node) {
        LOG_ERROR_MSG("fs::Pipe::create: invalid arguments\n");
        return -1;
    }
    
    // 分配管道结构
    pipe_t *pipe = (pipe_t *)kmalloc(sizeof(pipe_t));
    if (!pipe) {
        LOG_ERROR_MSG("fs::Pipe::create: failed to allocate pipe structure\n");
        return -1;
    }
    
    // 初始化管道
    memset(pipe->buffer, 0, PIPE_BUFFER_SIZE);
    pipe->read_pos = 0;
    pipe->write_pos = 0;
    pipe->count = 0;
    pipe->readers = 1;
    pipe->writers = 1;
    pipe->read_closed = false;
    pipe->write_closed = false;
    
    // 初始化同步原语
    pipe->lock.init();
    pipe->read_sem.init(0);              // 初始无数据可读
    pipe->write_sem.init(PIPE_BUFFER_SIZE);  // 初始全部空间可写
    
    // 分配 inode 号
    uint32_t inode = pipe_inode_counter++;
    
    // 创建读端节点
    fs_node_t *rnode = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!rnode) {
        LOG_ERROR_MSG("fs::Pipe::create: failed to allocate read node\n");
        kfree(pipe);
        return -1;
    }
    
    memset(rnode, 0, sizeof(fs_node_t));
    strcpy(rnode->name, "pipe_read");
    rnode->inode = inode;
    rnode->type = FS_PIPE;
    rnode->size = 0;
    rnode->permissions = 0644;
    rnode->uid = 0;
    rnode->gid = 0;
    rnode->impl = pipe;
    rnode->impl_data = 0;           // 标记为读端
    rnode->flags = FS_NODE_FLAG_ALLOCATED;
    rnode->ref_count = 1;
    
    // 设置读端操作函数
    rnode->ops = &pipe_read_ops;
    rnode->ptr = NULL;
    
    // 创建写端节点
    fs_node_t *wnode = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!wnode) {
        LOG_ERROR_MSG("fs::Pipe::create: failed to allocate write node\n");
        kfree(rnode);
        kfree(pipe);
        return -1;
    }
    
    memset(wnode, 0, sizeof(fs_node_t));
    strcpy(wnode->name, "pipe_write");
    wnode->inode = inode;           // 同一个 inode
    wnode->type = FS_PIPE;
    wnode->size = 0;
    wnode->permissions = 0644;
    wnode->uid = 0;
    wnode->gid = 0;
    wnode->impl = pipe;
    wnode->impl_data = 1;           // 标记为写端
    wnode->flags = FS_NODE_FLAG_ALLOCATED;
    wnode->ref_count = 1;
    
    // 设置写端操作函数
    wnode->ops = &pipe_write_ops;
    wnode->ptr = NULL;
    
    *read_node = rnode;
    *write_node = wnode;
    
    LOG_DEBUG_MSG("fs::Pipe::create: created pipe with inode %u\n", inode);
    
    return 0;
}

/**
 * 管道读取
 * 
 * 阻塞直到有数据可读，或者写端关闭
 */
static uint32_t pipe_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)offset;  // 管道不支持 offset
    
    if (!node || !buffer) {
        return 0;
    }
    
    pipe_t *pipe = (pipe_t *)node->impl;
    if (!pipe) {
        LOG_ERROR_MSG("pipe_read: pipe is NULL\n");
        return 0;
    }
    
    uint32_t bytes_read = 0;
    
    while (bytes_read < size) {
        // 等待数据可用
        pipe->lock.lock();
        
        while (pipe->count == 0) {
            // 如果写端已关闭且没有数据，返回 EOF
            if (pipe->write_closed) {
                pipe->lock.unlock();
                LOG_DEBUG_MSG("pipe_read: EOF (write_closed), read %u bytes\n", bytes_read);
                return bytes_read;
            }
            
            // 释放锁，等待数据
            pipe->lock.unlock();
            
            // 等待读信号量（有数据可读）
            pipe->read_sem.wait();
            
            // 重新获取锁
            pipe->lock.lock();
            
            // 再次检查写端是否关闭（可能在等待期间被关闭）
            if (pipe->count == 0 && pipe->write_closed) {
                pipe->lock.unlock();
                return bytes_read;
            }
        }
        
        // 计算本次可读取的数据量
        uint32_t to_read = size - bytes_read;
        if (to_read > pipe->count) {
            to_read = pipe->count;
        }
        
        // 读取数据（处理环形缓冲区回绕）
        for (uint32_t i = 0; i < to_read; i++) {
            buffer[bytes_read + i] = pipe->buffer[pipe->read_pos];
            pipe->read_pos = (pipe->read_pos + 1) % PIPE_BUFFER_SIZE;
            pipe->count--;
            
            // 通知写端有空间可用
            pipe->write_sem.signal();
        }
        
        bytes_read += to_read;
        
        pipe->lock.unlock();
        
        // 如果已读取到数据，可以返回（不必填满整个缓冲区）
        if (bytes_read > 0) {
            break;
        }
    }
    
    LOG_DEBUG_MSG("pipe_read: read %u bytes\n", bytes_read);
    return bytes_read;
}

/**
 * 管道写入
 * 
 * 阻塞直到有空间可写，或者读端关闭
 */
static uint32_t pipe_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)offset;  // 管道不支持 offset
    
    if (!node || !buffer) {
        return 0;
    }
    
    pipe_t *pipe = (pipe_t *)node->impl;
    if (!pipe) {
        LOG_ERROR_MSG("pipe_write: pipe is NULL\n");
        return 0;
    }
    
    uint32_t bytes_written = 0;
    
    // 检查读端是否已关闭
    pipe->lock.lock();
    if (pipe->read_closed) {
        pipe->lock.unlock();
        LOG_WARN_MSG("pipe_write: broken pipe (read_closed)\n");
        // 实际应该发送 SIGPIPE 信号
        return 0;
    }
    pipe->lock.unlock();
    
    while (bytes_written < size) {
        // 等待空间可用
        pipe->lock.lock();
        
        while (pipe->count == PIPE_BUFFER_SIZE) {
            // 如果读端已关闭，返回错误
            if (pipe->read_closed) {
                pipe->lock.unlock();
                LOG_WARN_MSG("pipe_write: broken pipe during write\n");
                return bytes_written;
            }
            
            // 释放锁，等待空间
            pipe->lock.unlock();
            
            // 等待写信号量（有空间可写）
            pipe->write_sem.wait();
            
            // 重新获取锁
            pipe->lock.lock();
            
            // 再次检查读端是否关闭
            if (pipe->read_closed) {
                pipe->lock.unlock();
                return bytes_written;
            }
        }
        
        // 计算本次可写入的数据量
        uint32_t to_write = size - bytes_written;
        uint32_t space_available = PIPE_BUFFER_SIZE - pipe->count;
        if (to_write > space_available) {
            to_write = space_available;
        }
        
        // 写入数据（处理环形缓冲区回绕）
        for (uint32_t i = 0; i < to_write; i++) {
            pipe->buffer[pipe->write_pos] = buffer[bytes_written + i];
            pipe->write_pos = (pipe->write_pos + 1) % PIPE_BUFFER_SIZE;
            pipe->count++;
            
            // 通知读端有数据可读
            pipe->read_sem.signal();
        }
        
        bytes_written += to_write;
        
        pipe->lock.unlock();
    }
    
    LOG_DEBUG_MSG("pipe_write: wrote %u bytes\n", bytes_written);
    return bytes_written;
}

/**
 * 关闭管道端
 * 
 * 根据 impl_data 判断是读端还是写端
 * 当两端都关闭时，释放管道资源
 */
static void pipe_close(fs_node_t *node) {
    if (!node) {
        return;
    }
    
    pipe_t *pipe = (pipe_t *)node->impl;
    if (!pipe) {
        LOG_ERROR_MSG("pipe_close: pipe is NULL\n");
        return;
    }
    
    bool is_write_end = (node->impl_data == 1);
    bool should_free = false;
    
    {
        sync::LockGuard guard(pipe->lock);
        if (is_write_end) {
            // 关闭写端
            pipe->writers--;
            LOG_DEBUG_MSG("pipe_close: closing write end, writers=%u\n", pipe->writers);
        
            if (pipe->writers == 0) {
                pipe->write_closed = true;
                // 唤醒所有等待读取的进程，让它们看到 EOF
                // 发送多个信号确保所有等待的读者被唤醒
                for (int i = 0; i < 10; i++) {
                    pipe->read_sem.signal();
                }
            }
        } else {
            // 关闭读端
            pipe->readers--;
            LOG_DEBUG_MSG("pipe_close: closing read end, readers=%u\n", pipe->readers);
        
            if (pipe->readers == 0) {
                pipe->read_closed = true;
                // 唤醒所有等待写入的进程，让它们看到错误
                for (int i = 0; i < 10; i++) {
                    pipe->write_sem.signal();
                }
            }
        }
    
        // 检查是否两端都已关闭
        should_free = (pipe->readers == 0 && pipe->writers == 0);
    }
    
    // 如果两端都关闭，释放管道资源
    if (should_free) {
        LOG_DEBUG_MSG("pipe_close: freeing pipe\n");
        // 清除节点对管道的引用（仅在释放时）
        node->impl = NULL;
        kfree(pipe);
    }
}

