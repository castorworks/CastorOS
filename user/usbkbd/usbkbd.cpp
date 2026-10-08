// usbkbd - 用户态 USB 键盘驱动
//
// USB 键盘是低速或全速设备，接在 UHCI 控制器上（uhci.cpp）。这里做三件事：
//   1. 看着每个口：USB 设备可以随时插拔，控制器不会为此发中断，所以每隔一会儿看一眼。
//      新插上的设备复位、给地址、读它的描述符；是键盘或集线器就配置好，别的不理它。
//      集线器是“更多的口”：它自己是一个设备，口上的事（有没有设备、复位）靠发请求问它，
//      除此之外和控制器自己的口一样对待，所以集线器后面还可以接集线器。
//   2. 读键盘：键盘属于“人机接口设备”（HID）。让它用“启动协议”——所有键盘都会的一种固定
//      格式：每次 8 个字节，第 0 个是修饰键（Ctrl、Shift……各占一位），第 2 到 7 个是此刻
//      按着的键（最多 6 个）的编号。键盘在按键有变化时给一份这样的报告。
//   3. 把报告变成字符：和上一份比，多出来的键就是刚按下的。字符交给 console 服务
//      （console_input，见 <console.h>），从那里起和 PS/2 键盘、串口来的输入走同一条路。
//
// 控制器的端口和中断线不写在这里：init 许可给本进程的就是。键盘布局是美式的。

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

// 集线器
#define DESC_HUB                0x29
#define HUB_FEATURE_PORT_ENABLE     1       // 清除它：关掉那个口
#define HUB_FEATURE_PORT_RESET      4
#define HUB_FEATURE_PORT_POWER      8
#define HUB_FEATURE_C_CONNECTION    16      // 清除“插拔过”的记号
#define HUB_FEATURE_C_RESET         20      // 清除“复位做完了”的记号
// 一个口的状态和“有变化”，各 16 位，位的含义相同
#define HUB_PORT_CONNECTION     (1u << 0)
#define HUB_PORT_ENABLE         (1u << 1)
#define HUB_PORT_RESET          (1u << 4)
#define HUB_PORT_LOW_SPEED      (1u << 9)
/** 一个集线器只管它的前这么多个口 */
#define HUB_PORTS               8

// 报告
#define REPORT_SIZE             8
#define REPORT_KEYS             2       // 按着的键从第几个字节开始
#define MOD_CTRL                0x11    // 左、右 Ctrl
#define MOD_SHIFT               0x22    // 左、右 Shift
#define KEY_ROLLOVER            0x01    // 按着的键太多，键盘报不过来：这份报告不算数
#define KEY_FIRST               0x04    // 再往前的编号不是键
#define KEY_CAPS_LOCK           0x39
#define KEY_KEYPAD_FIRST        0x54
#define KEY_HOME                0x4A
#define KEY_DELETE              0x4C
#define KEY_END                 0x4D
#define KEY_RIGHT               0x4F
#define KEY_LEFT                0x50
#define KEY_DOWN                0x51
#define KEY_UP                  0x52

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
/** 每隔这么久看一眼每个口上有没有插拔 */
#define SCAN_INTERVAL_MS        250
/** 读报告连着错这么多次：把设备当成新插上的重新认一遍 */
#define MAX_ERRORS              8
/** 重新认了这么多次还是错：不再理它，直到它被拔掉 */
#define MAX_ATTACHES            3

/** 同时认得这么多个设备（键盘和集线器）。设备的地址就是它在表里的位置加 1 */
#define MAX_DEVICES             16

enum port_state {
    PORT_EMPTY,         // 上面没有设备，或者有、还没去认
    PORT_IGNORED,       // 上面的设备不是键盘也不是集线器（或者认不出来）：拔掉之前不再理它
    PORT_DEVICE,        // 上面是 devices[device]
};

// 一个口：控制器自己的，或者集线器上的
struct port {
    enum port_state state;
    int seen;                   // 连着几次看到上面有设备（刚插上时触点会抖，等它稳下来）
    int attaches;               // 认了几次
    int device;
};

