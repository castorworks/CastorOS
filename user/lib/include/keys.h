#ifndef _USERLAND_LIB_KEYS_H_
#define _USERLAND_LIB_KEYS_H_

// 键盘驱动（kbd、usbkbd）共用：一个键在修饰键的作用下产生哪个字符。
// 哪个键对应哪两个字符由驱动自己的表决定（两种键盘给键编号的办法不同）。

/**
 * plain / shifted：这个键不按和按着 Shift 时的字符（美式布局）。
 * Caps Lock 只影响字母，和 Shift 互相抵消；Ctrl 把字母和 @ 到 _ 变成控制字符
 * （Ctrl-C 是 0x03，Ctrl-D 是 0x04）。
 */
char key_char(char plain, char shifted, bool shift, bool caps_lock, bool ctrl);

#endif // _USERLAND_LIB_KEYS_H_
