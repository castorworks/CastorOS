/**
 * 块设备服务的客户端：把请求发给登记为 "blk" 的驱动进程，数据经共享缓冲区传递
 */

#include <blk.h>
#include <names.h>
#include <syscall.h>
#include <string.h>

static int blk_server = 0;
static char *blk_buf = NULL;
static int blk_owner = 0;       // 建立这条连接的进程：fork 出来的子进程要自己重新建立

/** 确保当前进程与驱动之间有共享缓冲区。没有驱动时立刻失败（不等待） */
static bool blk_connect(void) {
    int self = getpid();
    if (blk_buf && blk_owner == self) {
        return true;
    }

    int server = name_lookup(BLK_SERVICE_NAME);
    if (server <= 0) {
        return false;
    }
    char *buf = (char *)mmap(NULL, BLK_BUF_SIZE, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) {
        return false;
    }
    if (mem_grant(server, buf, BLK_BUF_SIZE) != 0) {
        munmap(buf, BLK_BUF_SIZE);
        return false;
    }

    blk_server = server;
    blk_buf = buf;
    blk_owner = self;
    return true;
}

uint64_t blk_capacity(void) {
    if (!blk_connect()) {
        return 0;
    }
    struct ipc_msg m = {};
    m.label = BLK_INFO;
    if (ipc_call(blk_server, &m) != 0 || m.data[0] != 0) {
        return 0;
    }
    return m.data[1];
}

/** 读或写：每次最多一个缓冲区的量 */
static int blk_transfer(uint32_t label, uint64_t sector, char *buf, uint32_t count) {
    if (!blk_connect()) {
        return -1;
    }
    const uint32_t max = BLK_BUF_SIZE / BLK_SECTOR_SIZE;
    while (count > 0) {
        uint32_t n = count > max ? max : count;
        size_t bytes = (size_t)n * BLK_SECTOR_SIZE;
        if (label == BLK_WRITE) {
            memcpy(blk_buf, buf, bytes);
        }
        struct ipc_msg m = {};
        m.label = label;
        m.data[0] = sector;
        m.data[1] = n;
        if (ipc_call(blk_server, &m) != 0 || m.data[0] != 0) {
            return -1;
        }
        if (label == BLK_READ) {
            memcpy(buf, blk_buf, bytes);
        }
        sector += n;
        buf += bytes;
        count -= n;
    }
    return 0;
}

int blk_read(uint64_t sector, void *buf, uint32_t count) {
    return blk_transfer(BLK_READ, sector, (char *)buf, count);
}

int blk_write(uint64_t sector, const void *buf, uint32_t count) {
    return blk_transfer(BLK_WRITE, sector, (char *)buf, count);
}
