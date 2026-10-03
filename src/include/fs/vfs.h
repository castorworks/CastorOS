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

/* 前向声明 */
struct fs_node;

/* 文件操作函数类型 */
typedef uint32_t (*read_type_t)(struct fs_node *node, uint32_t offset, uint32_t size, uint8_t *buffer);
typedef uint32_t (*write_type_t)(struct fs_node *node, uint32_t offset, uint32_t size, uint8_t *buffer);
typedef void (*open_type_t)(struct fs_node *node, uint32_t flags);
typedef void (*close_type_t)(struct fs_node *node);
typedef struct dirent *(*readdir_type_t)(struct fs_node *node, uint32_t index);
typedef struct fs_node *(*finddir_type_t)(struct fs_node *node, const char *name);
typedef int (*create_type_t)(struct fs_node *node, const char *name);
typedef int (*mkdir_type_t)(struct fs_node *node, const char *name, uint32_t permissions);
typedef int (*unlink_type_t)(struct fs_node *node, const char *name);
typedef int (*truncate_type_t)(struct fs_node *node, uint32_t new_size);
typedef int (*rename_type_t)(struct fs_node *node, const char *old_name, const char *new_name);

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

    // 文件操作函数
    read_type_t read;
    write_type_t write;
    open_type_t open;
    close_type_t close;
    readdir_type_t readdir;
    finddir_type_t finddir;
    create_type_t create;
    mkdir_type_t mkdir;
    unlink_type_t unlink;
    truncate_type_t truncate;
    rename_type_t rename;        // 重命名操作

    struct fs_node *ptr;         // 用于符号链接和挂载点
} fs_node_t;

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
     * 减少文件节点引用计数并在计数为0时释放
     * 如果节点是动态分配的（flags & FS_NODE_FLAG_ALLOCATED）且引用计数为0，则释放它
     * @param node 文件节点
     */
    static void release_node(fs_node_t *node);

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
