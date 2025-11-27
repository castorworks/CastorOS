// hello.c - 简单的 Hello World 用户程序
// 这个程序演示如何从 FAT32 文件系统加载并执行外部程序

#include <syscall.h>
#include <stdio.h>
#include <time.h>

// 主函数
void _start(void) {
    printf("Hello from hello.elf!\n");
    printf("My PID: %d, Parent PID: %d\n", getpid(), getppid());

    for (int i = 0; i < 5; i++) {
        printf("%d seconds passed\n", i + 1);
        sleep(1);
    }

    printf("Goodbye!\n");
    exit(0);
}
