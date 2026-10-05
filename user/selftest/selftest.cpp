// selftest - 用户态自检程序
//
// 放在启动文件系统里，由命令行的 rc 脚本在开机时运行（也可以随时手动再跑）。
// 依次检查内存、进程、IPC、特权、共享内存、名字服务和文件服务；
// 每项打印一行结果，有失败时以非零状态退出。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <names.h>
#include <fs.h>

static int failures = 0;

// 打印一项检查的结果；good 是通过时显示的词（"ok" 或 "refused"）
static void report(const char *what, bool passed, const char *good) {
    printf("selftest: %s: %s\n", what, passed ? good : "FAILED");
    if (!passed) {
        failures++;
    }
}

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

static void test_memory_and_fork(void) {
    char *page = (char *)mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) {
        report("mmap", false, "ok");
        return;
    }
    page[0] = 42;

    // 子进程看到父进程写入的值；它自己的写入落在写时复制的副本上
    int parent = getpid();
    int pid = fork();
    if (pid == 0) {
        int ok = page[0] == 42 && getppid() == parent;
        page[0] = 1;
        usleep(20000);
        exit(ok ? 7 : 1);
    }
    int status = 0;
    int reaped = waitpid(pid, &status, 0);
    report("mmap, fork, copy-on-write, waitpid",
           reaped == pid && WEXITSTATUS(status) == 7 && page[0] == 42, "ok");
    munmap(page, 4096);
}

static void test_ipc(void) {
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
    report("ipc call x3", ok, "ok");

    // reply 只对正在 call 自己的进程有效，否则立刻失败而不是阻塞
    struct ipc_msg stray = {};
    report("ipc reply to a process that is not calling", ipc_reply(server, &stray) == -1, "refused");

    // 让服务退出；之后再发消息应当失败
    struct ipc_msg quit = {};
    quit.label = ADD_QUIT;
    ipc_send(server, &quit);
    waitpid(server, NULL, 0);
    report("ipc send to exited server", ipc_send(server, &quit) == -1, "refused");

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
    report("kill of blocked receiver", WIFSIGNALED(status), "ok");
}

static void test_privilege(void) {
    // 本程序没有特权：不能访问设备寄存器，也不能认领中断
    int pid = fork();
    if (pid == 0) {
        uint32_t v;
        exit(io_read(0x80, 1, &v) == -1 && irq_claim(5) == -1 && irq_claim(40) == -1 &&
             map_device(0xB8000, 4096) == MAP_FAILED ? 0 : 1);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    report("hardware access without privilege", WEXITSTATUS(status) == 0, "refused");
}

static void test_shared_memory(void) {
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
    report("shared memory", ok, "ok");
}

static void test_names(void) {
    // 名字服务：登记、查询、重复登记被拒绝
    int ok = name_register("selftest") == 0 && name_lookup("selftest") == getpid() &&
             name_register("selftest") == -1 && name_lookup("no-such-service") == 0;
    report("name service", ok, "ok");
}

static void test_fs(void) {
    // 文件服务（user/ramfs）：内容经共享缓冲区传递，一次读写会被拆成多个请求
    static char out[10000], in[10000];
    for (size_t i = 0; i < sizeof(out); i++) {
        out[i] = (char)(i * 7 + i / 251);
    }

    int fd = fs_open("selftest.dat", FS_O_CREATE | FS_O_TRUNC);
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
        if (strcmp(name, "selftest.dat") == 0 && size == sizeof(out)) {
            listed = 1;
        }
    }

    // 另一个进程自己建立连接后能读到同一个文件，但用不了别人的句柄
    int child = fork();
    if (child == 0) {
        char byte = 0;
        int mine = fs_open("selftest.dat", 0);
        exit(mine >= 0 && fs_read(mine, 5000, &byte, 1) == 1 && byte == out[5000] &&
             fs_read(fd, 0, &byte, 1) == -1 ? 0 : 1);
    }
    int status = 1;
    waitpid(child, &status, 0);

    ok = ok && listed && WEXITSTATUS(status) == 0 &&
         fs_close(fd) == 0 && fs_read(fd, 0, in, 1) == -1 &&
         fs_unlink("selftest.dat") == 0 && fs_open("selftest.dat", 0) == -1;
    report("file service", ok, "ok");
}

int main(int argc, char **argv) {
    // 命令行（或 rc）是带着程序名启动我们的
    report("program arguments", argc >= 1 && strcmp(argv[0], "selftest") == 0 && argv[argc] == NULL, "ok");

    test_memory_and_fork();
    test_ipc();
    test_privilege();
    test_shared_memory();
    test_names();
    test_fs();

    if (failures == 0) {
        printf("selftest: all passed\n");
    } else {
        printf("selftest: %d FAILED\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
