// hello.c - 简单的 Hello World 用户程序
// 这个程序演示如何从 FAT32 文件系统加载并执行外部程序

#include <syscall.h>

// 主函数
void _start(void) {
    print("Hello, World!\n");
    exit(0);
}
