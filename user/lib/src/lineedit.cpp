// 行编辑（见 lineedit.h）

#include <lineedit.h>
#include <console.h>
#include <syscall.h>
#include <string.h>

enum { ESC_NONE, ESC_START, ESC_PARAMS };   // 不在转义序列里 / 刚读到 ESC / 读到了 ESC [

static void put(const char *s, size_t n) {
    if (n > 0) {
        console_write(s, n);
    }
}

/** 屏幕上的光标从这一行的 from 走到 to：往左是退格，往右是把经过的字符再写一遍 */
static void walk(const struct line_editor *e, size_t from, size_t to) {
    for (; from > to; from--) {
        put("\b", 1);
    }
    put(e->text + from, to - from);
}

void line_edit_init(struct line_editor *e, char *buf, size_t size, struct line_history *history) {
    e->text = buf;
    e->size = size;
    e->history = history;
    line_edit_reset(e);
}

void line_edit_reset(struct line_editor *e) {
    e->len = e->cursor = 0;
    e->escape = ESC_NONE;
    if (e->size > 0) {
        e->text[0] = '\0';
    }
    if (e->history) {
        e->history->browsing = 0;
    }
}

bool line_edit_replace(struct line_editor *e, size_t pos, size_t remove, const char *text, size_t n,
                       size_t cursor) {
    if (pos + remove > e->len || e->len - remove + n >= e->size) {
        return false;
    }
    // 开头一样的不用重写：补全通常只是在词的后面接着写
    while (remove > 0 && n > 0 && e->text[pos] == *text) {
        pos++, text++, remove--, n--;
    }

    walk(e, e->cursor, pos);
    size_t at = pos;                // 屏幕上的光标在哪里
    if (remove > 0 || n > 0) {
        size_t old_len = e->len;
        memmove(e->text + pos + n, e->text + pos + remove, old_len - pos - remove);
        memcpy(e->text + pos, text, n);
        e->len = old_len - remove + n;
        e->text[e->len] = '\0';
        put(e->text + pos, e->len - pos);
        // 变短了：原来多出来的那几格擦掉
        for (at = e->len; at < old_len; at++) {
            put(" ", 1);
        }
        for (; at > e->len; at--) {
            put("\b", 1);
        }
    }
    walk(e, at, cursor);
    e->cursor = cursor;
    return true;
}

void line_edit_show(const struct line_editor *e) {
    put(e->text, e->len);
    walk(e, e->len, e->cursor);
}

static void move(struct line_editor *e, size_t to) {
    line_edit_replace(e, e->cursor, 0, "", 0, to);
}

static void erase(struct line_editor *e, size_t pos) {
    line_edit_replace(e, pos, 1, "", 0, pos);
}

/** 换成历史里倒数第 to 行；0 是翻历史之前正在敲的那一行 */
static void browse(struct line_editor *e, int to) {
    struct line_history *h = e->history;
    if (!h || to < 0 || to > h->count) {
        return;
    }
    if (h->browsing == 0) {
        size_t n = e->len < sizeof(h->typed) - 1 ? e->len : sizeof(h->typed) - 1;
        memcpy(h->typed, e->text, n);
        h->typed[n] = '\0';
    }
    const char *line = to == 0 ? h->typed : h->lines[h->count - to];
    size_t n = strlen(line);
    if (line_edit_replace(e, 0, e->len, line, n, n)) {
        h->browsing = to;
    }
}

/** 记下敲完的一行。空行和跟上一行一样的不记 */
static void remember(struct line_history *h, const char *line, size_t len) {
    if (len == 0 || len >= LINE_HISTORY_LINE || (h->count > 0 && strcmp(h->lines[h->count - 1], line) == 0)) {
        return;
    }
    if (h->count == LINE_HISTORY_MAX) {
        memmove(h->lines[0], h->lines[1], sizeof(h->lines) - sizeof(h->lines[0]));
        h->count--;
    }
    strcpy(h->lines[h->count++], line);
}

/** 一个转义序列读完了：final 是它的最后一个字节，param 是前面的数字 */
static void escape_key(struct line_editor *e, char final, int param) {
    if (final == '~') {
        // ESC [ <数字> ~：各种终端对 Home 和 End 有不同的写法
        final = param == 1 || param == 7 ? 'H' : param == 4 || param == 8 ? 'F' : param == 3 ? '~' : 0;
    }
    switch (final) {
    case 'A': browse(e, e->history ? e->history->browsing + 1 : 0); break;     // 上
    case 'B': browse(e, e->history ? e->history->browsing - 1 : 0); break;     // 下
    case 'C': move(e, e->cursor < e->len ? e->cursor + 1 : e->len); break;      // 右
    case 'D': move(e, e->cursor > 0 ? e->cursor - 1 : 0); break;                // 左
    case 'H': move(e, 0); break;                                                // Home
    case 'F': move(e, e->len); break;                                           // End
    case '~':                                                                   // Delete
        if (e->cursor < e->len) {
            erase(e, e->cursor);
        }
        break;
    }
}

int line_edit_feed(struct line_editor *e, char c) {
    if (e->escape == ESC_START) {
        // ESC [ 和 ESC O 后面是键；别的（Alt 加一个键）整个丢掉
        e->escape = c == '[' || c == 'O' ? ESC_PARAMS : ESC_NONE;
        e->param = 0;
        return 0;
    }
    if (e->escape == ESC_PARAMS) {
        if (c >= '0' && c <= '9') {
            e->param = e->param * 10 + (c - '0');
        } else if (c < 0x40) {
            e->param = -1;          // 带修饰键的写法（ESC [ 1 ; 5 C）：数字不是键的编号
        } else {
            e->escape = ESC_NONE;
            escape_key(e, c, e->param);
        }
        return 0;
    }

    switch (c) {
    case 0x1B:
        e->escape = ESC_START;
        return 0;
    case '\r':
    case '\n':
        put("\n", 1);
        if (e->history) {
            e->history->browsing = 0;
            remember(e->history, e->text, e->len);
        }
        return '\n';
    case '\t':
    case CONSOLE_CTRL_C:
    case CONSOLE_CTRL_D:
        return c;
    case 0x7F:
    case '\b':
        if (e->cursor > 0) {
            erase(e, e->cursor - 1);
        }
        return 0;
    case 0x01:                      // Ctrl-A
        move(e, 0);
        return 0;
    case 0x05:                      // Ctrl-E
        move(e, e->len);
        return 0;
    }
    if ((unsigned char)c >= 0x20) {
        line_edit_replace(e, e->cursor, 0, &c, 1, e->cursor + 1);
    }
    return 0;
}
