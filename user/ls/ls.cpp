// ls - 列出一个目录里有什么
//
//   ls              当前目录
//   ls /bin         从根写起的路径
//   ls notes        当前目录里的 notes 目录
//
// 每行一项：大小和名字；目录的名字后面带一个 '/'。

#include <stdio.h>
#include <string.h>
#include <fs.h>

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "";
    char name[FS_NAME_MAX];
    uint32_t size;
    bool is_dir;
    int i = 0;
    for (; fs_list(dir, i, name, &size, &is_dir) == 0; i++) {
        if (is_dir) {
            printf("%8s  %s/\n", "", name);
        } else {
            printf("%8u  %s\n", size, name);
        }
    }
    if (i == 0 && argc > 1) {
        // 空目录和"没有这个目录"在 fs_list 那里都是"没有第 0 项"：再问一下它上一级才分得清，
        // 这里只说看到的事实
        printf("(nothing in %s)\n", dir);
    }
    return 0;
}
