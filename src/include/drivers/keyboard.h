#ifndef _DRIVERS_KEYBOARD_H_
#define _DRIVERS_KEYBOARD_H_

#include <types.h>

/**
 * PS/2 键盘驱动
 * 
 * 处理键盘输入，转换扫描码为 ASCII 字符
 * 支持 Shift、Ctrl、Alt 等修饰键
 */

/* 键盘端口 */
#define KEYBOARD_DATA_PORT      0x60    // 数据端口
#define KEYBOARD_STATUS_PORT    0x64    // 状态端口
#define KEYBOARD_COMMAND_PORT   0x64    // 命令端口

/* 状态寄存器标志 */
#define KEYBOARD_STATUS_OUTPUT_FULL  0x01  // 输出缓冲区满
#define KEYBOARD_STATUS_INPUT_FULL   0x02  // 输入缓冲区满

/* 特殊扫描码 */
#define SCANCODE_EXTENDED       0xE0    // 扩展键前缀
#define SCANCODE_RELEASED       0xF0    // 释放键前缀（Set 2）

/* 扩展键扫描码 */
#define EXT_SCANCODE_UP         0x48    // 向上箭头
#define EXT_SCANCODE_DOWN       0x50    // 向下箭头
#define EXT_SCANCODE_LEFT       0x4B    // 向左箭头
#define EXT_SCANCODE_RIGHT      0x4D    // 向右箭头
#define EXT_SCANCODE_HOME       0x47    // Home
#define EXT_SCANCODE_END        0x4F    // End
#define EXT_SCANCODE_PGUP       0x49    // Page Up
#define EXT_SCANCODE_PGDN       0x51    // Page Down
#define EXT_SCANCODE_INSERT     0x52    // Insert
#define EXT_SCANCODE_DELETE     0x53    // Delete

/* 特殊键码（用于按键事件） */
#define KEY_UP          0x80
#define KEY_DOWN        0x81
#define KEY_LEFT        0x82
#define KEY_RIGHT       0x83
#define KEY_HOME        0x84
#define KEY_END         0x85
#define KEY_PGUP        0x86
#define KEY_PGDN        0x87
#define KEY_INSERT      0x88
#define KEY_DELETE      0x89
#define KEY_F1          0x8A
#define KEY_F2          0x8B
#define KEY_F3          0x8C
#define KEY_F4          0x8D
#define KEY_F5          0x8E
#define KEY_F6          0x8F
#define KEY_F7          0x90
#define KEY_F8          0x91
#define KEY_F9          0x92
#define KEY_F10         0x93
#define KEY_F11         0x94
#define KEY_F12         0x95

/* 键盘缓冲区大小 */
#define KEYBOARD_BUFFER_SIZE    256     // 输入缓冲区大小
#define KEYBOARD_EVENT_BUFFER_SIZE  64  // 事件缓冲区大小

/**
 * 键盘修饰键状态
 */
typedef struct {
    bool shift;     // Shift 键状态
    bool ctrl;      // Ctrl 键状态
    bool alt;       // Alt 键状态
    bool caps_lock; // Caps Lock 状态
    bool num_lock;  // Num Lock 状态
    bool scroll_lock; // Scroll Lock 状态
} keyboard_modifiers_t;

/**
 * 按键事件类型
 */
typedef enum {
    KEY_EVENT_PRESS,    // 按键按下
    KEY_EVENT_RELEASE   // 按键释放
} key_event_type_t;

/**
 * 按键事件
 */
typedef struct {
    uint8_t scancode;               // 原始扫描码
    char ascii;                     // ASCII 字符（如果可转换）
    uint8_t keycode;                // 特殊键码（如 KEY_UP, KEY_F1 等）
    key_event_type_t type;          // 事件类型（按下/释放）
    keyboard_modifiers_t modifiers; // 修饰键状态
    bool is_extended;               // 是否为扩展键
} key_event_t;

/**
 * 按键事件处理函数类型
 */
typedef void (*key_event_handler_t)(key_event_t *event);

/**
 * 初始化键盘驱动
 */
void keyboard_init(void);

/**
 * 获取键盘修饰键状态
 * @return 修饰键状态结构
 */
keyboard_modifiers_t keyboard_get_modifiers(void);

/**
 * 检查是否有按键可读
 * @return true 如果有按键，false 否则
 */
bool keyboard_has_key(void);

/**
 * 读取一个按键（阻塞）
 * 等待直到有按键可读
 * @return ASCII 字符（如果是可打印字符），否则返回 0
 */
char keyboard_getchar(void);

/**
 * 尝试读取一个按键（非阻塞）
 * @param c 输出参数，存储读取的字符
 * @return true 如果成功读取，false 如果没有按键
 */
bool keyboard_try_getchar(char *c);

/**
 * 读取一行文本（阻塞）
 * 读取直到遇到换行符
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @return 实际读取的字符数
 */
size_t keyboard_getline(char *buffer, size_t size);

/**
 * 清空键盘缓冲区
 */
void keyboard_clear_buffer(void);

/**
 * 注册按键事件处理函数
 * @param handler 事件处理函数
 */
void keyboard_register_event_handler(key_event_handler_t handler);

/**
 * 取消注册按键事件处理函数
 */
void keyboard_unregister_event_handler(void);

/**
 * 更新键盘 LED 状态
 */
void keyboard_update_leds(void);

/**
 * 设置键盘 LED 状态
 * @param caps_lock Caps Lock LED 状态
 * @param num_lock Num Lock LED 状态
 * @param scroll_lock Scroll Lock LED 状态
 */
void keyboard_set_leds(bool caps_lock, bool num_lock, bool scroll_lock);

#endif // _DRIVERS_KEYBOARD_H_
