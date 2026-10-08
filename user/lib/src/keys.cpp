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

const char *key_sequence(int edit_key) {
    static const char *const sequences[] = {
        "\033[A", "\033[B", "\033[C", "\033[D", "\033[H", "\033[F", "\033[3~", "\033[1;5C", "\033[1;5D",
    };
    return sequences[edit_key];
}
