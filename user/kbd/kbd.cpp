// kbd - 用户态 PS/2 键盘驱动
//
// PC 的键盘接在键盘控制器（8042）上：每按下或松开一个键，控制器把一个扫描码放进数据
// 端口并发一次中断。这里认领那个中断，把扫描码翻译成字符，交给 console 服务（console_input，
// 见 <console.h>）：终端的输入归谁由它管，键盘敲的和串口收到的从那里起走同一条路，
// 命令行和程序不知道输入是从哪里来的。
//
// 控制器的两个端口和中断线不写在这里：init 许可给本进程的就是（hw_find）。
// 键盘布局是美式的。笔记本的内置键盘也是这个接口。

#include <syscall.h>
#include <stdio.h>
#include <names.h>
#include <console.h>
#include <keys.h>

// 状态端口的位
#define STATUS_OUTPUT_FULL  0x01    // 数据端口里有一个字节等着读
#define STATUS_INPUT_FULL   0x02    // 上次写给控制器的字节它还没取走
#define STATUS_FROM_MOUSE   0x20    // 等着读的那个字节来自第二个接口（鼠标）

// 写到命令端口的控制器命令
#define CMD_READ_CONFIG     0x20
#define CMD_WRITE_CONFIG    0x60
#define CMD_DISABLE_MOUSE   0xA7
#define CMD_DISABLE_KEYBOARD 0xAD
#define CMD_ENABLE_KEYBOARD 0xAE

// 配置字节的位
#define CONFIG_KEYBOARD_IRQ 0x01
#define CONFIG_MOUSE_IRQ    0x02
#define CONFIG_KEYBOARD_OFF 0x10
#define CONFIG_TRANSLATE    0x40    // 把键盘发来的扫描码翻译成第 1 套（下面的表是第 1 套）

// 写到数据端口、发给键盘本身的命令
#define KEYBOARD_SET_LEDS   0xED
#define LED_CAPS_LOCK       0x04

// 扫描码（第 1 套）：按下是下面的值，松开是它加上 0x80
#define KEY_RELEASED        0x80
#define KEY_CTRL            0x1D
#define KEY_LEFT_SHIFT      0x2A
#define KEY_RIGHT_SHIFT     0x36
#define KEY_CAPS_LOCK       0x3A
#define KEY_ENTER           0x1C
#define KEY_SLASH           0x35
// 下面几个只在带 PREFIX_EXTENDED 时是这些键（不带时是小键盘上的数字）
#define KEY_HOME            0x47
#define KEY_UP              0x48
#define KEY_LEFT            0x4B
#define KEY_RIGHT           0x4D
#define KEY_END             0x4F
#define KEY_DOWN            0x50
#define KEY_DELETE          0x53
#define PREFIX_EXTENDED     0xE0    // 后面跟着的是“扩展键”的扫描码（右 Ctrl、方向键、小键盘回车……）

// 扫描码 -> 字符，下标是扫描码；0 表示这个键不产生字符
static const char plain[] =
    "\0\033" "1234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
static const char shifted[] =
    "\0\033" "!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";
static_assert(sizeof(plain) - 1 == KEY_CAPS_LOCK && sizeof(shifted) == sizeof(plain));

static uint32_t data_port, status_port;
static int kbd_irq;

static bool shift_left, shift_right, ctrl, caps_lock;
static bool extended;       // 上一个字节是 PREFIX_EXTENDED

static uint8_t status(void) {
    uint32_t v = 0;
    io_read(status_port, 1, &v);
    return (uint8_t)v;
}

static uint8_t data(void) {
    uint32_t v = 0;
    io_read(data_port, 1, &v);
    return (uint8_t)v;
}

/** 等到 status 里 mask 这些位等于 value；控制器一直不响应就放弃 */
static bool wait_status(uint8_t mask, uint8_t value) {
    for (int i = 0; i < 100000; i++) {
        if ((status() & mask) == value) {
            return true;
        }
    }
    return false;
}

static bool controller_command(uint8_t command) {
    return wait_status(STATUS_INPUT_FULL, 0) && io_write(status_port, 1, command) == 0;
}

static bool write_data(uint8_t value) {
    return wait_status(STATUS_INPUT_FULL, 0) && io_write(data_port, 1, value) == 0;
}

/** 把控制器里积着的字节读掉 */
static void flush(void) {
    for (int i = 0; i < 64 && (status() & STATUS_OUTPUT_FULL); i++) {
        data();
    }
}

// 让控制器处在我们要的状态，不管固件把它留成了什么样：键盘开着、有中断、扫描码是
// 第 1 套；鼠标接口关掉（没有人读它，它的字节会堵住键盘的）
static bool controller_init(void) {
    if (status() == 0xFF) {
        return false;           // 端口后面什么都没有
    }
    if (!controller_command(CMD_DISABLE_KEYBOARD) || !controller_command(CMD_DISABLE_MOUSE)) {
        return false;
    }
    flush();
    if (!controller_command(CMD_READ_CONFIG) || !wait_status(STATUS_OUTPUT_FULL, STATUS_OUTPUT_FULL)) {
        return false;
    }
    uint8_t config = data();
    config |= CONFIG_KEYBOARD_IRQ | CONFIG_TRANSLATE;
    config &= (uint8_t)~(CONFIG_MOUSE_IRQ | CONFIG_KEYBOARD_OFF);
    return controller_command(CMD_WRITE_CONFIG) && write_data(config) &&
           controller_command(CMD_ENABLE_KEYBOARD);
}

