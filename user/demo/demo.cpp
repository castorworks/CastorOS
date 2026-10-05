// demo - 非特权的示例程序
//
// 由 init 启动。依次检查内存、进程、IPC、特权、共享内存、名字服务和文件服务，
// 然后变成一个很小的命令行：输入来自 uart 驱动，文件操作交给文件服务。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <names.h>
#include <fs.h>
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
        ipc_reply(m.sender, &m);
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

    // reply 只对正在 call 自己的进程有效，否则立刻失败而不是阻塞
    struct ipc_msg stray = {};
    printf("demo: ipc reply to a process that is not calling: %s\n",
           ipc_reply(server, &stray) == -1 ? "refused" : "FAILED");

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
        exit(io_read(0x80, 1, &v) == -1 && irq_claim(5) == -1 && irq_claim(40) == -1 &&
             map_device(0xB8000, 4096) == MAP_FAILED ? 0 : 1);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    printf("demo: hardware access without privilege: %s\n",
           WEXITSTATUS(status) == 0 ? "refused" : "FAILED");
}

static void demo_shared_memory(void) {
    // 父进程把一页内存共享给子进程；内核用一条 IPC_LABEL_GRANT 消息告诉子进程映射在哪。
    // 子进程经由共享映射写入，父进程能看到（fork 得到的那份只是写时复制的副本）
    volatile uint32_t *page = (volatile uint32_t *)mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    page[0] = 1;

    int child = fork();
    if (child == 0) {
        struct ipc_msg m;
        ipc_recv(getppid(), &m);            // 内核发来的授予通知
        if (m.label != IPC_LABEL_GRANT || m.data[1] != 4096) {
            exit(1);
        }
        volatile uint32_t *shared = (volatile uint32_t *)(uintptr_t)m.data[0];
        uint32_t seen = shared[0];
        shared[0] = 2;
        page[0] = 99;                       // 这是子进程自己的副本，父进程看不到

        ipc_recv(getppid(), &m);            // 父进程来问结果
        m.data[0] = seen;
        ipc_reply(m.sender, &m);
        exit(0);
    }

    struct ipc_msg m = {};
    int ok = mem_grant(child, (void *)page, 4096) == 0 &&
             ipc_call(child, &m) == 0 && m.data[0] == 1 && page[0] == 2;
    waitpid(child, NULL, 0);

    // 对方退出后这一页仍然属于自己；向已退出的进程授予会失败
    page[0] = 3;
    ok = ok && page[0] == 3 && mem_grant(child, (void *)page, 4096) == -1;

    // 用户进程发不出内核保留的 label
    struct ipc_msg forged = {};
    forged.label = IPC_LABEL_GRANT;
    ok = ok && ipc_send(NAME_SERVER_PID, &forged) == -1;

    munmap((void *)page, 4096);
    printf("demo: shared memory: %s\n", ok ? "ok" : "FAILED");
}

static void demo_names(void) {
    // 名字服务：登记、查询、重复登记被拒绝
    int ok = name_register("demo") == 0 && name_lookup("demo") == getpid() &&
             name_register("demo") == -1 && name_lookup("no-such-service") == 0;
    printf("demo: name service: %s\n", ok ? "ok" : "FAILED");
}

static void demo_fs(void) {
    // 文件服务（user/ramfs）：内容经共享缓冲区传递，一次读写会被拆成多个请求
    static char out[10000], in[10000];
    for (size_t i = 0; i < sizeof(out); i++) {
        out[i] = (char)(i * 7 + i / 251);
    }

    int fd = fs_open("demo.dat", FS_O_CREATE | FS_O_TRUNC);
    int ok = fd >= 0 &&
             fs_write(fd, 0, out, sizeof(out)) == (long)sizeof(out) &&
             fs_read(fd, 0, in, sizeof(in)) == (long)sizeof(in) &&
             memcmp(out, in, sizeof(out)) == 0 &&
             fs_read(fd, 9990, in, 100) == 10 && memcmp(out + 9990, in, 10) == 0 &&
             fs_open("no-such-file", 0) == -1;

    // 能在列表里找到它，大小正确
    char name[FS_NAME_MAX];
    uint32_t size = 0;
    int listed = 0;
    for (int i = 0; fs_list(i, name, &size) == 0; i++) {
        if (strcmp(name, "demo.dat") == 0 && size == sizeof(out)) {
            listed = 1;
        }
    }

    // 另一个进程自己建立连接后能读到同一个文件，但用不了别人的句柄
    int child = fork();
    if (child == 0) {
        char byte = 0;
        int mine = fs_open("demo.dat", 0);
        exit(mine >= 0 && fs_read(mine, 5000, &byte, 1) == 1 && byte == out[5000] &&
             fs_read(fd, 0, &byte, 1) == -1 ? 0 : 1);
    }
    int status = 1;
    waitpid(child, &status, 0);

    ok = ok && listed && WEXITSTATUS(status) == 0 &&
         fs_close(fd) == 0 && fs_read(fd, 0, in, 1) == -1 &&
         fs_unlink("demo.dat") == 0 && fs_open("demo.dat", 0) == -1;
    printf("demo: file service: %s\n", ok ? "ok" : "FAILED");
}

