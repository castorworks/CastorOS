#ifndef _FS_BLOCKDEV_H_
#define _FS_BLOCKDEV_H_

#include <types.h>

/**
 * 块设备抽象层
 * 
 * 提供统一的块设备访问接口，支持：
 * - 按块（sector）读写
 * - 设备大小查询
 * - 块大小查询
 */

// 块设备配置
#define BLOCKDEV_MAX_DEVICES 32

namespace fs { class BlockdevOps; struct Blockdev; }

namespace fs {

/**
 * @brief 块设备操作接口
 *
 * 每个块设备驱动提供一个无状态的实现对象，fs::Blockdev::ops 指向它。
 * dev 参数是 fs::Blockdev::private_data。
 */
class BlockdevOps {
public:
    virtual int read(void *dev, uint32_t sector, uint32_t count, uint8_t *buffer) const = 0;
    virtual int write(void *dev, uint32_t sector, uint32_t count, const uint8_t *buffer) const = 0;

    /** @brief 总扇区数；默认使用注册时填写的 total_sectors */
    virtual uint32_t get_size(const fs::Blockdev *bdev) const;
    /** @brief 块大小（字节）；默认使用注册时填写的 block_size */
    virtual uint32_t get_block_size(const fs::Blockdev *bdev) const;

protected:
    /* 实现对象都是静态存储期的单例，不会通过基类指针销毁 */
    ~BlockdevOps() = default;
};

} // namespace fs

namespace fs {

/**
 * 块设备结构
 */
struct Blockdev {
    char name[64];                      // 设备名称
    void *private_data;                 // 设备私有数据
    uint32_t block_size;                // 块大小（字节），通常是 512
    uint32_t total_sectors;             // 总扇区数
    uint32_t ref_count;                 // 引用计数
    bool registered;                    // 是否已注册
    
    const fs::BlockdevOps *ops;         // 设备操作（虚函数接口，见上方 fs::BlockdevOps）

    /**
     * 读取块设备
     * @param dev 块设备
     * @param sector 起始扇区号
     * @param count 要读取的扇区数
     * @param buffer 缓冲区（必须至少能容纳 count * block_size 字节）
     * @return 0 成功，-1 失败
     */
    static int read(Blockdev *dev, uint32_t sector, uint32_t count, uint8_t *buffer);

    /**
     * 写入块设备
     * @param dev 块设备
     * @param sector 起始扇区号
     * @param count 要写入的扇区数
     * @param buffer 数据缓冲区
     * @return 0 成功，-1 失败
     */
    static int write(Blockdev *dev, uint32_t sector, uint32_t count, const uint8_t *buffer);

    /**
     * 获取块设备总大小（扇区数）
     * @param dev 块设备
     * @return 总扇区数
     */
    static uint32_t get_size(Blockdev *dev);

    /**
     * 获取块设备块大小（字节）
     * @param dev 块设备
     * @return 块大小（字节）
     */
    static uint32_t get_block_size(Blockdev *dev);

    /**
     * 获取块设备总大小（字节）
     * @param dev 块设备
     * @return 总大小（字节）
     */
    static uint64_t get_size_bytes(Blockdev *dev) {
        return (uint64_t)get_size(dev) * get_block_size(dev);
    }

    /**
     * 注册块设备
     * @param dev 块设备
     * @return 0 成功，-1 失败
     */
    static int register_device(Blockdev *dev);

    /**
     * 注销块设备
     * @param dev 块设备
     */
    static void unregister_device(Blockdev *dev);

    /**
     * 按名称查找块设备
     * @param name 设备名称
     * @return 块设备指针，未找到返回 NULL
     */
    static Blockdev *get_by_name(const char *name);

    /**
     * 增加块设备引用计数
     * @param dev 块设备
     * @return 块设备指针
     */
    static Blockdev *retain(Blockdev *dev);

    /**
     * 释放块设备引用
     * @param dev 块设备
     */
    static void release(Blockdev *dev);
};

} // namespace fs

#endif // _FS_BLOCKDEV_H_
