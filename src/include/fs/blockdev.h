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

// 块设备操作函数类型
typedef int (*blockdev_read_t)(void *dev, uint32_t sector, uint32_t count, uint8_t *buffer);
typedef int (*blockdev_write_t)(void *dev, uint32_t sector, uint32_t count, const uint8_t *buffer);
typedef uint32_t (*blockdev_get_size_t)(void *dev);  // 返回扇区数
typedef uint32_t (*blockdev_get_block_size_t)(void *dev);  // 返回块大小（字节）

/**
 * 块设备结构
 */
typedef struct blockdev {
    char name[64];                      // 设备名称
    void *private_data;                 // 设备私有数据
    uint32_t block_size;                // 块大小（字节），通常是 512
    uint32_t total_sectors;             // 总扇区数
    uint32_t ref_count;                 // 引用计数
    bool registered;                    // 是否已注册
    
    // 操作函数
    blockdev_read_t read;
    blockdev_write_t write;
    blockdev_get_size_t get_size;
    blockdev_get_block_size_t get_block_size;
} blockdev_t;

/**
 * 读取块设备
 * @param dev 块设备
 * @param sector 起始扇区号
 * @param count 要读取的扇区数
 * @param buffer 缓冲区（必须至少能容纳 count * block_size 字节）
 * @return 0 成功，-1 失败
 */
int blockdev_read(blockdev_t *dev, uint32_t sector, uint32_t count, uint8_t *buffer);

/**
 * 写入块设备
 * @param dev 块设备
 * @param sector 起始扇区号
 * @param count 要写入的扇区数
 * @param buffer 数据缓冲区
 * @return 0 成功，-1 失败
 */
int blockdev_write(blockdev_t *dev, uint32_t sector, uint32_t count, const uint8_t *buffer);

/**
 * 获取块设备总大小（扇区数）
 * @param dev 块设备
 * @return 总扇区数
 */
uint32_t blockdev_get_size(blockdev_t *dev);

/**
 * 获取块设备块大小（字节）
 * @param dev 块设备
 * @return 块大小（字节）
 */
uint32_t blockdev_get_block_size(blockdev_t *dev);

/**
 * 获取块设备总大小（字节）
 * @param dev 块设备
 * @return 总大小（字节）
 */
static inline __attribute__((unused)) uint64_t blockdev_get_size_bytes(blockdev_t *dev) {
    return (uint64_t)blockdev_get_size(dev) * blockdev_get_block_size(dev);
}

/**
 * 注册块设备
 * @param dev 块设备
 * @return 0 成功，-1 失败
 */
int blockdev_register(blockdev_t *dev);

/**
 * 注销块设备
 * @param dev 块设备
 */
void blockdev_unregister(blockdev_t *dev);

/**
 * 按名称查找块设备
 * @param name 设备名称
 * @return 块设备指针，未找到返回 NULL
 */
blockdev_t *blockdev_get_by_name(const char *name);

/**
 * 增加块设备引用计数
 * @param dev 块设备
 * @return 块设备指针
 */
blockdev_t *blockdev_retain(blockdev_t *dev);

/**
 * 释放块设备引用
 * @param dev 块设备
 */
void blockdev_release(blockdev_t *dev);

#endif // _FS_BLOCKDEV_H_

