// init - 第一个用户进程
//
// 以 ELF 映像的形式嵌入内核（src/kernel/init_image.S），由内核在启动时加载。
// 它做两件事：
//   1. 启动模块（映像带在自己身上，见 modules.S）：驱动保留特权，其余的先放弃
//   2. 充当名字服务：服务进程把名字登记到这里，客户按名字查到它的 PID（协议见 names.h）

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <names.h>

extern "C" const char uart_image_start[], uart_image_end[];
extern "C" const char demo_image_start[], demo_image_end[];

// ============================================================================
// 模块启动
// ============================================================================

// fork 之后用模块的 ELF 映像替换子进程
static int start_module(const char *name, const char *image, const char *image_end, bool privileged) {
    int pid = fork();
    if (pid == 0) {
        if (!privileged) {
            drop_privilege();
        }
        exec(image, (size_t)(image_end - image));
        printf("init: exec %s failed\n", name);
        exit(1);
    }
    printf("init: started %s (pid %d%s)\n", name, pid, privileged ? ", privileged" : "");
    return pid;
}

// ============================================================================
// 名字服务
// ============================================================================

#define MAX_NAMES 16

static struct {
    char name[NAME_MAX];
    int pid;            // 0 表示空闲
} names[MAX_NAMES];

// 回收已退出的子进程，并注销它们登记的名字
static void reap_children(void) {
    int pid;
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_NAMES; i++) {
            if (names[i].pid == pid) {
                printf("init: %s (pid %d) exited\n", names[i].name, pid);
                names[i].pid = 0;
            }
        }
    }
}

static int find_name(const char *name) {
    for (int i = 0; i < MAX_NAMES; i++) {
        if (names[i].pid != 0 && strcmp(names[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static bool register_name(const char *name, int pid) {
    if (name[0] == '\0' || find_name(name) >= 0) {
        return false;
    }
    for (int i = 0; i < MAX_NAMES; i++) {
        if (names[i].pid == 0) {
            strcpy(names[i].name, name);
            names[i].pid = pid;
            return true;
        }
    }
    return false;
}

int main() {
    printf("init: started, pid=%d\n", getpid());

    start_module("uart", uart_image_start, uart_image_end, true);
    start_module("demo", demo_image_start, demo_image_end, false);

    struct ipc_msg m;
    for (;;) {
        if (ipc_recv(IPC_ANY, &m) != 0) {
            continue;
        }
        reap_children();

        char *name = (char *)m.data;
        name[NAME_MAX - 1] = '\0';

        uint64_t result;
        if (m.label == NAME_REGISTER) {
            result = register_name(name, (int)m.sender) ? 0 : 1;
        } else if (m.label == NAME_LOOKUP) {
            int i = find_name(name);
            result = i >= 0 ? (uint64_t)names[i].pid : 0;
        } else {
            continue;   // 不认识的请求：不应答
        }

        struct ipc_msg reply = {};
        reply.label = m.label;
        reply.data[0] = result;
        ipc_send(m.sender, &reply);
    }
}
