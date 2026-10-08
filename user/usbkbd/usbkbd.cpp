// usbkbd - 用户态 USB 键盘驱动
//
// USB 键盘是低速或全速设备，接在 UHCI 控制器上（uhci.cpp）。这里做三件事：
//   1. 看着端口：USB 设备可以随时插拔，控制器不会为此发中断，所以每隔一会儿看一眼每个
//      端口。新插上的设备复位、给地址、读它的描述符；是键盘就配置好，不是就不理它。
//   2. 读键盘：键盘属于“人机接口设备”（HID）。让它用“启动协议”——所有键盘都会的一种固定
//      格式：每次 8 个字节，第 0 个是修饰键（Ctrl、Shift……各占一位），第 2 到 7 个是此刻
//      按着的键（最多 6 个）的编号。键盘在按键有变化时给一份这样的报告。
//   3. 把报告变成字符：和上一份比，多出来的键就是刚按下的。字符交给 console 服务
//      （console_input，见 <console.h>），从那里起和 PS/2 键盘、串口来的输入走同一条路。
//
// 控制器的端口和中断线不写在这里：init 许可给本进程的就是。键盘布局是美式的。
// 不认集线器后面的键盘。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <names.h>
#include <console.h>
#include <keys.h>
#include <usb.h>
#include "uhci.h"

// 人机接口设备（HID）
#define CLASS_HID               0x03
#define SUBCLASS_BOOT           0x01    // 会启动协议
#define PROTOCOL_KEYBOARD       0x01
#define REQ_SET_REPORT          0x09
#define REQ_SET_IDLE            0x0A
#define REQ_SET_PROTOCOL        0x0B
#define PROTOCOL_BOOT           0
#define REPORT_OUTPUT           (2 << 8)    // SET_REPORT 的 value：发给键盘的报告（指示灯）
#define LED_CAPS_LOCK           0x02

// 报告
#define REPORT_SIZE             8
#define REPORT_KEYS             2       // 按着的键从第几个字节开始
#define MOD_CTRL                0x11    // 左、右 Ctrl
#define MOD_SHIFT               0x22    // 左、右 Shift
#define KEY_ROLLOVER            0x01    // 按着的键太多，键盘报不过来：这份报告不算数
#define KEY_FIRST               0x04    // 再往前的编号不是键
#define KEY_CAPS_LOCK           0x39
#define KEY_KEYPAD_FIRST        0x54

// 键的编号 -> 字符，从 KEY_FIRST 开始：字母、数字、回车到空格、符号
static const char plain[] =
    "abcdefghijklmnopqrstuvwxyz1234567890\n\033\b\t -=[]\\#;'`,./";
