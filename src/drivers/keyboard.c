// ============================================================================
// keyboard.c - PS/2 键盘驱动
// ============================================================================

#include <drivers/keyboard.h>
#include <kernel/io.h>
#include <kernel/irq.h>
#include <kernel/isr.h>
#include <kernel/task.h>
#include <lib/klog.h>
#include <lib/string.h>

/* 键盘缓冲区 */
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static volatile size_t buffer_read_pos = 0;
static volatile size_t buffer_write_pos = 0;

/* 修饰键状态 */
static keyboard_modifiers_t modifiers = {
    .shift = false,
    .ctrl = false,
    .alt = false,
    .caps_lock = false,
    .num_lock = false,
    .scroll_lock = false
};

/* 扩展键标志 */
static bool is_extended = false;

/* 按键事件处理器 */
static key_event_handler_t event_handler = NULL;

/**
 * US QWERTY 键盘布局映射表
 * 索引为扫描码，值为对应的 ASCII 字符
 */
static const char scancode_to_ascii[128] = {
    // 0x00 - 0x0F
    0,    27,  '1',  '2',  '3',  '4',  '5',  '6',
    '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
    // 0x10 - 0x1F
    'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
    'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',
    // 0x20 - 0x2F
    'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',
    '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',
    // 0x30 - 0x3F
    'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',
    0,    ' ',  0,    0,    0,    0,    0,    0,
    // 0x40 - 0x4F (功能键等，暂不处理)
    0,    0,    0,    0,    0,    0,    0,    '7',
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
    // 0x50 - 0x5F
    '2',  '3',  '0',  '.',  0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    // 0x60 - 0x6F
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    // 0x70 - 0x7F
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0
};

/**
 * Shift 键映射表
 * 按下 Shift 时的字符映射
 */
static const char scancode_to_ascii_shift[128] = {
    // 0x00 - 0x0F
    0,    27,  '!',  '@',  '#',  '$',  '%',  '^',
    '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
    // 0x10 - 0x1F
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
    'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',
    // 0x20 - 0x2F
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
    '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',
    // 0x30 - 0x3F
    'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',
    0,    ' ',  0,    0,    0,    0,    0,    0,
    // 其余与普通表相同
    // 0x40 - 0x4F
    0,    0,    0,    0,    0,    0,    0,    '7',
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
    // 0x50 - 0x5F
    '2',  '3',  '0',  '.',  0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    // 0x60 - 0x7F 全部为 0
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0
};

/**
 * 将字符放入缓冲区
 * 
 * 注意：此实现在理论上存在竞态条件（中断可能在更新 buffer_write_pos 时打断读取操作）
 * 但在单核 x86 系统中，由于环形缓冲区的简单性和操作的原子性，实际影响极小。
 * 如果需要严格的线程安全，可以在读写时禁用中断（cli/sti）。
 */
static void buffer_put(char c) {
    size_t next_write = (buffer_write_pos + 1) % KEYBOARD_BUFFER_SIZE;
    if (next_write != buffer_read_pos) {
        keyboard_buffer[buffer_write_pos] = c;
        buffer_write_pos = next_write;
    }
    // 如果缓冲区满，静默丢弃输入（生产环境可考虑添加日志）
}

/**
 * 处理扩展键并返回特殊键码
 */
static uint8_t handle_extended_key(uint8_t scancode) {
    switch (scancode) {
        case EXT_SCANCODE_UP:     return KEY_UP;
        case EXT_SCANCODE_DOWN:   return KEY_DOWN;
        case EXT_SCANCODE_LEFT:   return KEY_LEFT;
        case EXT_SCANCODE_RIGHT:  return KEY_RIGHT;
        case EXT_SCANCODE_HOME:   return KEY_HOME;
        case EXT_SCANCODE_END:    return KEY_END;
        case EXT_SCANCODE_PGUP:   return KEY_PGUP;
        case EXT_SCANCODE_PGDN:   return KEY_PGDN;
        case EXT_SCANCODE_INSERT: return KEY_INSERT;
        case EXT_SCANCODE_DELETE: return KEY_DELETE;
        default:                  return 0;
    }
}

/**
 * 触发按键事件
 */
static void trigger_key_event(uint8_t scancode, char ascii, uint8_t keycode, 
                               key_event_type_t type, bool is_ext) {
    if (event_handler != NULL) {
        key_event_t event = {
            .scancode = scancode,
            .ascii = ascii,
            .keycode = keycode,
            .type = type,
            .modifiers = modifiers,
            .is_extended = is_ext
        };
        event_handler(&event);
    }
}

/**
 * 从缓冲区获取字符
 */
