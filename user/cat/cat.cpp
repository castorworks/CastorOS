// cat - 把文件内容写到标准输出；不给文件名时把标准输入抄到标准输出

#include <syscall.h>
#include <stdio.h>
#include <fs.h>

int main(int argc, char **argv) {
    char buf[512];
    long n;
    if (argc < 2) {
        while ((n = read_input(buf, sizeof(buf))) > 0) {
            write_out(buf, (size_t)n);
        }
        return 0;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        int fd = fs_open(argv[i], 0);
        if (fd < 0) {
            eprintf("cat: %s: no such file\n", argv[i]);
            status = 1;
            continue;
        }
        uint32_t offset = 0;
        while ((n = fs_read(fd, offset, buf, sizeof(buf))) > 0) {
            write_out(buf, (size_t)n);
            offset += (uint32_t)n;
        }
        fs_close(fd);
    }
    return status;
}
