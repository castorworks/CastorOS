// init - 第一个用户进程
//
// 以 ELF 映像的形式嵌入内核（src/kernel/init_image.S），由内核在启动时加载。
// 内核只提供进程、内存和调试控制台；这里演示这三类调用，然后回显控制台输入。

#include <syscall.h>
#include <stdio.h>

int main() {
    printf("init: started, pid=%d\n", getpid());

    // 内存：匿名映射
    char *page = (char *)mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) {
        printf("init: mmap failed\n");
        return 1;
    }
    page[0] = 42;

    // 进程：fork + waitpid，子进程看到写时复制的页
    int pid = fork();
    if (pid == 0) {
        printf("init: child pid=%d ppid=%d sees %d\n", getpid(), getppid(), page[0]);
        sleep(1);
        exit(7);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    printf("init: child %d exited with %d\n", pid, WEXITSTATUS(status));
    munmap(page, 4096);

    printf("init: ready, echoing console input\n");
    for (;;) {
        char c = (char)getchar();
        console_write(&c, 1);
    }
}