static const char shifted[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\n\033\b\t _+{}|~:\"~<>?";
static_assert(sizeof(plain) - 1 == KEY_CAPS_LOCK - KEY_FIRST && sizeof(shifted) == sizeof(plain));
// 小键盘上不随 Num Lock 变的那几个，从 KEY_KEYPAD_FIRST 开始
static const char keypad[] = "/*-+\n";

// 按住一个键不放：过 REPEAT_DELAY_MS 开始重复，之后每 REPEAT_INTERVAL_MS 一次。
// PS/2 键盘自己会重复，USB 键盘只报告“按着”，重复是主机的事
#define REPEAT_DELAY_MS         500
#define REPEAT_INTERVAL_MS      40
/** 每隔这么久看一眼端口上有没有插拔 */
#define SCAN_INTERVAL_MS        250
/** 读报告连着错这么多次：把设备当成新插上的重新认一遍 */
#define MAX_ERRORS              8
/** 重新认了这么多次还是错：不再理它，直到它被拔掉 */
#define MAX_ATTACHES            3

enum port_state {
    PORT_EMPTY,         // 上面没有设备，或者有、还没去认
    PORT_IGNORED,       // 上面的设备不是键盘（或者认不出来）：拔掉之前不再理它
    PORT_KEYBOARD,
};

struct port {
    enum port_state state;
    int seen;                   // 连着几次看到上面有设备（刚插上时触点会抖，等它稳下来）
    int attaches;               // 认了几次
    int errors;                 // 读报告连着错了几次
    struct uhci_device dev;
    uint8_t interface;
    uint8_t keys[REPORT_SIZE - REPORT_KEYS];    // 上一份报告里按着的键
    uint8_t mods;
    uint8_t repeat_key;         // 正在重复的键；0 表示没有
    uint64_t repeat_at;
};

static struct port ports[UHCI_MAX_CONTROLLERS][UHCI_PORTS];
static int controller_count;
static bool caps_lock;          // 所有键盘共用一个

// 攒着要交给 console 的字符
static char pending[CONSOLE_READ_MAX];
static uint32_t pending_count;

static void flush(void) {
    if (pending_count > 0) {
        console_input(pending, pending_count);
        pending_count = 0;
    }
}

static void emit(char c) {
    pending[pending_count++] = c;
    if (pending_count == CONSOLE_READ_MAX) {
        flush();
    }
}

// ============================================================================
// 报告 -> 字符
// ============================================================================

/** 编号是 key 的键在修饰键 mods 下产生的字符；不产生字符返回 0 */
static char translate(uint8_t key, uint8_t mods) {
    if (key >= KEY_KEYPAD_FIRST && key < KEY_KEYPAD_FIRST + sizeof(keypad) - 1) {
        return keypad[key - KEY_KEYPAD_FIRST];
    }
    if (key < KEY_FIRST || key >= KEY_CAPS_LOCK) {
        return 0;
    }
    return key_char(plain[key - KEY_FIRST], shifted[key - KEY_FIRST], mods & MOD_SHIFT, caps_lock, mods & MOD_CTRL);
}

static void set_leds(const struct port *p) {
    uint8_t leds = caps_lock ? LED_CAPS_LOCK : 0;
    uhci_control(&p->dev, USB_TYPE_CLASS_TO_INTERFACE, REQ_SET_REPORT, REPORT_OUTPUT, p->interface, &leds, 1);
}

static bool held(const uint8_t *keys, uint8_t key) {
    for (int i = 0; i < REPORT_SIZE - REPORT_KEYS; i++) {
        if (keys[i] == key) {
            return true;
        }
    }
    return false;
}

/** 键盘给了一份报告 */
static void report(struct port *p, const uint8_t *r) {
    const uint8_t *keys = r + REPORT_KEYS;
    if (keys[0] == KEY_ROLLOVER) {
        return;
    }
    p->mods = r[0];
    for (int i = 0; i < REPORT_SIZE - REPORT_KEYS; i++) {
        uint8_t key = keys[i];
        if (key < KEY_FIRST || held(p->keys, key)) {
            continue;
        }
        // 刚按下的键
        if (key == KEY_CAPS_LOCK) {
            caps_lock = !caps_lock;
            set_leds(p);
            continue;
        }
        char c = translate(key, p->mods);
        if (c != 0) {
            emit(c);
            p->repeat_key = key;
            p->repeat_at = uptime_ms() + REPEAT_DELAY_MS;
        }
    }
    memcpy(p->keys, keys, sizeof(p->keys));
    if (!held(p->keys, p->repeat_key)) {
        p->repeat_key = 0;
    }
}

/** 按住不放的键到时候了就再出一个字符 */
static void repeat(struct port *p) {
    if (p->repeat_key != 0 && uptime_ms() >= p->repeat_at) {
        char c = translate(p->repeat_key, p->mods);     // 修饰键可能变了：按现在的算
        if (c != 0) {
            emit(c);
        }
        p->repeat_at = uptime_ms() + REPEAT_INTERVAL_MS;
    }
}

// ============================================================================
// 认设备
// ============================================================================

/** 在配置描述符里找会启动协议的键盘接口和它的中断输入端点。@return 找到了 */
static bool find_keyboard(const uint8_t *desc, uint32_t total, uint8_t *interface, uint8_t *endpoint,
                          uint16_t *max_packet) {
    bool in_keyboard = false;
    for (uint32_t off = 0; off + 2 <= total && desc[off] >= 2; off += desc[off]) {
        const uint8_t *d = desc + off;
        if (off + d[0] > total) {
            break;
        }
        if (d[1] == USB_DESC_INTERFACE && d[0] >= 9) {
            in_keyboard = d[5] == CLASS_HID && d[6] == SUBCLASS_BOOT && d[7] == PROTOCOL_KEYBOARD;
            *interface = d[2];
        } else if (d[1] == USB_DESC_ENDPOINT && d[0] >= 7 && in_keyboard && (d[2] & USB_ENDPOINT_IN) &&
                   (d[3] & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_INTERRUPT) {
            *endpoint = d[2] & 0x0F;
            *max_packet = (uint16_t)(d[4] | ((d[5] & 0x07) << 8));
            return true;
        }
    }
    return false;
}

/** 第 controller 个控制器的第 port 个端口上是不是一个键盘；是的话把它配置好，开始读它 */
static bool attach(int controller, int port) {
    struct port *p = &ports[controller][port];
    bool low_speed;
    if (!uhci_port_reset(controller, port, &low_speed)) {
        return false;
    }
    // 刚复位的设备在地址 0 上。控制端点的最大包长写在设备描述符的第 7 个字节里，
    // 而最小的包长是 8：先按 8 读开头 8 个字节
    p->dev = { controller, 0, low_speed, 8 };
    uint8_t device[8];
    if (uhci_control(&p->dev, USB_TYPE_FROM_DEVICE, USB_REQ_GET_DESCRIPTOR, USB_DESC_DEVICE << 8, 0,
                     device, 8) < 8) {
        return false;
    }
    p->dev.max_packet = device[7];
    // 给它一个自己的地址（之后它要缓一下）。每个控制器是一条单独的总线，地址按端口给就不会重
    uint8_t address = (uint8_t)(port + 1);
    if (uhci_control(&p->dev, USB_TYPE_TO_DEVICE, USB_REQ_SET_ADDRESS, address, 0, NULL, 0) < 0) {
        return false;
    }
    uhci_sleep(20);
    p->dev.address = address;

    // 配置描述符：先读开头 9 个字节，里面有连同接口、端点描述符在内的总长度，再读全
    static uint8_t desc[UHCI_CONTROL_MAX];
    if (uhci_control(&p->dev, USB_TYPE_FROM_DEVICE, USB_REQ_GET_DESCRIPTOR, USB_DESC_CONFIGURATION << 8, 0,
                     desc, 9) < 9) {
        return false;
    }
    uint32_t total = (uint32_t)desc[2] | ((uint32_t)desc[3] << 8);
    if (total > sizeof(desc)) {
        total = sizeof(desc);
    }
    long got = uhci_control(&p->dev, USB_TYPE_FROM_DEVICE, USB_REQ_GET_DESCRIPTOR, USB_DESC_CONFIGURATION << 8, 0,
                            desc, (uint16_t)total);
    uint8_t endpoint;
    uint16_t max_packet;
    if (got < 9 || !find_keyboard(desc, (uint32_t)got, &p->interface, &endpoint, &max_packet)) {
        return false;           // 不是键盘（鼠标、U 盘……）
    }
    if (uhci_control(&p->dev, USB_TYPE_TO_DEVICE, USB_REQ_SET_CONFIGURATION, desc[5], 0, NULL, 0) < 0) {
        return false;
    }
    // 用启动协议；按键没有变化时不用重复报告（重复由这里来做）。只会启动协议的键盘可以
    // 不认这两个请求，所以不看结果
    uhci_control(&p->dev, USB_TYPE_CLASS_TO_INTERFACE, REQ_SET_PROTOCOL, PROTOCOL_BOOT, p->interface, NULL, 0);
    uhci_control(&p->dev, USB_TYPE_CLASS_TO_INTERFACE, REQ_SET_IDLE, 0, p->interface, NULL, 0);
    set_leds(p);

    memset(p->keys, 0, sizeof(p->keys));
    p->mods = 0;
    p->repeat_key = 0;
    p->errors = 0;
    uhci_interrupt_start(&p->dev, port, endpoint, max_packet);
    printf("usbkbd: keyboard ready (controller %d, port %d, %s speed)\n", controller, port,
           low_speed ? "low" : "full");
    return true;
}

static void detach(int controller, int port) {
    struct port *p = &ports[controller][port];
    if (p->state == PORT_KEYBOARD) {
        uhci_interrupt_stop(controller, port);
        printf("usbkbd: keyboard gone (controller %d, port %d)\n", controller, port);
    }
    p->state = PORT_EMPTY;
    p->seen = 0;
    p->repeat_key = 0;
}

/** 看一眼每个端口：拔掉了的放手，新插上的去认 */
static void scan_ports(void) {
    for (int c = 0; c < controller_count; c++) {
        for (int port = 0; port < UHCI_PORTS; port++) {
            struct port *p = &ports[c][port];
            bool changed = uhci_port_changed(c, port);
            bool connected = uhci_port_connected(c, port);
            if (changed || !connected) {
                detach(c, port);
                p->attaches = 0;
                continue;
            }
            if (p->state != PORT_EMPTY || ++p->seen < 2) {
                continue;
            }
            if (p->attaches++ < MAX_ATTACHES && attach(c, port)) {
                p->state = PORT_KEYBOARD;
            } else {
                uhci_port_disable(c, port);
                p->state = PORT_IGNORED;
            }
        }
    }
}

/** 读走每个键盘的报告，该重复的键重复。@return 有键正按着等重复 */
static bool poll_keyboards(void) {
    bool repeating = false;
    for (int c = 0; c < controller_count; c++) {
        for (int port = 0; port < UHCI_PORTS; port++) {
            struct port *p = &ports[c][port];
            if (p->state != PORT_KEYBOARD) {
                continue;
            }
            uint8_t data[UHCI_INTERRUPT_MAX] = {};
            long n;
            while ((n = uhci_interrupt_poll(c, port, data)) >= 0) {
                p->errors = 0;
                p->attaches = 0;        // 它是好的：以后再出错，从头数重新认了几次
                if (n >= REPORT_KEYS + 1) {
                    report(p, data);
                }
                memset(data, 0, REPORT_SIZE);
            }
            if (n == -2 && ++p->errors >= MAX_ERRORS) {
                // 多半是被拔掉了（下一次看端口时就知道）；不是的话重新认一遍
                detach(c, port);
                continue;
            }
            repeat(p);
            repeating = repeating || p->repeat_key != 0;
        }
    }
    flush();
    return repeating;
}

int main() {
    controller_count = uhci_open();
    if (controller_count == 0) {
        printf("usbkbd: no USB 1.1 controller\n");
        return 1;
    }
    if (name_register(USBKBD_NAME) != 0) {
        printf("usbkbd: cannot register name\n");
        return 1;
    }
    printf("usbkbd: driver ready (pid %d, %d controller%s)\n", getpid(), controller_count,
           controller_count == 1 ? "" : "s");

    // 报告来了控制器发中断；端口上的插拔和按住不放的键要靠定时器：有键等着重复时醒得勤，
    // 否则只为看端口醒来。没有中断线的话报告也只能定时去看
    uint64_t next_scan = 0;
    bool busy = true;
    struct ipc_msg m;
    for (;;) {
        if (uptime_ms() >= next_scan) {
            scan_ports();
            next_scan = uptime_ms() + SCAN_INTERVAL_MS;
        }
        timer_set(busy || !uhci_has_irq() ? 10 : SCAN_INTERVAL_MS);
        if (ipc_recv(IPC_ANY, &m) != 0) {
            continue;
        }
        if (m.sender == IPC_KERNEL) {
            if (m.label == IPC_LABEL_IRQ) {
                uhci_irq((int)m.data[0]);
            }
            busy = poll_keyboards();
        } else if (m.label == CONSOLE_DEBUG_EXIT) {
            struct ipc_msg done = {};
            ipc_reply((int)m.sender, &done);
            printf("usbkbd: exiting on request (CONSOLE_DEBUG_EXIT)\n");
            exit(1);
        }
        // 别的请求：没有这样的请求，不应答
    }
}
