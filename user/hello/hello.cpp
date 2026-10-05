// hello - 最小的示例程序：放在启动文件系统里，在命令行里输入 hello 运行

#include <syscall.h>
#include <stdio.h>

int main(int argc, char **argv) {
    printf("hello from pid %d (parent %d)\n", getpid(), getppid());
    for (int i = 1; i < argc; i++) {
        printf("  argv[%d] = %s\n", i, argv[i]);
    }
    return 0;
}