enum device_kind { DEVICE_NONE, DEVICE_KEYBOARD, DEVICE_HUB };

struct device {
    enum device_kind kind;      // DEVICE_NONE：表里的这一项空着
    struct uhci_device dev;
    struct port *port;          // 它插在哪个口上
    // 键盘
    int slot;                   // 控制器定期去问它，用的是第几个位置
    uint8_t interface;
    int errors;                 // 读报告连着错了几次
    uint8_t keys[REPORT_SIZE - REPORT_KEYS];    // 上一份报告里按着的键
    uint8_t mods;
    uint8_t repeat_key;         // 正在重复的键；0 表示没有
    uint64_t repeat_at;
    // 集线器
    int port_count;
    struct port ports[HUB_PORTS];
};

static struct device devices[MAX_DEVICES];
static struct port root_ports[UHCI_MAX_CONTROLLERS][UHCI_PORTS];
static bool slot_used[UHCI_MAX_CONTROLLERS][UHCI_INTERRUPT_SLOTS];
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

static void emit(const char *chars) {
    for (; *chars; chars++) {
        pending[pending_count++] = *chars;
        if (pending_count == CONSOLE_READ_MAX) {
            flush();
        }
    }
}

// ============================================================================
// 报告 -> 字符
// ============================================================================

/**
 * 编号是 key 的键在修饰键 mods 下产生的字符：一般是一个，方向键这样的编辑键是一串（见
 * keys.h）。不产生字符返回空串
 */
static const char *translate(uint8_t key, uint8_t mods) {
    static char one[2];
    switch (key) {
    case KEY_UP:        return key_sequence(EDIT_KEY_UP);
    case KEY_DOWN:      return key_sequence(EDIT_KEY_DOWN);
    case KEY_RIGHT:     return key_sequence(EDIT_KEY_RIGHT);
    case KEY_LEFT:      return key_sequence(EDIT_KEY_LEFT);
    case KEY_HOME:      return key_sequence(EDIT_KEY_HOME);
    case KEY_END:       return key_sequence(EDIT_KEY_END);
    case KEY_DELETE:    return key_sequence(EDIT_KEY_DELETE);
    }
    if (key >= KEY_KEYPAD_FIRST && key < KEY_KEYPAD_FIRST + sizeof(keypad) - 1) {
        one[0] = keypad[key - KEY_KEYPAD_FIRST];
    } else if (key < KEY_FIRST || key >= KEY_CAPS_LOCK) {
        return "";
    } else {
        one[0] = key_char(plain[key - KEY_FIRST], shifted[key - KEY_FIRST], mods & MOD_SHIFT, caps_lock,
                          mods & MOD_CTRL);
    }
    return one;
}