// 键盘对每个命令字节应答一个 0xFA，它们和扫描码一样从数据端口读到；
// 当作扫描码看是“松开了一个不存在的键”，translate 自然不理会
static void set_leds(void) {
    if (write_data(KEYBOARD_SET_LEDS)) {
        write_data(caps_lock ? LED_CAPS_LOCK : 0);
    }
}

/**
 * 一个扫描码产生的字符：一般是一个，方向键这样的编辑键是一串（见 keys.h）。
 * 不产生字符（修饰键、松开、不认识的键）返回空串
 */
static const char *translate(uint8_t code) {
    static char one[2];
    if (code == PREFIX_EXTENDED) {
        extended = true;
        return "";
    }
    bool was_extended = extended;
    extended = false;

    bool pressed = !(code & KEY_RELEASED);
    uint8_t key = code & (uint8_t)~KEY_RELEASED;

    if (was_extended) {
        // 扩展键里认右 Ctrl、小键盘的回车和斜杠、方向键和它们上面那几个编辑键。带前缀的
        // Shift 是键盘为方向键等自动插入的假按键，不能当成真的 Shift
        if (key == KEY_CTRL) {
            ctrl = pressed;
            return "";
        }
        switch (pressed ? key : 0) {
        case KEY_ENTER:     return "\n";
        case KEY_SLASH:     return "/";
        case KEY_UP:        return key_sequence(EDIT_KEY_UP);
        case KEY_DOWN:      return key_sequence(EDIT_KEY_DOWN);
        case KEY_RIGHT:     return key_sequence(ctrl ? EDIT_KEY_WORD_RIGHT : EDIT_KEY_RIGHT);
        case KEY_LEFT:      return key_sequence(ctrl ? EDIT_KEY_WORD_LEFT : EDIT_KEY_LEFT);
        case KEY_HOME:      return key_sequence(EDIT_KEY_HOME);
        case KEY_END:       return key_sequence(EDIT_KEY_END);
        case KEY_DELETE:    return key_sequence(EDIT_KEY_DELETE);
        }
        return "";
    }

    switch (key) {
    case KEY_CTRL:          ctrl = pressed; return "";
    case KEY_LEFT_SHIFT:    shift_left = pressed; return "";
    case KEY_RIGHT_SHIFT:   shift_right = pressed; return "";
    case KEY_CAPS_LOCK:
        if (pressed) {
            caps_lock = !caps_lock;
            set_leds();
        }
        return "";
    }
    if (!pressed || key >= sizeof(plain) - 1) {
        return "";
    }

    one[0] = key_char(plain[key], shifted[key], shift_left || shift_right, caps_lock, ctrl);
    return one;
}

/** 读走控制器里所有的扫描码，产生的字符交出去 */
static void drain(void) {
    char chars[CONSOLE_READ_MAX];
    uint32_t n = 0;
    for (;;) {
        uint8_t st = status();
        if (!(st & STATUS_OUTPUT_FULL)) {
            break;
        }
        uint8_t code = data();
        if (st & STATUS_FROM_MOUSE) {
            continue;
        }
        for (const char *c = translate(code); *c; c++) {
            chars[n++] = *c;
            if (n == CONSOLE_READ_MAX) {
                console_input(chars, n);
                n = 0;
            }
        }
    }
    if (n > 0) {
        console_input(chars, n);
    }
}

int main() {
    struct hw_range data_range, status_range, irq;
    if (!hw_find(HW_PORTS, 0, &data_range) || !hw_find(HW_PORTS, 1, &status_range) ||
        !hw_find(HW_IRQ, 0, &irq)) {
        printf("kbd: no keyboard controller allowed\n");
        return 1;
    }
    data_port = (uint32_t)data_range.start;
    status_port = (uint32_t)status_range.start;
    kbd_irq = (int)irq.start;

    if (!controller_init()) {
        printf("kbd: no keyboard controller found\n");
        return 1;
    }
    if (irq_claim(kbd_irq) != 0) {
        printf("kbd: cannot claim IRQ %d\n", kbd_irq);
        return 1;
    }
    if (name_register(KBD_NAME) != 0) {
        printf("kbd: cannot register name\n");
        return 1;
    }
    printf("kbd: driver ready (pid %d, irq %d)\n", getpid(), kbd_irq);
    drain();        // 认领之前就到了的扫描码不会再有中断来通知：不读走，后面的也进不来

    struct ipc_msg m;
    for (;;) {
        if (ipc_recv(IPC_ANY, &m) != 0) {
            continue;
        }
        if (m.sender == IPC_KERNEL && m.label == IPC_LABEL_IRQ) {
            drain();
            irq_ack(kbd_irq);
        }
        if (m.sender != IPC_KERNEL && m.label == CONSOLE_DEBUG_EXIT) {
            struct ipc_msg done = {};
            ipc_reply((int)m.sender, &done);
            printf("kbd: exiting on request (CONSOLE_DEBUG_EXIT)\n");
            exit(1);
        }
        // 别的请求：没有这样的请求，不应答
    }
}
