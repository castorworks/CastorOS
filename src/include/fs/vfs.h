#ifndef _FS_VFS_H_
#define _FS_VFS_H_

#include <types.h>

/**
 * 虚拟文件系统（VFS）
 * 
 * 提供统一的文件系统接口层
 */

// 文件类型
typedef enum {
    FS_FILE,
    FS_DIRECTORY,
    FS_CHARDEVICE,
    FS_BLOCKDEVICE,
    FS_PIPE,
    FS_SYMLINK,
} fs_node_type_t;

// 文件权限（内核使用）
#define FS_PERM_READ    0x4
#define FS_PERM_WRITE   0x2
#define FS_PERM_EXEC    0x1

// 文件节点标志（用于 flags 字段）
#define FS_NODE_FLAG_ALLOCATED      0x80000000  // 节点是动态分配的，需要释放
#define FS_NODE_FLAG_UNLINKED       0x40000000  // 已从目录摘除，最后一次关闭时销毁

/* 前向声明 */
struct fs_node;

/* 文件操作函数类型 */

namespace fs { class NodeOps; }

/**
 * 文件节点（inode）
 * 表示一个文件或目录
 */
typedef struct fs_node {
    char name[128];              // 文件名
    uint32_t inode;              // inode 编号
    fs_node_type_t type;         // 文件类型
    uint32_t size;               // 文件大小
    uint32_t permissions;        // 权限
    uint32_t uid;                // 用户 ID
    uint32_t gid;                // 组 ID
    uint32_t flags;              // 标志位
    void *impl;                  // 文件系统私有数据指针（如 fat32_file_t*），释放节点时会 kfree
    uint32_t impl_data;          // 文件系统私有整数值（如 procfs 的 PID），不会被 kfree
    uint32_t ref_count;          // 引用计数（用于资源管理）
    uint32_t open_count;         // 指向本节点的打开文件描述符数（fs::Vfs::pin/unpin 维护）

    // 节点操作（虚函数接口，见下方 fs::NodeOps）；NULL 表示不支持任何操作
    const fs::NodeOps *ops;

    struct fs_node *ptr;         // 用于符号链接和挂载点
} fs_node_t;

namespace fs {

/**
 * @brief 节点操作接口
 *
 * 每种节点（ramfs 文件、ramfs 目录、/dev/null、/proc/meminfo ...）提供一个无状态的
 * 实现对象，fs_node_t::ops 指向它。supported() 返回该节点类型实际支持的操作集合，
 * VFS 在调用前据此判断；未重写的操作保持“不支持”的默认实现。
 */
class NodeOps {
public:
    enum Op : uint32_t {
        OP_READ     = 1u << 0,
        OP_WRITE    = 1u << 1,
        OP_OPEN     = 1u << 2,
        OP_CLOSE    = 1u << 3,
        OP_READDIR  = 1u << 4,
        OP_FINDDIR  = 1u << 5,
        OP_CREATE   = 1u << 6,
        OP_MKDIR    = 1u << 7,
        OP_UNLINK   = 1u << 8,
        OP_TRUNCATE = 1u << 9,
        OP_RENAME   = 1u << 10,
    };

    /** @brief 该节点类型支持的操作（Op 位的组合） */
    virtual uint32_t supported() const = 0;
    bool supports(Op op) const { return (supported() & op) != 0; }

    virtual uint32_t read(fs_node_t *, uint32_t, uint32_t, uint8_t *) const { return 0; }
    virtual uint32_t write(fs_node_t *, uint32_t, uint32_t, uint8_t *) const { return 0; }
    virtual void open(fs_node_t *, uint32_t) const {}
    virtual void close(fs_node_t *) const {}
    virtual struct dirent *readdir(fs_node_t *, uint32_t) const { return nullptr; }
    virtual fs_node_t *finddir(fs_node_t *, const char *) const { return nullptr; }
    virtual int create(fs_node_t *, const char *) const { return -1; }
    virtual int mkdir(fs_node_t *, const char *, uint32_t) const { return -1; }
    virtual int unlink(fs_node_t *, const char *) const { return -1; }
    virtual int truncate(fs_node_t *, uint32_t) const { return -1; }
    virtual int rename(fs_node_t *, const char *, const char *) const { return -1; }

    /**
     * @brief 释放 node->impl（动态节点的引用计数归零时由 VFS 调用）
     *
     * 默认 kfree(impl)。impl 被多个节点共享的类型（管道两端）必须重写，
     * 否则先归零的一端会把另一端还在用的数据释放掉。
     */
    virtual void release_impl(fs_node_t *node) const;

