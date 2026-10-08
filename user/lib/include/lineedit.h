#ifndef _USERLAND_LIB_LINEEDIT_H_
#define _USERLAND_LIB_LINEEDIT_H_

// 行编辑：把键盘来的字节变成一行文字，边敲边在屏幕上显示。
//
// 命令行的提示符和程序读一行输入（console_read_line）用的是同一份：调用者自己去读输入，
// 一个字节一个字节地喂进来（line_edit_feed）；编辑器改缓冲区、回显，遇到要调用者决定的键
// （回车、Tab、Ctrl-C、Ctrl-D）就把它交回去。
//
// 认的键：
//   左、右方向键                    光标移动一格
//   Ctrl-左/右，Alt-B / Alt-F       移动一个词（词是空格隔开的）
//   Home / Ctrl-A，End / Ctrl-E     到行首、行尾
//   退格，Delete                    删光标前面、光标处的字符
//   Ctrl-K，Ctrl-U                  删到行尾、删到行首
//   Ctrl-W / Alt-退格               删光标前面的一个词
//   别的可见字符                    插在光标处
// 给了历史（line_history）的话还有：
//   上、下方向键                    翻以前的行
//   Ctrl-R                          在历史里找：接着敲的字是要找的内容，每敲一个就显示最近的含有
//                                   它的那一行；再按 Ctrl-R 找更早的，退格改要找的内容，Ctrl-G
//                                   放弃，别的键（回车、方向键……）留下找到的行再照常起作用
// 方向键这些键来的是终端上通用的转义序列（ESC [ A 等），串口那头的终端和键盘驱动（keys.h）
// 送的一样。
//
// 屏幕上只用两样东西：输出字符，和退格（光标左移一格）。所以它不知道屏幕有多宽：一行长到
// 折了行之后，光标回不到上一行，显示会乱（缓冲区里的内容不受影响）。

#include <types.h>

#define LINE_HISTORY_MAX    16      // 记多少行，再多的挤掉最早的
#define LINE_HISTORY_LINE   256     // 每行最长（含结尾 NUL），更长的不记

struct line_history {
    int count;
    int browsing;                   // 0 = 在敲新的一行；k = 正看着倒数第 k 行
    char typed[LINE_HISTORY_LINE];  // 开始翻历史之前敲了一半的那一行，翻回来时还它
    bool searching;                 // 正在历史里找（Ctrl-R）：屏幕上是 (search)'要找的': 找到的行
    bool not_found;
    char pattern[32];               // 要找的内容
    size_t shown;                   // 找的时候屏幕上那一行有多长（光标在它的末尾）
    char lines[LINE_HISTORY_MAX][LINE_HISTORY_LINE];    // 从早到晚
};

struct line_editor {
    char *text;                     // 调用者的缓冲区，总是以 NUL 结尾
    size_t size;
    size_t len;
    size_t cursor;                  // 0 .. len
    struct line_history *history;   // NULL = 没有历史
    int escape;                     // 转义序列读到哪里了
    int param[2];                   // 转义序列里的数字：键的编号、修饰键
    int params;                     // 读到第几个数字了
};

/** 开始用 buf（size 字节）编辑一行；history 可以是 NULL */
void line_edit_init(struct line_editor *e, char *buf, size_t size, struct line_history *history);

/** 开始新的一行（上一行用完了，或者放弃了）。不动屏幕 */
void line_edit_reset(struct line_editor *e);

/**
 * 喂一个输入的字节。
 * @return 0 = 编辑器自己处理了；否则是要调用者决定的键：
 *         '\n'  回车（'\r' 也算）：这一行敲完了，在 e->text 里，已经换行、记进了历史；用完之后调 line_edit_reset
 *         '\t'、Ctrl-C (0x03)、Ctrl-D (0x04)：编辑器什么都没做
 */
int line_edit_feed(struct line_editor *e, char c);

/**
 * 把 [pos, pos + remove) 这一段换成 text 的 n 个字符，光标放到 cursor（新的一行里的位置），
 * 屏幕上只重写变了的部分。补全用它。
 * @return 放不下时什么都不做，返回 false
 */
bool line_edit_replace(struct line_editor *e, size_t pos, size_t remove, const char *text, size_t n,
                       size_t cursor);

/** 把这一行重新显示出来（屏幕上被别的输出隔开了之后），光标回到原处 */
void line_edit_show(const struct line_editor *e);

#endif // _USERLAND_LIB_LINEEDIT_H_
