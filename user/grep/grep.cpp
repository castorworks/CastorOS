// grep - 只留下标准输入里含有指定文字的行
//
//   ls | grep hello        cat < notes | grep -v todo
//
// 只做子串匹配，没有正则表达式。-v 反过来：留下不含的行。

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    bool invert = argc > 1 && strcmp(argv[1], "-v") == 0;
    if (argc != (invert ? 3 : 2)) {
        eprintf("usage: grep [-v] <text>\n");
        return 2;
    }
    const char *text = argv[argc - 1];

    static char line[256];
    while (read_line(line, sizeof(line)) >= 0) {
        if ((strstr(line, text) != NULL) != invert) {
            printf("%s\n", line);
        }
    }
    return 0;
}