    /**
     * @brief 销毁一个已被 unlink 的节点及其数据
     *
     * 节点带 FS_NODE_FLAG_UNLINKED 且最后一个文件描述符关闭时由 VFS 调用。
     * 支持“打开状态下删除”的文件系统在这里释放节点；调用后节点不再有效。
     */
    virtual void destroy(fs_node_t *) const {}

protected:
    /* 实现对象都是静态存储期的单例，不会通过基类指针销毁 */
    ~NodeOps() = default;
};

/** @brief 节点是否支持某个操作（node 或 node->ops 为空时返回 false） */
inline bool node_supports(const fs_node_t *node, NodeOps::Op op) {
    return node && node->ops && node->ops->supports(op);
}

} // namespace fs

namespace fs {

/**
 * @brief 虚拟文件系统（路径解析、挂载与统一的文件操作入口）
 */
class Vfs {
public:
    /**
     * 初始化 VFS
     */
    static void init();

    /**
     * 获取根文件系统
     * @return 根文件系统节点
     */
    static fs_node_t *get_root();

    /**
     * 设置根文件系统
     * @param root 根文件系统节点
     */
    static void set_root(fs_node_t *root);

    /**
     * 读取文件
     * @param node 文件节点
     * @param offset 偏移量
     * @param size 读取大小
     * @param buffer 缓冲区
     * @return 实际读取的字节数
     */
    static uint32_t read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);

    /**
     * 写入文件
     * @param node 文件节点
     * @param offset 偏移量
     * @param size 写入大小
     * @param buffer 数据缓冲区
     * @return 实际写入的字节数
     */
    static uint32_t write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);

    /**
     * 打开文件
     * @param node 文件节点
     * @param flags 打开标志
     */
    static void open(fs_node_t *node, uint32_t flags);

    /**
     * 关闭文件
     * @param node 文件节点
     */
    static void close(fs_node_t *node);

    /**
     * 增加文件节点引用计数
     * @param node 文件节点
     */
    static void ref_node(fs_node_t *node);

    /**
     * 为一个已缓存的节点再取一个引用（文件系统的 in-core 节点表使用）
     * @return false 表示节点是动态分配的且引用计数已经归零（正在被释放），
     *         调用者不能再使用它
     */
    static bool try_ref_node(fs_node_t *node);

    /**
     * 减少文件节点引用计数并在计数为0时释放
     * 如果节点是动态分配的（flags & FS_NODE_FLAG_ALLOCATED）且引用计数为0，则释放它
     * @param node 文件节点
     */
    static void release_node(fs_node_t *node);

    /**
     * 记录一个文件描述符开始引用节点（所有节点类型都计数）
     */
    static void pin(fs_node_t *node);

    /**
     * 一个文件描述符不再引用节点
     * @return true 表示节点已被销毁（之前被 unlink，且这是最后一个引用），
     *         调用者不得再访问它
     */
    static bool unpin(fs_node_t *node);

    /**
     * 节点当前是否被文件描述符引用
     */
    static bool is_pinned(fs_node_t *node);

    /**
     * 读取目录项
     * @param node 目录节点
     * @param index 索引
     * @return 目录项，没有更多时返回 NULL
     */
    static struct dirent *readdir(fs_node_t *node, uint32_t index);

    /**
     * 在目录中查找文件
     * @param node 目录节点
     * @param name 文件名
     * @return 文件节点（ref_count=1，调用者需要调用 vfs_release_node），未找到返回 NULL
     */
    static fs_node_t *finddir(fs_node_t *node, const char *name);

    /**
     * 路径解析
     * @param path 路径字符串
     * @return 文件节点（对于动态分配的节点 ref_count=1，调用者需要调用 vfs_release_node），未找到返回 NULL
     */
    static fs_node_t *path_to_node(const char *path);

    /**
     * 创建文件
     * @param path 文件路径
     * @return 0 成功，-1 失败
     */
    static int create(const char *path);

    /**
     * 创建目录
     * @param path 目录路径
     * @param permissions 权限
     * @return 0 成功，-1 失败
     */
    static int mkdir(const char *path, uint32_t permissions);

    /**
     * 删除文件或目录
     * @param path 文件路径
     * @return 0 成功，-1 失败
     */
    static int unlink(const char *path);

    /**
     * 截断文件到指定大小
     * @param node 文件节点
     * @param new_size 新的文件大小
     * @return 0 成功，-1 失败
     */
    static int truncate(fs_node_t *node, uint32_t new_size);

    /**
     * 挂载文件系统到指定路径
     * @param path 挂载点路径（必须是已存在的目录）
     * @param root 要挂载的文件系统根节点
     * @return 0 成功，-1 失败
     */
    static int mount(const char *path, fs_node_t *root);

    /**
     * 重命名文件或目录
     * @param oldpath 原路径
     * @param newpath 新路径
     * @return 0 成功，-1 失败
     * 
     * 注意：当前仅支持同一目录下的重命名
     */
    static int rename(const char *oldpath, const char *newpath);
};

} // namespace fs

#endif // _FS_VFS_H_
