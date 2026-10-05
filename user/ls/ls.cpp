// ls - 列出文件服务里的文件
//
//   ls          内存文件系统
//   ls disk:    磁盘文件系统

#include <stdio.h>
#include <fs.h>

int main(int argc, char **argv) {
    const char *where = argc > 1 ? argv[1] : "";
    char name[FS_NAME_MAX];
    uint32_t size;
    int i = 0;
    for (; fs_list(where, i, name, &size) == 0; i++) {
        printf("%8u  %s%s\n", size, where, name);
    }
    if (i == 0 && argc > 1) {
        printf("(nothing in %s)\n", where);
    }
    return 0;
}
