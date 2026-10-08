// 行编辑（见 lineedit.h）

#include <lineedit.h>
#include <console.h>
#include <syscall.h>
#include <string.h>
#include <stdio.h>

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
        e->history->searching = false;
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

/** 删掉 [from, to)，光标留在 from */
static void erase(struct line_editor *e, size_t from, size_t to) {
    line_edit_replace(e, from, to - from, "", 0, from);
}

/** 光标左边 / 右边那个词的开头 / 结尾：先越过空格，再越过词 */
static size_t word_left(const struct line_editor *e) {
    size_t pos = e->cursor;
    while (pos > 0 && e->text[pos - 1] == ' ') {
        pos--;
    }
    while (pos > 0 && e->text[pos - 1] != ' ') {
        pos--;
    }
    return pos;
}

static size_t word_right(const struct line_editor *e) {
    size_t pos = e->cursor;
    while (pos < e->len && e->text[pos] == ' ') {
        pos++;
    }
    while (pos < e->len && e->text[pos] != ' ') {
        pos++;
    }
    return pos;
}

// ============================================================================
// 历史
// ============================================================================

/**
 * 屏幕上原来是一行长 old_len 的字、光标在 old_cursor：整个换成 text 的 n 个字符，光标到 cursor。
 * 换的是整行（翻历史、进出查找），所以不像 line_edit_replace 那样只重写变了的部分
 */
static void repaint(size_t old_len, size_t old_cursor, const char *text, size_t n, size_t cursor) {
    for (; old_cursor > 0; old_cursor--) {
        put("\b", 1);
    }
    put(text, n);
    size_t at = n;
    for (; at < old_len; at++) {
        put(" ", 1);
    }
    for (; at > cursor; at--) {
        put("\b", 1);
    }
}

/**
 * 把缓冲区换成历史里倒数第 to 行（0 是翻历史之前正在敲的那一行），光标在行尾。不动屏幕。
 * @return 没有这一行，或者缓冲区放不下，返回 false
 */
static bool history_load(struct line_editor *e, int to) {
    struct line_history *h = e->history;
    if (to < 0 || to > h->count) {
        return false;
    }
    if (h->browsing == 0) {
        size_t n = e->len < sizeof(h->typed) - 1 ? e->len : sizeof(h->typed) - 1;
        memcpy(h->typed, e->text, n);
        h->typed[n] = '\0';
    }
    const char *line = to == 0 ? h->typed : h->lines[h->count - to];
    size_t n = strlen(line);
    if (n >= e->size) {
        return false;
    }
    memcpy(e->text, line, n + 1);
    e->len = e->cursor = n;
    h->browsing = to;
    return true;
}

/** 上下键：换成历史里倒数第 to 行 */
static void browse(struct line_editor *e, int to) {
    size_t old_len = e->len, old_cursor = e->cursor;
    if (e->history && history_load(e, to)) {
        repaint(old_len, old_cursor, e->text, e->len, e->cursor);
    }
}

/** 查找时屏幕上的那一行。屏幕上原来是长 old_len 的一行，光标在 old_cursor */
static void search_paint(struct line_editor *e, size_t old_len, size_t old_cursor) {
    struct line_history *h = e->history;
    char view[LINE_HISTORY_LINE + sizeof(h->pattern) + 24];
    size_t n = (size_t)snprintf(view, sizeof(view), "(%s)'%s': %s", h->not_found ? "not found" : "search",
                                h->pattern, e->text);
    n = n < sizeof(view) ? n : sizeof(view) - 1;
    repaint(old_len, old_cursor, view, n, n);
    h->shown = n;
}

/** 从倒数第 from 行起往早的找含有 pattern 的一行，找到就换上；找不到时原来的那一行留着 */
static void search_find(struct line_editor *e, int from) {
    struct line_history *h = e->history;
    h->not_found = h->pattern[0] != '\0';
    for (int k = from; h->not_found && k <= h->count; k++) {
        if (strstr(h->lines[h->count - k], h->pattern) && history_load(e, k)) {
            h->not_found = false;
        }
    }
}

/** 查找结束：屏幕上换回正在编辑的那一行 */
static void search_end(struct line_editor *e) {
    e->history->searching = false;
    repaint(e->history->shown, e->history->shown, e->text, e->len, e->cursor);
}

