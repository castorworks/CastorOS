#ifndef _USERLAND_LIB_KEYS_H_
#define _USERLAND_LIB_KEYS_H_

// 键盘驱动（kbd、usbkbd）共用：一个键在修饰键的作用下产生哪个字符，方向键这些键产生什么。
// 哪个键对应哪两个字符由驱动自己的表决定（两种键盘给键编号的办法不同）。

/**
 * plain / shifted：这个键不按和按着 Shift 时的字符（美式布局）。
 * Caps Lock 只影响字母，和 Shift 互相抵消；Ctrl 把字母和 @ 到 _ 变成控制字符
 * （Ctrl-C 是 0x03，Ctrl-D 是 0x04）。
 */
char key_char(char plain, char shifted, bool shift, bool caps_lock, bool ctrl);

/** 不产生字符、用来编辑一行的键 */
enum {
    EDIT_KEY_UP,
    EDIT_KEY_DOWN,
    EDIT_KEY_RIGHT,
    EDIT_KEY_LEFT,
    EDIT_KEY_HOME,
    EDIT_KEY_END,
    EDIT_KEY_DELETE,
    EDIT_KEY_WORD_RIGHT,    // Ctrl 加右、左方向键：移动一个词
    EDIT_KEY_WORD_LEFT,
};

/**
 * 这样一个键交给 console 的字节：终端上通用的转义序列（方向键上是 ESC [ A）。串口那头的
 * 终端送来的也是它们，所以读输入的一方（lineedit.h）不用分键是从哪里来的。
 */
const char *key_sequence(int edit_key);

#endif // _USERLAND_LIB_KEYS_H_
