#ifndef _USERLAND_LIB_BLK_H_
#define _USERLAND_LIB_BLK_H_

#include <types.h>

// 块设备服务：协议和客户端接口。
//
// 驱动进程以 "blk" 登记（目前是 user/blk，virtio-blk）。数据经客户与驱动之间的
// 共享缓冲区传递，和文件服务的做法一样：客户第一次使用时把缓冲区 mem_grant 给驱动。

#define BLK_SERVICE_NAME    "blk"

#define BLK_SECTOR_SIZE     512

/** 共享缓冲区大小，也是单次请求能传输的最大字节数（8 个扇区） */
#define BLK_BUF_SIZE        4096

// 请求的 label。应答的 data[0] 是结果（0 成功，负数失败，按 int64_t 解释）
enum {
    BLK_INFO  = 1,  // 应答 data[1]: 扇区总数
    BLK_READ  = 2,  // data[0]: 起始扇区, data[1]: 扇区数 (<= 8)。内容在缓冲区
    BLK_WRITE = 3,  // data[0]: 起始扇区, data[1]: 扇区数 (<= 8)；缓冲区: 内容
};

/** 磁盘的扇区总数；没有块设备服务时返回 0 */
uint64_t blk_capacity(void);

/** 读/写 count 个扇区（任意数量，库会拆成多个请求）。@return 0 成功，-1 失败 */
int blk_read(uint64_t sector, void *buf, uint32_t count);
int blk_write(uint64_t sector, const void *buf, uint32_t count);

#endif // _USERLAND_LIB_BLK_H_
