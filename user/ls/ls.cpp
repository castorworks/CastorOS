// ls - 列出文件服务里的文件

#include <stdio.h>
#include <fs.h>

int main() {
    char name[FS_NAME_MAX];
    uint32_t size;
    for (int i = 0; fs_list(i, name, &size) == 0; i++) {
        printf("%8u  %s\n", size, name);
    }
    return 0;
}
