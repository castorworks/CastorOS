// cat - 把文件内容打印到控制台

#include <syscall.h>
#include <stdio.h>
#include <fs.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: cat <file>...\n");
        return 1;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        int fd = fs_open(argv[i], 0);
        if (fd < 0) {
            printf("cat: %s: no such file\n", argv[i]);
            status = 1;
            continue;
        }
        char buf[512];
        uint32_t offset = 0;
        long n;
        while ((n = fs_read(fd, offset, buf, sizeof(buf))) > 0) {
            console_write(buf, (size_t)n);
            offset += (uint32_t)n;
        }
        fs_close(fd);
    }
    return status;
}
