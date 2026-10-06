// init - 第一个用户进程
//
// 以 ELF 映像的形式嵌入内核（src/kernel/init_image.S），由内核在启动时加载。
// 它做三件事：
//   1. 启动模块（映像带在自己身上，见 modules.S）
//   2. 分配设备：只有 init 有特权。模块启动前都先放弃特权；驱动在放弃之前由 init 把
//      它的设备（端口或设备内存、中断线）记进许可表，之后它只碰得到这一个设备
//   3. 充当名字服务：服务进程把名字登记到这里，客户按名字查到它的 PID（协议见 names.h）

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <names.h>
#include <virtio.h>

extern "C" const char uart_image_start[], uart_image_end[];
extern "C" const char blk_image_start[], blk_image_end[];
extern "C" const char net_image_start[], net_image_end[];
extern "C" const char ramfs_image_start[], ramfs_image_end[];
extern "C" const char diskfs_image_start[], diskfs_image_end[];
extern "C" const char sh_image_start[], sh_image_end[];

// ============================================================================
// 分配设备
//
// 下面每个函数在 fork 出来、还有特权的子进程里运行：找到一个驱动的设备，把它占用的
// 资源记进这个子进程的许可表。找不到设备就什么都不记，驱动启动后发现没有设备自己退出。
// ============================================================================

static void allow_uart(void) {
#if defined(ARCH_ARM64)
    // PL011 在哪里、用哪个中断，由设备树说了算
    struct device_info dev;
    if (device_find("arm,pl011", 0, &dev) == 0 && dev.has_irq) {
        hw_allow(HW_MEMORY, (uintptr_t)dev.base, (uintptr_t)(dev.size ? dev.size : 0x1000));
        hw_allow(HW_IRQ, dev.irq, 1);
    }
#else
    // COM1：PC 上它的 8 个端口和 4 号中断是固定的
    hw_allow(HW_PORTS, 0x3F8, 8);
    hw_allow(HW_IRQ, 4, 1);
#endif
}

static void allow_blk(void) {
    virtio_allow(VIRTIO_ID_BLOCK);
}

static void allow_net(void) {
    virtio_allow(VIRTIO_ID_NET);
}

// ============================================================================
// 模块启动
// ============================================================================

// fork 之后用模块的 ELF 映像替换子进程。allow 不为 NULL 表示这个模块是驱动：
// 子进程放弃特权之前先调用它，把驱动的设备许可给自己
static int start_module(const char *name, const char *image, const char *image_end, void (*allow)(void)) {
    int parent = getpid();
    int pid = fork();
    if (pid == 0) {
        if (allow) {
            allow();
            struct ipc_msg done = {};
            ipc_send(parent, &done);
        }
        drop_privilege();
        const char *argv[] = { name, NULL };
        exec(image, (size_t)(image_end - image), argv);
        printf("init: exec %s failed\n", name);
        exit(1);
    }
    if (allow) {
        // 等它找完设备再启动下一个：找设备要读写 PCI 配置空间，地址和数据是两个端口，
        // 两个进程同时找会互相打断（子进程中途退出时这里返回 -1，不会一直等）
        struct ipc_msg done;
        ipc_recv(pid, &done);
    }
    printf("init: started %s (pid %d%s)\n", name, pid, allow ? ", driver" : "");
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
        if (names[i].pid == 0 || strcmp(names[i].name, name) != 0) {
            continue;
        }
        // 登记者不一定是 init 的子进程，退出时这里收不到通知：用到时再确认它还在
        if (kill(names[i].pid, 0) != 0) {
            names[i].pid = 0;
            return -1;
        }
        return i;
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

    start_module("uart", uart_image_start, uart_image_end, allow_uart);
    start_module("blk", blk_image_start, blk_image_end, allow_blk);
    start_module("net", net_image_start, net_image_end, allow_net);
    start_module("ramfs", ramfs_image_start, ramfs_image_end, NULL);
    start_module("diskfs", diskfs_image_start, diskfs_image_end, NULL);
    start_module("sh", sh_image_start, sh_image_end, NULL);

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
        ipc_reply(m.sender, &reply);
    }
}
