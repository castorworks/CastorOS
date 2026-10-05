// disk - 直接读写块设备扇区的小工具
//
//   disk                    显示容量
//   disk read <sector>      把扇区开头的文字打印出来
//   disk write <sector> <words...>   把文字写到扇区开头

#include <stdio.h>
#include <string.h>
#include <blk.h>

static char sector_buf[BLK_SECTOR_SIZE];

int main(int argc, char **argv) {
    uint64_t sectors = blk_capacity();
    if (sectors == 0) {
        eprintf("disk: no block device\n");
        return 1;
    }
    if (argc < 3) {
        printf("%u sectors of %d bytes (%u MB)\n", (uint32_t)sectors, BLK_SECTOR_SIZE,
               (uint32_t)(sectors / 2048));
        eprintf("usage: disk read <sector> | disk write <sector> <words...>\n");
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
    eprintf("usage: disk read <sector> | disk write <sector> <words...>\n");
    return 1;
}
