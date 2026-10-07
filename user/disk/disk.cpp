// disk - 直接读写块设备扇区的小工具
//
//   disk                    显示容量
//   disk read <sector>      把扇区开头的文字打印出来
//   disk write <sector> <words...>   把文字写到扇区开头
//   disk erase              把磁盘开头清零：下次启动时 diskfs 把它当成空盘重新格式化

#include <stdio.h>
#include <string.h>
#include <blk.h>

static char sector_buf[BLK_SECTOR_SIZE];

/** diskfs 只格式化空白的盘（第一个 4KB 的块全是 0）：清掉它，原来的内容就不再被当回事 */
#define ERASE_SECTORS   8

static int erase(void) {
    printf("This makes everything on the disk unreachable. Type erase to go on: ");
    char answer[16];
    if (read_line(answer, sizeof(answer)) < 0 || strcmp(answer, "erase") != 0) {
        eprintf("disk: nothing erased\n");
        return 1;
    }
    memset(sector_buf, 0, sizeof(sector_buf));
    for (uint32_t s = 0; s < ERASE_SECTORS; s++) {
        if (blk_write(s, sector_buf, 1) != 0) {
            eprintf("disk: write of sector %u failed\n", s);
            return 1;
        }
    }
    printf("erased; restart to let diskfs format the disk\n");
    return 0;
}

int main(int argc, char **argv) {
    uint64_t sectors = blk_capacity();
    if (sectors == 0) {
        eprintf("disk: no block device\n");
        return 1;
    }
    if (argc == 2 && strcmp(argv[1], "erase") == 0) {
        return erase();
    }
    if (argc < 3) {
        printf("%u sectors of %d bytes (%u MB)\n", (uint32_t)sectors, BLK_SECTOR_SIZE,
               (uint32_t)(sectors / 2048));
        eprintf("usage: disk read <sector> | disk write <sector> <words...> | disk erase\n");
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
    eprintf("usage: disk read <sector> | disk write <sector> <words...> | disk erase\n");
    return 1;
}