static bool buffer_get(char *c) {
    if (buffer_read_pos == buffer_write_pos) {
        return false;  // 缓冲区为空
    }
    *c = keyboard_buffer[buffer_read_pos];
    buffer_read_pos = (buffer_read_pos + 1) % KEYBOARD_BUFFER_SIZE;
    return true;
}

/**
 * 键盘中断处理函数
 */
static void keyboard_callback(registers_t *regs) {
    (void)regs;  // 未使用参数
    
    /* 读取扫描码 */
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    
    /* 处理扩展键前缀 */
    if (scancode == SCANCODE_EXTENDED) {
        is_extended = true;
        return;
    }
    
    /* 判断是按下还是释放 */
    bool is_release = (scancode & 0x80) != 0;
    scancode &= 0x7F;  // 移除释放标志
    
    /* 处理修饰键 */
    switch (scancode) {
        case 0x2A:  // Left Shift
        case 0x36:  // Right Shift
            modifiers.shift = !is_release;
            is_extended = false;
            return;
            
        case 0x1D:  // Ctrl
            modifiers.ctrl = !is_release;
            is_extended = false;
            return;
            
        case 0x38:  // Alt
            modifiers.alt = !is_release;
            is_extended = false;
            return;
            
        case 0x3A:  // Caps Lock
            if (!is_release) {
                modifiers.caps_lock = !modifiers.caps_lock;
                keyboard_update_leds();  // 更新 LED
            }
            is_extended = false;
            return;
            
        case 0x45:  // Num Lock
            if (!is_release) {
                modifiers.num_lock = !modifiers.num_lock;
                keyboard_update_leds();  // 更新 LED
            }
            is_extended = false;
            return;
            
        case 0x46:  // Scroll Lock
            if (!is_release) {
                modifiers.scroll_lock = !modifiers.scroll_lock;
                keyboard_update_leds();  // 更新 LED
            }
            is_extended = false;
            return;
    }
    
    /* 处理扩展键 */
    if (is_extended) {
        uint8_t keycode = handle_extended_key(scancode);
        if (keycode != 0) {
            /* 触发扩展键事件 */
            trigger_key_event(scancode, 0, keycode, 
                            is_release ? KEY_EVENT_RELEASE : KEY_EVENT_PRESS, 
                            true);
            
            /* 对于按下事件，生成 ANSI 转义序列到缓冲区 */
            if (!is_release) {
                /* 生成 ANSI 转义序列: ESC [ X */
                buffer_put(0x1B);  // ESC
                buffer_put('[');
                
                switch (keycode) {
                    case KEY_UP:     buffer_put('A'); break;
                    case KEY_DOWN:   buffer_put('B'); break;
                    case KEY_RIGHT:  buffer_put('C'); break;
                    case KEY_LEFT:   buffer_put('D'); break;
                    case KEY_HOME:   buffer_put('H'); break;
                    case KEY_END:    buffer_put('F'); break;
                    case KEY_PGUP:   buffer_put('5'); buffer_put('~'); break;
                    case KEY_PGDN:   buffer_put('6'); buffer_put('~'); break;
                    case KEY_INSERT: buffer_put('2'); buffer_put('~'); break;
                    case KEY_DELETE: buffer_put('3'); buffer_put('~'); break;
                }
            }
        }
        is_extended = false;
        return;
    }
    
    /* 转换扫描码为 ASCII */
    char ascii = 0;
    
    if (modifiers.shift) {
        ascii = scancode_to_ascii_shift[scancode];
    } else {
        ascii = scancode_to_ascii[scancode];
    }
    
    /* 处理 Caps Lock */
    if (modifiers.caps_lock && ascii >= 'a' && ascii <= 'z') {
        ascii -= 32;  // 转为大写
    } else if (modifiers.caps_lock && ascii >= 'A' && ascii <= 'Z') {
        ascii += 32;  // 转为小写（Shift + Caps Lock）
    }

    /* 处理 Ctrl 组合键 -> 生成控制字符 */
    if (modifiers.ctrl) {
        if (ascii >= 'a' && ascii <= 'z') {
            ascii = (ascii - 'a') + 1;  // ^a = 0x01 ... ^z = 0x1A
        } else if (ascii >= 'A' && ascii <= 'Z') {
            ascii = (ascii - 'A') + 1;
        }
    }
    
    /* 触发普通按键事件 */
    if (ascii != 0 || !is_release) {
        trigger_key_event(scancode, ascii, 0,
                        is_release ? KEY_EVENT_RELEASE : KEY_EVENT_PRESS,
                        false);
    }
    
    /* 只处理按下事件的字符输入 */
    if (is_release) {
        return;
    }
    
    /* 将字符放入缓冲区 */
    if (ascii != 0) {
        buffer_put(ascii);
    }
}

/**
 * 初始化键盘驱动
 */