// ============================================================================
// 一个很小的命令行：输入来自 uart 驱动，文件操作交给文件服务
// ============================================================================

static void cmd_ls(void) {
    char name[FS_NAME_MAX];
    uint32_t size;
    int i = 0;
    for (; fs_list(i, name, &size) == 0; i++) {
        printf("%8u  %s\n", size, name);
    }
    if (i == 0) {
        printf("(no files)\n");
    }
}

static void cmd_cat(const char *name) {
    int fd = fs_open(name, 0);
    if (fd < 0) {
        printf("cat: %s: no such file\n", name);
        return;
    }
    char buf[256];
    uint32_t offset = 0;
    long n;
    while ((n = fs_read(fd, offset, buf, sizeof(buf))) > 0) {
        console_write(buf, (size_t)n);
        offset += (uint32_t)n;
    }
    fs_close(fd);
}

static void cmd_write(char *args) {
    char *text = strchr(args, ' ');
    if (!text) {
        printf("usage: write <file> <text>\n");
        return;
    }
    *text++ = '\0';
    int fd = fs_open(args, FS_O_CREATE | FS_O_TRUNC);
    size_t len = strlen(text);
    if (fd < 0 || fs_write(fd, 0, text, len) != (long)len || fs_write(fd, (uint32_t)len, "\n", 1) != 1) {
        printf("write: %s: failed\n", args);
    }
    if (fd >= 0) {
        fs_close(fd);
    }
}

static void run_command(char *line) {
    while (*line == ' ') {
        line++;
    }
    if (*line == '\0') {
        return;
    }
    char *args = strchr(line, ' ');
    if (args) {
        *args++ = '\0';
    } else {
        args = line + strlen(line);
    }

    if (strcmp(line, "help") == 0) {
        printf("commands: ls, cat <file>, write <file> <text>, rm <file>, help\n");
    } else if (strcmp(line, "ls") == 0) {
        cmd_ls();
    } else if (strcmp(line, "cat") == 0 && *args) {
        cmd_cat(args);
    } else if (strcmp(line, "write") == 0 && *args) {
        cmd_write(args);
    } else if (strcmp(line, "rm") == 0 && *args) {
        if (fs_unlink(args) != 0) {
            printf("rm: %s: no such file\n", args);
        }
    } else {
        printf("%s: unknown command (try help)\n", line);
    }
}

int main() {
    printf("demo: started, pid=%d\n", getpid());

    demo_memory_and_fork();
    demo_ipc();
    demo_privilege();
    demo_shared_memory();
    demo_names();
    demo_fs();

    int uart = name_wait("uart");
    printf("demo: ready, reading commands from uart (pid %d); try help\n> ", uart);

    static char line[128];
    size_t len = 0;
    for (;;) {
        struct ipc_msg m = {};
        m.label = UART_READ;
        if (ipc_call(uart, &m) != 0) {
            printf("demo: uart driver is gone\n");
            return 1;
        }
        const char *in = (const char *)&m.data[1];
        for (size_t i = 0; i < (size_t)m.data[0]; i++) {
            char c = in[i];
            if (c == '\r' || c == '\n') {
                console_write("\n", 1);
                line[len] = '\0';
                run_command(line);
                len = 0;
                console_write("> ", 2);
            } else if (c == 0x7F || c == '\b') {
                if (len > 0) {
                    len--;
                    console_write("\b \b", 3);
                }
            } else if (len < sizeof(line) - 1) {
                line[len++] = c;
                console_write(&c, 1);
            }
        }
    }
}
