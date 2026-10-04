// init - 第一个用户进程
//
// 以 ELF 映像的形式嵌入内核（src/kernel/init_image.S），由内核在启动时加载。
// 内核只提供进程、内存、IPC 和给驱动用的硬件访问。init 依次演示它们，
// 启动用户态串口驱动（user/uart），然后把驱动送来的输入回显到控制台。

#include <syscall.h>
#include <stdio.h>
#include <uart.h>

// 模块映像（modules.S）
extern "C" const char uart_image_start[];
extern "C" const char uart_image_end[];

// 演示用的“服务”协议
enum {
    ADD_REQUEST = 1,    // data[0] + data[1] -> data[0]
    ADD_QUIT    = 2,
};

// 一个最小的服务进程：收请求、处理、把应答发回给请求者
static void add_server(void) {
    struct ipc_msg m;
    for (;;) {
        if (ipc_recv(IPC_ANY, &m) != 0) {
            continue;
        }
        if (m.label == ADD_QUIT) {
            exit(0);
        }
        m.data[0] += m.data[1];
        ipc_send(m.sender, &m);
    }
}

static void demo_memory_and_fork(void) {
    char *page = (char *)mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) {
        printf("init: mmap failed\n");
        return;
    }
    page[0] = 42;

    // 子进程看到写时复制的页
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
}

static void demo_ipc(void) {
    int server = fork();
    if (server == 0) {
        drop_privilege();   // 普通服务不需要碰硬件
        add_server();
    }

    // 请求-应答
    int ok = 1;
    for (int i = 1; i <= 3; i++) {
        struct ipc_msg m = {};
        m.label = ADD_REQUEST;
        m.data[0] = i;
        m.data[1] = 100;
        if (ipc_call(server, &m) != 0 || m.sender != (uint32_t)server || m.data[0] != (uint64_t)(i + 100)) {
            ok = 0;
        }
    }
    printf("init: ipc call x3 to server %d: %s\n", server, ok ? "ok" : "FAILED");

    // 让服务退出；之后再发消息应当失败
    struct ipc_msg quit = {};
    quit.label = ADD_QUIT;
    ipc_send(server, &quit);
    waitpid(server, NULL, 0);
    printf("init: ipc send to exited server: %s\n", ipc_send(server, &quit) == -1 ? "refused" : "FAILED");

    // 阻塞在 recv 上的进程可以被 kill
    int idle = fork();
    if (idle == 0) {
        struct ipc_msg m;
        ipc_recv(IPC_ANY, &m);
        exit(1);
    }
    usleep(50000);
    kill(idle, 9);
    int status = 0;
    waitpid(idle, &status, 0);
    printf("init: kill of blocked receiver: %s\n", WIFSIGNALED(status) ? "ok" : "FAILED");
}

static void demo_privilege(void) {
    // 放弃特权的进程不能访问设备寄存器，也不能认领中断
    int pid = fork();
    if (pid == 0) {
        drop_privilege();
        uint32_t v;
        exit(io_read(0x80, 1, &v) == -1 && irq_claim(5) == -1 && irq_claim(40) == -1 ? 0 : 1);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    printf("init: hardware access after drop_privilege: %s\n",
           WEXITSTATUS(status) == 0 ? "refused" : "FAILED");
}

// 启动一个模块：fork 之后用模块的 ELF 映像替换子进程（特权随之保留）
static int start_module(const char *name, const char *image, const char *image_end) {
    int pid = fork();
    if (pid == 0) {
        exec(image, (size_t)(image_end - image));
        printf("init: exec %s failed\n", name);
        exit(1);
    }
    return pid;
}

int main() {
    printf("init: started, pid=%d\n", getpid());

    demo_memory_and_fork();
    demo_ipc();
    demo_privilege();

    int uart = start_module("uart", uart_image_start, uart_image_end);

    printf("init: ready, echoing console input\n");
    for (;;) {
        struct ipc_msg m = {};
        m.label = UART_READ;
        if (ipc_call(uart, &m) != 0) {
            printf("init: uart driver is gone\n");
            for (;;) {
                sleep(60);
            }
        }
        console_write(&m.data[1], (size_t)m.data[0]);
    }
}
