// init - 第一个用户进程
//
// 以 ELF 映像的形式嵌入内核（src/kernel/init_image.S），由内核在启动时加载。
// 它做四件事：
//   1. 启动模块（映像带在自己身上，见 modules.S）
//   2. 分配设备：只有 init 有特权。模块启动前都先放弃特权；驱动在放弃之前由 init 把
//      它的设备（端口或设备内存、中断线）记进许可表，之后它只碰得到这一个设备
//   3. 充当名字服务：服务进程把名字登记到这里，客户按名字查到它的 PID（协议见 names.h）。
//      模块的服务名是留给它的：只有 init 启动的那个进程能登记，别的进程冒充不了
//   4. 看着模块：工作着的模块退出了（崩溃、被杀）就重启它，驱动重启时重新许可它的设备

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <names.h>
#include <console.h>
#include <virtio.h>
#include <blk.h>
#include <net.h>
#include <fs.h>

extern "C" const char console_image_start[], console_image_end[];
extern "C" const char uart_image_start[], uart_image_end[];
#if !defined(ARCH_ARM64)
extern "C" const char kbd_image_start[], kbd_image_end[];
#endif
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

#if !defined(ARCH_ARM64)
static void allow_kbd(void) {
    // PS/2 键盘控制器：数据端口、状态/命令端口和 1 号中断在 PC 上是固定的
    hw_allow(HW_PORTS, 0x60, 1);
    hw_allow(HW_PORTS, 0x64, 1);
    hw_allow(HW_IRQ, 1, 1);
}
#endif

static void allow_blk(void) {
    if (virtio_allow(VIRTIO_ID_BLOCK)) {
        return;
    }
#if !defined(ARCH_ARM64)
    // 没有 virtio 磁盘：PC 的第一个 IDE 通道，它的端口和 14 号中断是固定的。
    // 上面接没接硬盘由驱动自己去看
    hw_allow(HW_PORTS, 0x1F0, 8);
    hw_allow(HW_PORTS, 0x3F6, 1);
    hw_allow(HW_IRQ, 14, 1);
#endif
}

static void allow_net(void) {
    virtio_allow(VIRTIO_ID_NET);
}

// ============================================================================
// 模块启动
// ============================================================================

// 一个模块：init 启动它，它退出了 init 知道，该重启的时候重启它
struct module {
    const char *name;
    const char *service;        // 它登记的服务名（没有就是 NULL）。这个名字留给它一个人用
    const char *image, *image_end;
    void (*allow)(void);        // 不为 NULL 表示它是驱动：放弃特权之前调用，把设备许可给它
    int pid;                    // 现在运行着的那个进程；0 表示没有在运行
    bool registered;            // 这一次启动以来它登记过服务名了：说明它真的工作起来了
    int restarts;               // 已经重启过几次
};

static struct module modules[] = {
    { "console", CONSOLE_SERVICE_NAME, console_image_start, console_image_end, NULL, 0, false, 0 },
    { "uart", UART_NAME, uart_image_start, uart_image_end, allow_uart, 0, false, 0 },
#if !defined(ARCH_ARM64)
    { "kbd", KBD_NAME, kbd_image_start, kbd_image_end, allow_kbd, 0, false, 0 },
#endif
    { "blk", BLK_SERVICE_NAME, blk_image_start, blk_image_end, allow_blk, 0, false, 0 },
    { "net", NET_SERVICE_NAME, net_image_start, net_image_end, allow_net, 0, false, 0 },
    { "ramfs", FS_SERVICE_NAME, ramfs_image_start, ramfs_image_end, NULL, 0, false, 0 },
    { "diskfs", FS_DISK_SERVICE_NAME, diskfs_image_start, diskfs_image_end, NULL, 0, false, 0 },
    { "sh", NULL, sh_image_start, sh_image_end, NULL, 0, false, 0 },
};
#define MODULE_COUNT ((int)(sizeof(modules) / sizeof(modules[0])))

/** 一个模块最多重启这么多次：一启动就崩溃的模块不能没完没了地重启下去 */
#define MAX_RESTARTS    5
/** 每隔这么久看一眼有没有模块退出了（没有人来问名字的时候，init 靠定时器醒来） */
#define REAP_INTERVAL_MS 100

// 在等一个模块“登记了，或者不会来登记了”的进程（NAME_SETTLE）
#define MAX_WAITERS 16
static struct {
    int pid;                    // 0 表示空闲
    struct module *module;
} waiters[MAX_WAITERS];

/** 模块 m 有定论了：告诉等它的进程。pid 是它登记的进程，0 表示它不在了 */
static void settle(struct module *m, int pid) {
    for (int i = 0; i < MAX_WAITERS; i++) {
        if (waiters[i].pid != 0 && waiters[i].module == m) {
            struct ipc_msg reply = {};
            reply.label = NAME_SETTLE;
            reply.data[0] = (uint64_t)pid;
            ipc_reply(waiters[i].pid, &reply);
            waiters[i].pid = 0;
        }
    }
}

/** 登记了（或者有权登记）服务名 name 的模块；不是模块的服务名返回 NULL */
static struct module *module_of_service(const char *name) {
    for (int i = 0; i < MODULE_COUNT; i++) {
        if (modules[i].service && strcmp(modules[i].service, name) == 0) {
            return &modules[i];
        }
    }
    return NULL;
}

