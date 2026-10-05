// sleep - 睡指定的秒数然后退出；试后台任务和 Ctrl-C 时用得上

#include <syscall.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: sleep <seconds>\n");
        return 1;
    }
    sleep((unsigned)atoi(argv[1]));
    return 0;
}
