// init - 第一个用户进程
//
// 以 ELF 映像的形式嵌入内核（src/kernel/init_image.S），由内核在启动时加载。
// 它做五件事：
//   1. 启动模块（映像带在自己身上，见 modules.S）
//   2. 分配设备：只有 init 有特权。模块启动前都先放弃特权；驱动在放弃之前由 init 把
//      它的设备（端口或设备内存、中断线）记进许可表，之后它只碰得到这一个设备
//   3. 充当名字服务：服务进程把名字登记到这里，客户按名字查到它的 PID（协议见 names.h）。
//      模块的服务名是留给它的：只有 init 启动的那个进程能登记，别的进程冒充不了
//   4. 看着模块：工作着的模块退出了（崩溃、被杀）就重启它，驱动重启时重新许可它的设备
//   5. 关机和重启：别的进程请它来做（power.h）。它先让文件系统和磁盘停稳，再让内核断电或复位

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <names.h>
#include <power.h>
#include <console.h>
#include <virtio.h>
#include <pci.h>
#include <blk.h>
#include <net.h>
#include <fs.h>

extern "C" const char console_image_start[], console_image_end[];
extern "C" const char uart_image_start[], uart_image_end[];
#if !defined(ARCH_ARM64)
extern "C" const char kbd_image_start[], kbd_image_end[];
extern "C" const char usbkbd_image_start[], usbkbd_image_end[];
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

// UHCI 控制器（USB 1.1，键盘接在它上面）：在 PCI 上按类别找，一台机器上常有好几个。
// 它的寄存器是一段端口（第 4 个 BAR，32 个）
#define PCI_CLASS_UHCI          0x0C0300
#define UHCI_BAR                4
#define UHCI_PORT_COUNT         32
#define UHCI_MAX                4       // 最多许可这么多个（usbkbd 也只管这么多）
#define UHCI_REG_USBCMD         0x00
#define UHCI_REG_USBINTR        0x04
// 配置空间里固件和系统之间交接用的寄存器（16 位）
#define UHCI_LEGACY_SUPPORT     0xC0
#define UHCI_LEGACY_CLEAR       0x8F00  // 关掉固件的那些中断，清掉它们留下的状态
#define UHCI_LEGACY_IRQ_ENABLE  0x2000  // 控制器的中断送到它的中断线上（而不是送给固件）

static void allow_usbkbd(void) {
    uint32_t lines = 0;         // 用到的中断线，每条一位
    pci_dev_t dev;
    for (uint32_t index = 0; index < UHCI_MAX && pci_find_class(PCI_CLASS_UHCI, index, &dev); index++) {
        uint32_t bar = pci_read(dev, PCI_BAR0 + UHCI_BAR * 4);
        uint32_t base = bar & 0xFFFCu;
        if (!(bar & 1) || base == 0) {
            continue;
        }
        // 打开端口的访问和总线主控（控制器要自己读写内存）；驱动碰不到配置空间
        pci_write(dev, PCI_COMMAND, pci_read(dev, PCI_COMMAND) | PCI_COMMAND_IO | PCI_COMMAND_MASTER);
        // 从固件手里把控制器拿过来。固件可能正用着它，把 USB 键盘装成 PS/2 键盘给没有驱动的
        // 系统用：靠的是控制器一有事就打断系统、转去执行固件。把这些关掉，让控制器停下来
        // （上一个驱动如果是崩溃的，它还在照着已经被收回的内存收发），再让它的中断走中断线
        uint32_t legacy = pci_read(dev, UHCI_LEGACY_SUPPORT) & 0xFFFF0000u;
        pci_write(dev, UHCI_LEGACY_SUPPORT, legacy | UHCI_LEGACY_CLEAR);
        io_write(base + UHCI_REG_USBCMD, 2, 0);
        io_write(base + UHCI_REG_USBINTR, 2, 0);
        pci_write(dev, UHCI_LEGACY_SUPPORT, legacy | UHCI_LEGACY_IRQ_ENABLE);

        hw_allow(HW_PORTS, base, UHCI_PORT_COUNT);
        uint32_t line = pci_read(dev, PCI_INTERRUPT) & 0xFF;
        if (line != 0 && line < 16) {
            lines |= 1u << line;
        }
    }
    // 几个控制器常常共用一条中断线：每条只许可一次
    for (uint32_t line = 1; line < 16; line++) {
        if (lines & (1u << line)) {
            hw_allow(HW_IRQ, line, 1);
        }
    }
}

