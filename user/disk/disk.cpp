// disk - 直接读写块设备扇区的小工具
//
//   disk                    列出每块盘的容量
//   disk read <sector>      把扇区开头的文字打印出来
//   disk write <sector> <words...>   把文字写到扇区开头
//   disk <n> read ... / disk <n> write ...   对第 n 块盘做（不写就是第 0 块）

#include <stdio.h>
#include <string.h>
#include <blk.h>

static char sector_buf[BLK_SECTOR_SIZE];

#define USAGE "usage: disk [n] read <sector> | disk [n] write <sector> <words...>\n"

int main(int argc, char **argv) {
    int disks = blk_count();
    if (disks == 0) {
        eprintf("disk: no block device\n");
        return 1;
    }
    // 第一个参数是数字的话，它是磁盘的编号
    if (argc >= 2 && argv[1][0] >= '0' && argv[1][0] <= '9') {
        int which = atoi(argv[1]);
        if (which >= disks) {
            eprintf("disk: there is no disk %d\n", which);
            return 1;
        }
        blk_select(which);
        argv++;
        argc--;
    }
    if (argc < 3) {
        for (int i = 0; i < disks; i++) {
            blk_select(i);
            uint64_t sectors = blk_capacity();
            printf("disk %d: %u sectors of %d bytes (%u MB)\n", i, (uint32_t)sectors, BLK_SECTOR_SIZE,
                   (uint32_t)(sectors / 2048));
        }
        eprintf(USAGE);
        return 0;
    }

    uint32_t sector = (uint32_t)atoi(argv[2]);
    if (strcmp(argv[1], "read") == 0) {
        if (blk_read(sector, sector_buf, 1) != 0) {
            eprintf("disk: read of sector %u failed\n", sector);
            return 1;
        }
        sector_buf[BLK_SECTOR_SIZE - 1] = '\0';
        printf("%s\n", sector_buf[0] ? sector_buf : "(empty)");
        return 0;
    }
    if (strcmp(argv[1], "write") == 0 && argc >= 4) {
        memset(sector_buf, 0, sizeof(sector_buf));
        size_t len = 0;
        for (int i = 3; i < argc; i++) {
            size_t n = strlen(argv[i]);
            if (len + n + 2 > sizeof(sector_buf)) {
                break;
            }
            if (i > 3) {
                sector_buf[len++] = ' ';
            }
            memcpy(sector_buf + len, argv[i], n);
            len += n;
        }
        if (blk_write(sector, sector_buf, 1) != 0) {
            eprintf("disk: write of sector %u failed\n", sector);
            return 1;
        }
        return 0;
    }
    eprintf(USAGE);
    return 1;
}