// fork 之后用模块的 ELF 映像替换子进程。驱动的子进程在放弃特权之前先把设备许可给自己。
// 重启的模块多带一个参数 "restarted"（命令行靠它知道不用再执行一遍启动脚本）
static void start_module(struct module *m) {
    int parent = getpid();
    int pid = fork();
    if (pid == 0) {
        if (m->allow) {
            m->allow();
            struct ipc_msg done = {};
            ipc_send(parent, &done);
        }
        drop_privilege();
        const char *argv[] = { m->name, m->restarts > 0 ? "restarted" : NULL, NULL };
        exec(m->image, (size_t)(m->image_end - m->image), argv);
        printf("init: exec %s failed\n", m->name);
        exit(1);
    }
    if (m->allow) {
        // 等它找完设备再往下走：找设备要读写 PCI 配置空间，地址和数据是两个端口，
        // 两个进程同时找会互相打断（子进程中途退出时这里返回 -1，不会一直等）
        struct ipc_msg done;
        ipc_recv(pid, &done);
    }
    // 服务名从现在起留给这个 PID。这时它的登记请求还没有被处理过（init 要回到主循环才收
    // 请求），所以不会被人抢先
    m->pid = pid > 0 ? pid : 0;
    m->registered = false;
    printf("init: %s %s (pid %d%s)\n", m->restarts > 0 ? "restarted" : "started", m->name, pid,
           m->allow ? ", driver" : "");
}

/**
 * 一个模块退出了。重启它，如果它是真的工作过之后才退出的：登记过服务名的模块是这样，
 * 没有服务名的模块（命令行）总是这样。一启动就发现没有自己的设备、没登记就退出的驱动
 * （没有磁盘时的 blk）不重启——再启动一次结果也一样。
 */
static void module_exited(struct module *m) {
    bool worked = m->service == NULL || m->registered;
    m->pid = 0;
    if (!worked) {
        settle(m, 0);
        return;
    }
    if (m->restarts >= MAX_RESTARTS) {
        printf("init: %s keeps exiting, giving up on it\n", m->name);
        settle(m, 0);
        return;
    }
    m->restarts++;
    start_module(m);
}

// ============================================================================
// 名字服务
// ============================================================================

#define MAX_NAMES 16

static struct {
    char name[NAME_MAX];
    int pid;            // 0 表示空闲
} names[MAX_NAMES];

// 回收已退出的子进程，注销它们登记的名字；退出的是模块的话看要不要重启
static void reap_children(void) {
    int pid;
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_NAMES; i++) {
            if (names[i].pid == pid) {
                printf("init: %s (pid %d) exited\n", names[i].name, pid);
                names[i].pid = 0;
            }
        }
        for (int i = 0; i < MODULE_COUNT; i++) {
            if (modules[i].pid == pid) {
                module_exited(&modules[i]);
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
    // 模块的服务名只有 init 启动的那个进程能登记，不管那个服务现在在不在：
    // 客户按这个名字找到的要么是真的那个服务，要么谁也找不到
    struct module *owner = module_of_service(name);
    if (owner && owner->pid != pid) {
        return false;
    }
    for (int i = 0; i < MAX_NAMES; i++) {
        if (names[i].pid == 0) {
            strcpy(names[i].name, name);
            names[i].pid = pid;
            if (owner) {
                owner->registered = true;
                settle(owner, pid);
            }
            return true;
        }
    }
    return false;
}

int main() {
    printf("init: started, pid=%d\n", getpid());

    for (int i = 0; i < MODULE_COUNT; i++) {
        start_module(&modules[i]);
    }
    timer_set(REAP_INTERVAL_MS);

    struct ipc_msg m;
    for (;;) {
        if (ipc_recv(IPC_ANY, &m) != 0) {
            continue;
        }
        reap_children();
        if (m.sender == IPC_KERNEL) {
            // 定时器：上面已经看过有没有模块退出了，定下一次
            timer_set(REAP_INTERVAL_MS);
            continue;
        }

        char *name = (char *)m.data;
        name[NAME_MAX - 1] = '\0';

        uint64_t result;
        if (m.label == NAME_REGISTER) {
            result = register_name(name, (int)m.sender) ? 0 : 1;
        } else if (m.label == NAME_LOOKUP) {
            int i = find_name(name);
            result = i >= 0 ? (uint64_t)names[i].pid : 0;
        } else if (m.label == NAME_SETTLE) {
            int i = find_name(name);
            struct module *owner = module_of_service(name);
            result = i >= 0 ? (uint64_t)names[i].pid : 0;
            if (i < 0 && owner && owner->pid != 0) {
                // 它在运行，还没登记：先不应答，等它登记或者退出（settle）
                bool parked = false;
                for (int w = 0; w < MAX_WAITERS && !parked; w++) {
                    if (waiters[w].pid == 0) {
                        waiters[w].pid = (int)m.sender;
                        waiters[w].module = owner;
                        parked = true;
                    }
                }
                if (parked) {
                    continue;
                }
            }
        } else {
            continue;   // 不认识的请求：不应答
        }

        struct ipc_msg reply = {};
        reply.label = m.label;
        reply.data[0] = result;
        ipc_reply(m.sender, &reply);
    }
}
