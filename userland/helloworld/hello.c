// hello.c - 简单的 Hello World 用户程序
// 这个程序演示如何从 FAT32 文件系统加载并执行外部程序

#include <syscall.h>
#include <time.h>

// 简单的字符串输出函数（使用系统调用）
void puts(const char *str) {
    int i = 0;
    while (str[i]) {
        syscall3(SYS_WRITE, 1, (uint32_t)&str[i], 1);
        i++;
    }
}

// 主函数
void _start(void) {
    puts("Hello from hello.elf!\n");
    
    // Sleep 3 次，每次 1 秒
    for (int i = 0; i < 60; i++) {
        sleep(1);
        puts("1 second passed\n");
    }
    
    puts("Goodbye!\n");
    exit(0);
}
