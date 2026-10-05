// demo - 非特权的示例程序
//
// 由 init 启动。依次演示内存、进程、IPC 和特权，然后按名字找到 uart 驱动，
// 把它送来的控制台输入回显出来。

#include <syscall.h>
#include <stdio.h>
#include <names.h>
#include <uart.h>

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
        printf("demo: mmap failed\n");
        return;
    }
    page[0] = 42;

    // 子进程看到写时复制的页
    int pid = fork();
    if (pid == 0) {
        printf("demo: child pid=%d ppid=%d sees %d\n", getpid(), getppid(), page[0]);
        sleep(1);
        exit(7);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    printf("demo: child %d exited with %d\n", pid, WEXITSTATUS(status));
    munmap(page, 4096);
}

static void demo_ipc(void) {
    int server = fork();
    if (server == 0) {
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
    printf("demo: ipc call x3 to server %d: %s\n", server, ok ? "ok" : "FAILED");

    // 让服务退出；之后再发消息应当失败
    struct ipc_msg quit = {};
    quit.label = ADD_QUIT;
    ipc_send(server, &quit);
    waitpid(server, NULL, 0);
    printf("demo: ipc send to exited server: %s\n", ipc_send(server, &quit) == -1 ? "refused" : "FAILED");

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
    printf("demo: kill of blocked receiver: %s\n", WIFSIGNALED(status) ? "ok" : "FAILED");
}

static void demo_privilege(void) {
    // demo 是放弃特权后启动的：不能访问设备寄存器，也不能认领中断
    int pid = fork();
    if (pid == 0) {
        uint32_t v;
        exit(io_read(0x80, 1, &v) == -1 && irq_claim(5) == -1 && irq_claim(40) == -1 ? 0 : 1);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    printf("demo: hardware access without privilege: %s\n",
           WEXITSTATUS(status) == 0 ? "refused" : "FAILED");
}

static void demo_names(void) {
    // 名字服务：登记、查询、重复登记被拒绝
    int ok = name_register("demo") == 0 && name_lookup("demo") == getpid() &&
             name_register("demo") == -1 && name_lookup("no-such-service") == 0;
    printf("demo: name service: %s\n", ok ? "ok" : "FAILED");
}

int main() {
    printf("demo: started, pid=%d\n", getpid());

    demo_memory_and_fork();
    demo_ipc();
    demo_privilege();
    demo_names();

    int uart = name_wait("uart");
    printf("demo: ready, echoing console input from uart (pid %d)\n", uart);
    for (;;) {
        struct ipc_msg m = {};
        m.label = UART_READ;
        if (ipc_call(uart, &m) != 0) {
            printf("demo: uart driver is gone\n");
            return 1;
        }
        console_write(&m.data[1], (size_t)m.data[0]);
    }
}