/**
 * 查找时来了一个字节。
 * @return 用掉了；false = 这个键不归查找管：查找已经结束，照平常的处理
 */
static bool search_key(struct line_editor *e, char c) {
    struct line_history *h = e->history;
    size_t n = strlen(h->pattern);
    if (c == 0x12) {                                // Ctrl-R：更早的一行
        search_find(e, h->browsing + 1);
    } else if (c == 0x7F || c == '\b' || (unsigned char)c >= 0x20) {
        // 要找的内容变了：从最近的一行重新找
        if (c == 0x7F || c == '\b') {
            n -= n > 0;
        } else if (n + 1 < sizeof(h->pattern)) {
            h->pattern[n++] = c;
        }
        h->pattern[n] = '\0';
        search_find(e, 1);
    } else {
        if (c == 0x07 && h->browsing != 0) {        // Ctrl-G：放弃，回到找之前敲的那一行
            history_load(e, 0);
        }
        search_end(e);
        return c == 0x07;
    }
    search_paint(e, h->shown, h->shown);
    return true;
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

// ============================================================================
// 键
// ============================================================================

/**
 * 一个转义序列读完了：ESC [ <key> ; <modifier> <final>。
 * modifier 是 1 加上按着的修饰键（Shift 1、Alt 2、Ctrl 4），没有就是 0
 */
static void escape_key(struct line_editor *e, char final, int key, int modifier) {
    bool by_word = modifier > 1 && ((modifier - 1) & 6);        // 按着 Ctrl 或 Alt
    if (final == '~') {
        // ESC [ <数字> ~：各种终端对 Home 和 End 有不同的写法
        final = key == 1 || key == 7 ? 'H' : key == 4 || key == 8 ? 'F' : key == 3 ? '~' : 0;
    }
    switch (final) {
    case 'A': browse(e, e->history ? e->history->browsing + 1 : 0); break;     // 上
    case 'B': browse(e, e->history ? e->history->browsing - 1 : 0); break;     // 下
    case 'C': move(e, by_word ? word_right(e) : e->cursor < e->len ? e->cursor + 1 : e->len); break;   // 右
    case 'D': move(e, by_word ? word_left(e) : e->cursor > 0 ? e->cursor - 1 : 0); break;              // 左
    case 'H': move(e, 0); break;                                                // Home
    case 'F': move(e, e->len); break;                                           // End
    case '~':                                                                   // Delete
        if (e->cursor < e->len) {
            erase(e, e->cursor, e->cursor + 1);
        }
        break;
    }
}

int line_edit_feed(struct line_editor *e, char c) {
    if (e->history && e->history->searching && search_key(e, c)) {
        return 0;
    }

    if (e->escape == ESC_START) {
        // ESC [ 和 ESC O 后面是键；ESC 加一个字符是 Alt 加那个键，不认识的整个丢掉
        e->escape = c == '[' || c == 'O' ? ESC_PARAMS : ESC_NONE;
        e->param[0] = e->param[1] = e->params = 0;
        switch (c) {
        case 'b':   move(e, word_left(e)); break;
        case 'f':   move(e, word_right(e)); break;
        case 0x7F:  erase(e, word_left(e), e->cursor); break;
        }
        return 0;
    }
    if (e->escape == ESC_PARAMS) {
        if (c >= '0' && c <= '9') {
            if (e->params < 2) {
                e->param[e->params] = e->param[e->params] * 10 + (c - '0');
            }
        } else if (c < 0x40) {
            e->params++;            // ';'：下一个数字
        } else {
            e->escape = ESC_NONE;
            escape_key(e, c, e->param[0], e->param[1]);
        }
        return 0;
    }

    switch (c) {
    case 0x1B:
        e->escape = ESC_START;
        return 0;
    case 0x12:                      // Ctrl-R
        if (e->history) {
            e->history->searching = true;
            e->history->not_found = false;
            e->history->pattern[0] = '\0';
            search_paint(e, e->len, e->cursor);
        }
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
            erase(e, e->cursor - 1, e->cursor);
        }
        return 0;
    case 0x0B:                      // Ctrl-K
        erase(e, e->cursor, e->len);
        return 0;
    case 0x15:                      // Ctrl-U
        erase(e, 0, e->cursor);
        return 0;
    case 0x17:                      // Ctrl-W
        erase(e, word_left(e), e->cursor);
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