void keyboard_init(void) {
    LOG_INFO_MSG("Initializing PS/2 keyboard...\n");
    
    /* 清空缓冲区 */
    buffer_read_pos = 0;
    buffer_write_pos = 0;
    memset(keyboard_buffer, 0, KEYBOARD_BUFFER_SIZE);
    
    /* 清空 PS/2 控制器硬件输出缓冲区 */
    /* 如果在初始化前有按键残留，不读取会导致控制器不发送新的中断 */
    while (inb(KEYBOARD_STATUS_PORT) & KEYBOARD_STATUS_OUTPUT_FULL) {
        inb(KEYBOARD_DATA_PORT);
    }
    
    /* 注册 IRQ 1 处理函数 */
    irq_register_handler(1, keyboard_callback);
    
    LOG_INFO_MSG("Keyboard initialized successfully\n");
    LOG_DEBUG_MSG("  Buffer size: %u bytes\n", KEYBOARD_BUFFER_SIZE);
}

/**
 * 获取修饰键状态
 */
keyboard_modifiers_t keyboard_get_modifiers(void) {
    return modifiers;
}

/**
 * 检查是否有按键可读
 */
bool keyboard_has_key(void) {
    return buffer_read_pos != buffer_write_pos;
}

/**
 * 读取一个按键（阻塞）
 */
char keyboard_getchar(void) {
    char c;
    while (!buffer_get(&c)) {
        // 在多任务环境下，使用 task_yield() 让出 CPU
        // 这样其他任务可以继续运行，而不是使用 hlt 阻塞整个 CPU
        task_yield();
    }
    return c;
}

/**
 * 尝试读取一个按键（非阻塞）
 */
bool keyboard_try_getchar(char *c) {
    return buffer_get(c);
}

/**
 * 读取一行文本（阻塞）
 */
size_t keyboard_getline(char *buffer, size_t size) {
    size_t i = 0;
    
    while (i < size - 1) {
        char c = keyboard_getchar();
        
        if (c == '\n') {
            buffer[i] = '\0';
            return i;
        } else if (c == '\b') {
            /* 处理退格 */
            if (i > 0) {
                i--;
            }
        } else {
            buffer[i++] = c;
        }
    }
    
    buffer[i] = '\0';
    return i;
}

/**
 * 清空键盘缓冲区
 */
void keyboard_clear_buffer(void) {
    buffer_read_pos = 0;
    buffer_write_pos = 0;
}

/**
 * 注册按键事件处理函数
 */
void keyboard_register_event_handler(key_event_handler_t handler) {
    event_handler = handler;
    LOG_DEBUG_MSG("Keyboard event handler registered\n");
}

/**
 * 取消注册按键事件处理函数
 */
void keyboard_unregister_event_handler(void) {
    event_handler = NULL;
    LOG_DEBUG_MSG("Keyboard event handler unregistered\n");
}

/**
 * 更新键盘 LED 状态
 */
void keyboard_update_leds(void) {
    uint8_t led_state = 0;
    
    if (modifiers.scroll_lock) led_state |= 0x01;
    if (modifiers.num_lock)    led_state |= 0x02;
    if (modifiers.caps_lock)   led_state |= 0x04;
    
    /* 等待输入缓冲区为空 */
    uint32_t timeout = 100000;
    while ((inb(KEYBOARD_STATUS_PORT) & KEYBOARD_STATUS_INPUT_FULL) && timeout > 0) {
        timeout--;
    }
    
    if (timeout == 0) {
        LOG_WARN_MSG("Keyboard LED update timeout (input buffer full)\n");
        return;
    }
    
    /* 发送 LED 命令 */
    outb(KEYBOARD_DATA_PORT, 0xED);
    
    /* 等待确认 */
    timeout = 100000;
    while ((inb(KEYBOARD_STATUS_PORT) & KEYBOARD_STATUS_INPUT_FULL) && timeout > 0) {
        timeout--;
    }
    
    if (timeout == 0) {
        LOG_WARN_MSG("Keyboard LED update timeout (waiting for ACK)\n");
        return;
    }
    
    /* 发送 LED 状态 */
    outb(KEYBOARD_DATA_PORT, led_state);
    
    LOG_DEBUG_MSG("Keyboard LEDs updated: Scroll=%d, Num=%d, Caps=%d\n",
                  modifiers.scroll_lock, modifiers.num_lock, modifiers.caps_lock);
}

/**
 * 设置键盘 LED 状态
 */
void keyboard_set_leds(bool caps_lock, bool num_lock, bool scroll_lock) {
    modifiers.caps_lock = caps_lock;
    modifiers.num_lock = num_lock;
    modifiers.scroll_lock = scroll_lock;
    keyboard_update_leds();
}
