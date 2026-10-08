/**
 * 键盘驱动共用的部分（见 keys.h）
 */

#include <keys.h>

char key_char(char plain, char shifted, bool shift, bool caps_lock, bool ctrl) {
    bool letter = plain >= 'a' && plain <= 'z';
    char c = shift != (letter && caps_lock) ? shifted : plain;
    if (ctrl && (letter || (c >= '@' && c <= '_'))) {
        c &= 0x1F;
    }
    return c;
}
