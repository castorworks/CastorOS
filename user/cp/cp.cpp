// cp - 复制文件，可以跨文件系统：cp hello disk:hello

#include <stdio.h>
#include <fs.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        eprintf("usage: cp <from> <to>\n");
        return 1;
    }
    int in = fs_open(argv[1], 0);
    if (in < 0) {
        eprintf("cp: %s: no such file\n", argv[1]);
        return 1;
    }
    int out = fs_open(argv[2], FS_O_CREATE | FS_O_TRUNC);
    if (out < 0) {
        eprintf("cp: %s: cannot create\n", argv[2]);
        return 1;
    }

    static char buf[FS_BUF_SIZE];
    uint32_t offset = 0;
    long n;
    while ((n = fs_read(in, offset, buf, sizeof(buf))) > 0) {
        if (fs_write(out, offset, buf, (size_t)n) != n) {
            eprintf("cp: %s: write failed (disk full?)\n", argv[2]);
            return 1;
        }
        offset += (uint32_t)n;
    }
    fs_close(in);
    fs_close(out);
    return n < 0 ? 1 : 0;
}
