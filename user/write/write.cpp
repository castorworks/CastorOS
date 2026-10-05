// write - 把文字写进文件
//
//   write <file> <text...>   把参数里的文字写成一行
//   write <file>             从键盘读，每行写进文件，行首按 Ctrl-D 结束

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <console.h>
#include <fs.h>

static int fd;
static uint32_t offset = 0;

static bool put(const char *text, size_t len) {
    if (fs_write(fd, offset, text, len) != (long)len) {
        return false;
    }
    offset += (uint32_t)len;
    return true;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: write <file> [text]\n");
        return 1;
    }
    fd = fs_open(argv[1], FS_O_CREATE | FS_O_TRUNC);
    bool ok = fd >= 0;

    if (argc > 2) {
        for (int i = 2; ok && i < argc; i++) {
            ok = put(argv[i], strlen(argv[i])) && put(i + 1 < argc ? " " : "\n", 1);
        }
    } else if (ok) {
        printf("(type lines; Ctrl-D on an empty line ends)\n");
        static char line[256];
        long len;
        while (ok && (len = read_line(line, sizeof(line))) >= 0) {
            ok = put(line, (size_t)len) && put("\n", 1);
        }
    }

    if (fd >= 0) {
        fs_close(fd);
    }
    if (!ok) {
        printf("write: %s: failed\n", argv[1]);
        return 1;
    }
    return 0;
}
