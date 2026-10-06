// mv - 改名或移动
//
//   mv old new              改名
//   mv notes/a.txt done     目标是一个已有的目录：移进去，名字不变（done/a.txt）
//   mv notes archive        目录也可以，里面的东西跟着走
//
// 两个路径要在同一个文件系统里；跨文件系统用 cp 再 rm。

#include <stdio.h>
#include <string.h>
#include <fs.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        eprintf("usage: mv <from> <to>\n");
        return 1;
    }
    const char *from = argv[1];
    const char *to = argv[2];

    // 目标是已有的目录的话，移到它里面去，保持原来的名字。"能换进去"就说明它是目录
    char target[2 * FS_NAME_MAX];
    char here[FS_NAME_MAX + 8];
    fs_getcwd(here);
    if (fs_chdir(to) == 0) {
        fs_chdir(here);
        const char *leaf = from;
        for (const char *p = from; *p; p++) {
            if ((*p == '/' || *p == ':') && p[1] != '\0') {
                leaf = p + 1;
            }
        }
        size_t n = strlen(to);
        snprintf(target, sizeof(target), "%s%s%s", to, n > 0 && (to[n - 1] == '/' || to[n - 1] == ':') ? "" : "/", leaf);
        to = target;
    }

    if (fs_rename(from, to) != 0) {
        eprintf("mv: cannot move %s to %s\n", from, to);
        return 1;
    }
    return 0;
}