// EHCI 控制器：在 PCI 上按类别找。它的寄存器在一段设备内存里（第 0 个 BAR）
#define PCI_CLASS_EHCI          0x0C0320
#define EHCI_CAP_HCCPARAMS      0x08    // 第 8-15 位：配置空间里“扩展能力”的偏移
#define EHCI_LEGACY_SUPPORT     1       // 那个扩展能力的编号：固件和系统之间交接控制器用的
#define EHCI_LEGACY_BIOS_OWNED  (1u << 16)
#define EHCI_LEGACY_OS_OWNED    (1u << 24)

static void allow_ehci(void) {
    pci_dev_t dev;
    if (!pci_find_class(PCI_CLASS_EHCI, 0, &dev)) {
        return;
    }
    uint32_t bar = pci_read(dev, PCI_BAR0);
    uint32_t size = pci_bar_size(dev, 0);
    if ((bar & 1) || size == 0) {
        return;
    }
    uintptr_t base = bar & ~0xFu;
    // 打开设备内存的访问和总线主控（控制器要自己读写内存）；驱动碰不到配置空间
    pci_write(dev, PCI_COMMAND, pci_read(dev, PCI_COMMAND) | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    volatile uint32_t *regs = (volatile uint32_t *)map_device(base & ~(uintptr_t)0xFFF, 0x1000);
    if (regs == MAP_FAILED) {
        return;
    }
    volatile uint32_t *cap = (volatile uint32_t *)((char *)regs + (base & 0xFFF));
    // 从固件手里把控制器要过来：固件可能正用着它（让 USB 键盘、U 盘在没有驱动时也能用）。
    // 在交接用的寄存器里举手“系统要了”，等固件放手；它一直不放就直接拿走。然后关掉它的
    // 那些会打断系统的中断
    uint32_t next = (cap[EHCI_CAP_HCCPARAMS / 4] >> 8) & 0xFF;
    for (int guard = 0; next >= 0x40 && guard < 16; guard++) {
        uint32_t value = pci_read(dev, next);
        if ((value & 0xFF) == EHCI_LEGACY_SUPPORT) {
            pci_write(dev, next, value | EHCI_LEGACY_OS_OWNED);
            for (int i = 0; i < 100 && (pci_read(dev, next) & EHCI_LEGACY_BIOS_OWNED); i++) {
                usleep(10000);
            }
            pci_write(dev, next, (pci_read(dev, next) & ~EHCI_LEGACY_BIOS_OWNED) | EHCI_LEGACY_OS_OWNED);
            pci_write(dev, next + 4, 0);
        }
        next = (value >> 8) & 0xFF;
    }
    // 让控制器停下来。上一个驱动如果是崩溃的，控制器还在照着它的（已经被收回的）内存收发
    uint32_t caplength = cap[0] & 0xFF;
    volatile uint32_t *usbcmd = (volatile uint32_t *)((char *)cap + caplength);
    *usbcmd = *usbcmd & ~1u;
    munmap((void *)regs, 0x1000);

    hw_allow(HW_MEMORY, base, size);
    uint32_t line = pci_read(dev, PCI_INTERRUPT) & 0xFF;
    if (line != 0 && line < 16) {
        hw_allow(HW_IRQ, line, 1);
    }
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
    // 还有 USB 2.0 的控制器（上面可能插着 U 盘），如果机器有的话
    allow_ehci();
#endif
}

#if !defined(ARCH_ARM64)

// Intel 的千兆网卡（82540 一族）：在 PCI 上的以太网卡里按厂商号和设备号认。
// 它的寄存器在一段设备内存里（第 0 个 BAR）
#define PCI_CLASS_ETHERNET      0x020000
#define PCI_VENDOR_INTEL        0x8086
#define E1000_CTRL_RESET        (1u << 26)      // 控制寄存器（偏移 0）里的复位位

static void allow_e1000(void) {
    // 82540EM（QEMU 模拟的那个）、82545EM，和笔记本上的 82540EP 的几个变种
    static const uint16_t devices[] = { 0x100E, 0x100F, 0x1015, 0x1016, 0x1017, 0x101E };
    pci_dev_t dev;
    for (uint32_t index = 0; pci_find_class(PCI_CLASS_ETHERNET, index, &dev); index++) {
        uint32_t id = pci_read(dev, PCI_ID);
        bool ours = false;
        for (size_t i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
            ours = ours || ((id & 0xFFFF) == PCI_VENDOR_INTEL && (id >> 16) == devices[i]);
        }
        uint32_t bar = pci_read(dev, PCI_BAR0);
        uint32_t size = pci_bar_size(dev, 0);
        if (!ours || (bar & 1) || size == 0) {
            continue;
        }
        uintptr_t base = bar & ~0xFu;
        // 打开设备内存的访问和总线主控（网卡要自己读写内存）；驱动碰不到配置空间
        pci_write(dev, PCI_COMMAND, pci_read(dev, PCI_COMMAND) | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
        // 复位网卡。上一个驱动如果是崩溃的，网卡还在往它的（已经被收回的）内存里写收到的帧
        volatile uint32_t *regs = (volatile uint32_t *)map_device(base, 0x1000);
        if (regs != MAP_FAILED) {
            regs[0] = regs[0] | E1000_CTRL_RESET;
            munmap((void *)regs, 0x1000);
        }
        hw_allow(HW_MEMORY, base, size);
        hw_allow(HW_IRQ, pci_read(dev, PCI_INTERRUPT) & 0xFF, 1);
        return;
    }
}

#endif

static void allow_net(void) {
    if (virtio_allow(VIRTIO_ID_NET)) {
        return;
    }
#if !defined(ARCH_ARM64)
    allow_e1000();
#endif
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
    { "usbkbd", USBKBD_NAME, usbkbd_image_start, usbkbd_image_end, allow_usbkbd, 0, false, 0 },
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

/** 正在关机或重启：从这时起退出的模块不再重启 */
static bool shutting_down;

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
 * （没有磁盘时的 blk）不重启——再启动一次结果也一样。关机的过程中退出的也不重启。
 */
static void module_exited(struct module *m) {
    bool worked = m->service == NULL || m->registered;
    m->pid = 0;
    if (!worked || shutting_down) {
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

// ============================================================================
// 关机和重启
// ============================================================================

/** 请一个模块的服务停下来（请求的含义见它的协议：应答之后退出），等它应答 */
static void stop_service(const char *service, uint32_t label) {
    struct module *m = module_of_service(service);
    if (m && m->pid != 0 && m->registered) {
        struct ipc_msg stop = {};
        stop.label = label;
        ipc_call(m->pid, &stop);
    }
}

/**
 * 关机或重启。在 fork 出来的子进程里运行（特权还在），不在 init 自己身上：要等别的服务
 * 应答，而那个服务可能正好在等 init 应答它查名字的请求，两边就都等不到了。
 *
 * 断电之前磁盘上要有全部内容，而且不能有做到一半的修改。先停文件系统：它一次处理一个请求，
 * 应答了停止的请求就说明手上没有别的了，之后它不在了，也就没有人再经它写盘。再停块设备
 * 驱动，它在退出之前让磁盘把自己的缓存落盘。别的进程不用管：它们留不下什么。
 */
static void power_down(int action) {
    printf("init: %s\n", action == POWER_REBOOT ? "rebooting" : "powering off");
    stop_service(FS_DISK_SERVICE_NAME, FS_STOP);
    stop_service(BLK_SERVICE_NAME, BLK_STOP);
    power(action);
    // 内核说这台机器的固件没有给出办法。文件系统已经停了，能做的只剩下告诉人
    printf("init: this machine cannot %s by itself; it is now safe to switch it off\n",
           action == POWER_REBOOT ? "reboot" : "power off");
    exit(1);
}

/** 处理一个关机或重启的请求。@return 接下了没有（接下了就不应答：请求者一直等到机器停下） */
static bool power_requested(uint64_t action) {
    if (shutting_down || (action != POWER_OFF && action != POWER_REBOOT)) {
        return false;
    }
    int pid = fork();
    if (pid == 0) {
        power_down((int)action);
    }
    shutting_down = pid > 0;
    return shutting_down;
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

        if (m.label == POWER_REQUEST) {
            if (!power_requested(m.data[0])) {
                struct ipc_msg refused = {};
                refused.label = POWER_REQUEST;
                ipc_reply(m.sender, &refused);
            }
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