static void set_leds(const struct device *p) {
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
static void report(struct device *p, const uint8_t *r) {
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
        const char *chars = translate(key, p->mods);
        if (*chars) {
            emit(chars);
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
static void repeat(struct device *p) {
    if (p->repeat_key != 0 && uptime_ms() >= p->repeat_at) {
        emit(translate(p->repeat_key, p->mods));        // 修饰键可能变了：按现在的算
        p->repeat_at = uptime_ms() + REPEAT_INTERVAL_MS;
    }
}

// ============================================================================
// 口：控制器自己的和集线器上的，对上面一个样
// ============================================================================

/** 在哪里的一个口。hub == NULL：控制器自己的第 number 个；否则是那个集线器的第 number 个 */
struct where {
    int controller;
    const struct device *hub;
    int number;
};

/** 问集线器它的一个口的状态和“有变化”。@return 它回答了 */
static bool hub_port_status(const struct where *w, uint16_t *status, uint16_t *change) {
    uint8_t data[4];
    if (uhci_control(&w->hub->dev, USB_TYPE_CLASS_FROM_PORT, USB_REQ_GET_STATUS, 0, (uint16_t)(w->number + 1),
                     data, sizeof(data)) < (long)sizeof(data)) {
        return false;
    }
    *status = (uint16_t)(data[0] | (data[1] << 8));
    *change = (uint16_t)(data[2] | (data[3] << 8));
    return true;
}

/** 置上或清除集线器一个口的一项“特性”（口从 1 编号） */
static bool hub_port_feature(const struct where *w, bool set, uint16_t feature) {
    return uhci_control(&w->hub->dev, USB_TYPE_CLASS_TO_PORT, set ? USB_REQ_SET_FEATURE : USB_REQ_CLEAR_FEATURE,
                        feature, (uint16_t)(w->number + 1), NULL, 0) >= 0;
}

/** 口上现在有没有设备，上次看过之后有没有插拔过（看完就清掉） */
static void port_look(const struct where *w, bool *connected, bool *changed) {
    if (w->hub == NULL) {
        *changed = uhci_port_changed(w->controller, w->number);
        *connected = uhci_port_connected(w->controller, w->number);
        return;
    }
    uint16_t status, change;
    if (!hub_port_status(w, &status, &change)) {
        *connected = *changed = false;      // 集线器不回答了（多半是被拔掉了）：它的口上什么都没有
        return;
    }
    *connected = status & HUB_PORT_CONNECTION;
    *changed = change & HUB_PORT_CONNECTION;
    if (*changed) {
        hub_port_feature(w, false, HUB_FEATURE_C_CONNECTION);
    }
}

/** 复位口上的设备并打开这个口：设备之后在地址 0 上。@return 上面有设备，*low_speed 是它的速度 */
static bool port_reset(const struct where *w, bool *low_speed) {
    if (w->hub == NULL) {
        return uhci_port_reset(w->controller, w->number, low_speed);
    }
    // 复位信号保持多久、之后打开这个口，都是集线器自己做；做完它记下“复位做完了”
    uint16_t status = 0, change = 0;
    if (!hub_port_feature(w, true, HUB_FEATURE_PORT_RESET)) {
        return false;
    }
    for (int i = 0; i < 20 && !(change & HUB_PORT_RESET); i++) {
        uhci_sleep(10);
        if (!hub_port_status(w, &status, &change)) {
            return false;
        }
    }
    hub_port_feature(w, false, HUB_FEATURE_C_RESET);
    uhci_sleep(20);         // 设备复位之后要缓一下才能回答
    if (!hub_port_status(w, &status, &change) || !(status & HUB_PORT_CONNECTION) ||
        !(status & HUB_PORT_ENABLE)) {
        return false;
    }
    *low_speed = status & HUB_PORT_LOW_SPEED;
    return true;
}

/** 关掉这个口：上面的设备不再收到任何东西 */
static void port_disable(const struct where *w) {
    if (w->hub == NULL) {
        uhci_port_disable(w->controller, w->number);
    } else {
        hub_port_feature(w, false, HUB_FEATURE_PORT_ENABLE);
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

/** 已经有了地址的设备 d 是键盘的话（desc 是它的配置描述符）把它配置好，开始读它 */
static bool keyboard_start(struct device *d, const uint8_t *desc, uint32_t total) {
    uint8_t endpoint;
    uint16_t max_packet;
    if (!find_keyboard(desc, total, &d->interface, &endpoint, &max_packet)) {
        return false;           // 不是键盘（鼠标、U 盘……）
    }
    bool *used = slot_used[d->dev.controller];
    for (d->slot = 0; d->slot < UHCI_INTERRUPT_SLOTS && used[d->slot]; d->slot++) {
    }
    if (d->slot == UHCI_INTERRUPT_SLOTS ||
        uhci_control(&d->dev, USB_TYPE_TO_DEVICE, USB_REQ_SET_CONFIGURATION, desc[5], 0, NULL, 0) < 0) {
        return false;
    }
    // 用启动协议；按键没有变化时不用重复报告（重复由这里来做）。只会启动协议的键盘可以
    // 不认这两个请求，所以不看结果
    uhci_control(&d->dev, USB_TYPE_CLASS_TO_INTERFACE, REQ_SET_PROTOCOL, PROTOCOL_BOOT, d->interface, NULL, 0);
    uhci_control(&d->dev, USB_TYPE_CLASS_TO_INTERFACE, REQ_SET_IDLE, 0, d->interface, NULL, 0);
    set_leds(d);
    used[d->slot] = true;
    uhci_interrupt_start(&d->dev, d->slot, endpoint, max_packet);
    d->kind = DEVICE_KEYBOARD;
    return true;
}

/** 已经有了地址的设备 d 是集线器（config 是它的配置的编号）：配置好，给它的口上电 */
static bool hub_start(struct device *d, uint8_t config) {
    if (uhci_control(&d->dev, USB_TYPE_TO_DEVICE, USB_REQ_SET_CONFIGURATION, config, 0, NULL, 0) < 0) {
        return false;
    }
    // 集线器描述符：第 2 个字节是口的个数，第 5 个是口上电之后要等多久才稳（单位 2 毫秒）
    uint8_t desc[8];
    if (uhci_control(&d->dev, USB_TYPE_CLASS_FROM_DEVICE, USB_REQ_GET_DESCRIPTOR, DESC_HUB << 8, 0,
                     desc, sizeof(desc)) < (long)sizeof(desc)) {
        return false;
    }
    d->port_count = desc[2] < HUB_PORTS ? desc[2] : HUB_PORTS;
    struct where w = { d->dev.controller, d, 0 };
    for (w.number = 0; w.number < d->port_count; w.number++) {
        hub_port_feature(&w, true, HUB_FEATURE_PORT_POWER);
    }
    uhci_sleep(desc[5] * 2 < 20 ? 20 : desc[5] * 2);
    d->kind = DEVICE_HUB;
    return true;
}

/** 口 w（它的记录是 p）上是不是一个键盘或集线器；是的话配置好，记进设备表 */
static bool attach(const struct where *w, struct port *p) {
    bool low_speed;
    if (!port_reset(w, &low_speed)) {
        return false;
    }
    int index = 0;
    while (index < MAX_DEVICES && devices[index].kind != DEVICE_NONE) {
        index++;
    }
    if (index == MAX_DEVICES) {
        return false;
    }
    struct device *d = &devices[index];
    memset(d, 0, sizeof(*d));
    // 刚复位的设备在地址 0 上。控制端点的最大包长写在设备描述符的第 7 个字节里，
    // 而最小的包长是 8：先按 8 读开头 8 个字节
    d->dev = { w->controller, 0, low_speed, 8 };
    uint8_t device[8];
    if (uhci_control(&d->dev, USB_TYPE_FROM_DEVICE, USB_REQ_GET_DESCRIPTOR, USB_DESC_DEVICE << 8, 0,
                     device, 8) < 8) {
        return false;
    }
    d->dev.max_packet = device[7];
    // 给它一个自己的地址（之后它要缓一下）
    uint8_t address = (uint8_t)(index + 1);
    if (uhci_control(&d->dev, USB_TYPE_TO_DEVICE, USB_REQ_SET_ADDRESS, address, 0, NULL, 0) < 0) {
        return false;
    }
    uhci_sleep(20);
    d->dev.address = address;

    // 配置描述符：先读开头 9 个字节，里面有连同接口、端点描述符在内的总长度，再读全
    static uint8_t desc[UHCI_CONTROL_MAX];
    if (uhci_control(&d->dev, USB_TYPE_FROM_DEVICE, USB_REQ_GET_DESCRIPTOR, USB_DESC_CONFIGURATION << 8, 0,
                     desc, 9) < 9) {
        return false;
    }
    uint32_t total = (uint32_t)desc[2] | ((uint32_t)desc[3] << 8);
    if (total > sizeof(desc)) {
        total = sizeof(desc);
    }
    long got = uhci_control(&d->dev, USB_TYPE_FROM_DEVICE, USB_REQ_GET_DESCRIPTOR, USB_DESC_CONFIGURATION << 8, 0,
                            desc, (uint16_t)total);
    if (got < 9) {
        return false;
    }
    const char *place = w->hub ? "hub port" : "port";
    if (device[4] == USB_CLASS_HUB) {
        if (!hub_start(d, desc[5])) {
            return false;
        }
        printf("usbkbd: hub ready (controller %d, %s %d, %d ports)\n", w->controller, place, w->number,
               d->port_count);
    } else {
        if (!keyboard_start(d, desc, (uint32_t)got)) {
            return false;
        }
        printf("usbkbd: keyboard ready (controller %d, %s %d, %s speed)\n", w->controller, place, w->number,
               low_speed ? "low" : "full");
    }
    d->port = p;
    p->device = index;
    return true;
}

/** 口 p 上的设备不在了（或者要重新认）：放掉它，是集线器的话连同它后面的所有设备 */
static void port_clear(struct port *p) {
    if (p->state == PORT_DEVICE) {
        struct device *d = &devices[p->device];
        if (d->kind == DEVICE_KEYBOARD) {
            uhci_interrupt_stop(d->dev.controller, d->slot);
            slot_used[d->dev.controller][d->slot] = false;
            printf("usbkbd: keyboard gone (controller %d)\n", d->dev.controller);
        } else {
            for (int n = 0; n < d->port_count; n++) {
                port_clear(&d->ports[n]);
            }
            printf("usbkbd: hub gone (controller %d)\n", d->dev.controller);
        }
        d->kind = DEVICE_NONE;
    }
    p->state = PORT_EMPTY;
    p->seen = 0;
}

/** 看一眼口 w（它的记录是 p）：设备拔掉了就放手，新插上的去认 */
static void scan_port(const struct where *w, struct port *p) {
    bool connected, changed;
    port_look(w, &connected, &changed);
    if (changed || !connected) {
        port_clear(p);
        p->attaches = 0;
        return;
    }
    if (p->state != PORT_EMPTY || ++p->seen < 2) {
        return;
    }
    if (p->attaches++ < MAX_ATTACHES && attach(w, p)) {
        p->state = PORT_DEVICE;
    } else {
        port_disable(w);
        p->state = PORT_IGNORED;
    }
}

/** 看一眼所有的口：先是控制器自己的，再是每个集线器上的 */
static void scan_ports(void) {
    for (int c = 0; c < controller_count; c++) {
        for (int n = 0; n < UHCI_PORTS; n++) {
            struct where w = { c, NULL, n };
            scan_port(&w, &root_ports[c][n]);
        }
    }
    // 集线器被拔掉的话，上面那一遍（或者它所在的集线器的那一遍）已经把它从表里拿掉了
    for (int i = 0; i < MAX_DEVICES; i++) {
        struct device *d = &devices[i];
        for (int n = 0; d->kind == DEVICE_HUB && n < d->port_count; n++) {
            struct where w = { d->dev.controller, d, n };
            scan_port(&w, &d->ports[n]);
        }
    }
}

/** 读走每个键盘的报告，该重复的键重复。@return 有键正按着等重复 */
static bool poll_keyboards(void) {
    bool repeating = false;
    for (int i = 0; i < MAX_DEVICES; i++) {
        struct device *d = &devices[i];
        if (d->kind != DEVICE_KEYBOARD) {
            continue;
        }
        uint8_t data[UHCI_INTERRUPT_MAX] = {};
        long n;
        while ((n = uhci_interrupt_poll(d->dev.controller, d->slot, data)) >= 0) {
            d->errors = 0;
            d->port->attaches = 0;      // 它是好的：以后再出错，从头数重新认了几次
            if (n >= REPORT_KEYS + 1) {
                report(d, data);
            }
            memset(data, 0, REPORT_SIZE);
        }
        if (n == -2 && ++d->errors >= MAX_ERRORS) {
            // 多半是被拔掉了（下一次看它的口时就知道）；不是的话重新认一遍
            port_clear(d->port);
            continue;
        }
        repeat(d);
        repeating = repeating || d->repeat_key != 0;
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

    // 报告来了控制器发中断；口上的插拔和按住不放的键要靠定时器：有键等着重复时醒得勤，
    // 否则只为看那些口醒来。没有中断线的话报告也只能定时去看
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
