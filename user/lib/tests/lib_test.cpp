// 用户库的宿主机测试：把库里不依赖内核的部分（printf 一族、字符串函数）用宿主机的编译器
// 编译，直接在开发机上运行（行编辑也在内：它只要一个输出字符的函数）。几秒钟就有结果，不用交叉编译，也不用启动 QEMU。
//
//   make lib-test          （顶层 Makefile；或者 make -C user/lib host-test）
//
// 依赖内核的部分（系统调用、IPC、文件和网络的客户端）测不了，那些由系统里运行的
// user/selftest 来检查。库要的几个底层函数在这个文件的最后用宿主机的系统调用补上。

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <keys.h>
#include <lineedit.h>

static int checks, failures;

static void check(bool ok, const char *what, const char *file, int line) {
    checks++;
    if (!ok) {
        failures++;
        printf("%s:%d: FAILED: %s\n", file, line, what);
    }
}
#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)

/** snprintf 的结果和返回值都要对 */
#define CHECK_FMT(want, ...) do { \
        char out_[512]; \
        int n_ = snprintf(out_, sizeof(out_), __VA_ARGS__); \
        checks++; \
        if (strcmp(out_, (want)) != 0 || n_ != (int)strlen(want)) { \
            failures++; \
            printf("%s:%d: FAILED: got [%s] (%d), want [%s]\n", __FILE__, __LINE__, out_, n_, (want)); \
        } \
    } while (0)

static void test_format_strings(void) {
    CHECK_FMT("hello world", "%s %s", "hello", "world");
    CHECK_FMT("[   ab][ab   ][000ab]", "[%5s][%-5s][%05s]", "ab", "ab", "ab");
    CHECK_FMT("(null)", "%s", (const char *)0);
    CHECK_FMT("a%b%q%", "%c%%b%q%", 'a');           // 不认识的格式符和末尾的 % 照原样输出
    CHECK_FMT("", "%s", "");
}

static void test_format_integers(void) {
    CHECK_FMT("42 -42 0", "%d %i %d", 42, -42, 0);
    CHECK_FMT("[   -7][-0007][-7   ]", "[%5d][%05d][%-5d]", -7, -7, -7);
    CHECK_FMT("-2147483648 2147483647", "%d %d", (int)(-2147483647 - 1), 2147483647);
    CHECK_FMT("-9223372036854775808", "%lld", (long long)(-9223372036854775807LL - 1));
    CHECK_FMT("18446744073709551615", "%llu", 18446744073709551615ULL);
    CHECK_FMT("4294967295 ff FF 17", "%u %x %X %o", 4294967295u, 255, 255, 15);
    CHECK_FMT("[00ff][  ff][ff  ]", "[%04x][%4x][%-4x]", 255, 255, 255);
    CHECK_FMT("123456789abcdef0", "%llx", 0x123456789abcdef0ULL);
    CHECK_FMT("0x1000 [0x00ab] [  0xab]", "%p [%06p] [%6p]", (void *)0x1000, (void *)0xab, (void *)0xab);
}

static void test_format_floats(void) {
    CHECK_FMT("3.14|-0.375000|  -1.500|3", "%.2f|%f|%8.3f|%.0f", 3.14159, -0.375, -1.5, 2.6);
    CHECK_FMT("0.000000 1.000000 123456.789000", "%f %f %f", 0.0, 1.0, 123456.789);
    CHECK_FMT("[0003.50][3.50   ]", "[%07.2f][%-7.2f]", 3.5, 3.5);
    CHECK_FMT("1000000000000000000000", "%.0f", 1e21);
    CHECK_FMT("inf -inf nan", "%f %f %f", __builtin_inf(), -__builtin_inf(), __builtin_nan(""));
    CHECK_FMT("0.100000000000000", "%.15f", 0.1);
    CHECK_FMT("0.100000000000000", "%.40f", 0.1);   // 精度最多 15 位
}

static void test_format_bounds(void) {
    // 放不下的部分被截掉，结果总是以 '\0' 结尾，返回值是实际写进去的字符数
    char small[5];
    int n = snprintf(small, sizeof(small), "%s", "truncated");
    CHECK(strcmp(small, "trun") == 0 && n == 4);
    n = snprintf(small, sizeof(small), "%d", 123456789);
    CHECK(strcmp(small, "1234") == 0 && n == 4);
    char one[1] = { 'x' };
    CHECK(snprintf(one, sizeof(one), "abc") == 0 && one[0] == '\0');
    CHECK(snprintf(small, 0, "abc") == 0);
    CHECK(snprintf(NULL, 10, "abc") == 0);
}

static void test_strings(void) {
    CHECK(strlen("") == 0 && strlen("abc") == 3);
    CHECK(strcmp("abc", "abc") == 0 && strcmp("abc", "abd") < 0 && strcmp("b", "a") > 0 && strcmp("ab", "abc") < 0);
    CHECK(strncmp("abcdef", "abcxyz", 3) == 0 && strncmp("abcdef", "abcxyz", 4) < 0 && strncmp("a", "b", 0) == 0);

    char buf[16];
    CHECK(strcpy(buf, "hello") == buf && strcmp(buf, "hello") == 0);
    CHECK(strcat(buf, " you") == buf && strcmp(buf, "hello you") == 0);
    memset(buf, 'x', sizeof(buf));
    strncpy(buf, "ab", 5);                          // 不够长的用 '\0' 补满
    CHECK(memcmp(buf, "ab\0\0\0x", 6) == 0);

    const char *text = "one two one";
    CHECK(strchr(text, 't') == text + 4 && strchr(text, 'z') == NULL);
    CHECK(strstr(text, "two") == text + 4 && strstr(text, "one") == text && strstr(text, "three") == NULL);
    CHECK(strstr(text, "") == text);
}

static void test_memory(void) {
    char a[16], b[16];
    for (int i = 0; i < 16; i++) {
        a[i] = (char)i;
    }
    CHECK(memcpy(b, a, 16) == b && memcmp(a, b, 16) == 0);
    b[7] = 100;
    CHECK(memcmp(a, b, 7) == 0 && memcmp(a, b, 8) < 0 && memcmp(b, a, 8) > 0 && memcmp(a, b, 0) == 0);
    CHECK(memset(b, 0x5A, 4) == b && b[0] == 0x5A && b[3] == 0x5A && b[4] == 4);

    // 重叠的区域：往前挪、往后挪都不能弄坏还没读的字节
    char m[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    memmove(m + 2, m, 5);
    CHECK(memcmp(m, "\0\1\0\1\2\3\4\7", 8) == 0);
    char n[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    memmove(n, n + 2, 5);
    CHECK(memcmp(n, "\2\3\4\5\6\5\6\7", 8) == 0);
}

static void test_conversions(void) {
    CHECK(isdigit('0') && isdigit('9') && !isdigit('a') && !isdigit('/'));
    CHECK(isspace(' ') && isspace('\t') && isspace('\n') && !isspace('x'));
    CHECK(atoi("0") == 0 && atoi("42") == 42 && atoi("-17") == -17 && atoi("  8") == 8 && atoi("12abc") == 12);
    CHECK(atoi("abc") == 0 && atoi("") == 0);

    char buf[40];
    CHECK(strcmp(itoa(255, buf, 16), "ff") == 0 && strcmp(itoa(-255, buf, 10), "-255") == 0);
    CHECK(strcmp(itoa(0, buf, 10), "0") == 0 && strcmp(itoa(5, buf, 2), "101") == 0);
    CHECK(strcmp(utoa(4294967295u, buf, 10), "4294967295") == 0 && strcmp(utoa(4294967295u, buf, 16), "ffffffff") == 0);
}

/** got 和 want 的相对误差不超过 1e-13（want 是 0 时看绝对误差） */
static bool close_to(double got, double want) {
    double error = got - want;
    if (error < 0) error = -error;
    double scale = want < 0 ? -want : want;
    return error <= 1e-13 * (scale > 1 ? scale : 1);
}
#define CHECK_NEAR(expr, want) check(close_to((expr), (want)), #expr " ~ " #want, __FILE__, __LINE__)

static void test_math(void) {
    CHECK(fabs(-2.5) == 2.5 && fabs(2.5) == 2.5 && fabs(0.0) == 0);
    CHECK(floor(2.7) == 2 && floor(-2.7) == -3 && floor(3.0) == 3 && floor(-0.5) == -1 && floor(1e300) == 1e300);
    CHECK(ceil(2.2) == 3 && ceil(-2.2) == -2 && ceil(3.0) == 3 && ceil(0.5) == 1);
    CHECK(fmod(7.5, 2) == 1.5 && fmod(-7.5, 2) == -1.5 && fmod(6, 3) == 0 && fmod(1, 0) != fmod(1, 0));

    CHECK(sqrt(0.0) == 0 && sqrt(4.0) == 2 && sqrt(1.0) == 1 && sqrt(-1.0) != sqrt(-1.0));
    CHECK_NEAR(sqrt(2.0), 1.4142135623730951);
    CHECK_NEAR(sqrt(1e-300), 1e-150);
    CHECK_NEAR(sqrt(1e300), 1e150);
    CHECK_NEAR(sqrt(123456789.0) * sqrt(123456789.0), 123456789.0);

    CHECK(exp(0.0) == 1);
    CHECK_NEAR(exp(1.0), M_E);
    CHECK_NEAR(exp(-1.0), 0.36787944117144233);
    CHECK_NEAR(exp(10.0), 22026.465794806718);
    CHECK_NEAR(exp(-20.0), 2.0611536224385579e-09 * 1);
    CHECK(exp(1000.0) == INFINITY && exp(-1000.0) == 0);

    CHECK(log(1.0) == 0 && log(0.0) == -INFINITY && log(-1.0) != log(-1.0));
    CHECK_NEAR(log(M_E), 1.0);
    CHECK_NEAR(log(2.0), M_LN2);
    CHECK_NEAR(log(1e10), 23.025850929940457);
    CHECK_NEAR(log(1e-10), -23.025850929940457);
    CHECK_NEAR(log(exp(3.7)), 3.7);

    CHECK(pow(2, 10) == 1024 && pow(-2, 3) == -8 && pow(2, -2) == 0.25 && pow(5, 0) == 1 && pow(0, 2) == 0);
    CHECK_NEAR(pow(2, 0.5), M_SQRT2);
    CHECK_NEAR(pow(10, -3.5), 0.00031622776601683794);
    CHECK(pow(-2, 0.5) != pow(-2, 0.5));

    CHECK(sin(0.0) == 0 && cos(0.0) == 1);
    CHECK_NEAR(sin(M_PI / 6), 0.5);
    CHECK_NEAR(cos(M_PI / 3), 0.5);
    CHECK_NEAR(sin(M_PI / 2), 1.0);
    CHECK_NEAR(cos(M_PI), -1.0);
    CHECK_NEAR(sin(M_PI), 0.0);
    CHECK_NEAR(sin(-2.0), -0.90929742682568171);
    CHECK_NEAR(cos(100.0), 0.86231887228768393);
    CHECK_NEAR(sin(1000.0), 0.82687954053200256);
    CHECK_NEAR(tan(M_PI / 4), 1.0);
    for (double x = -10; x <= 10; x += 0.37) {      // sin^2 + cos^2 = 1 处处成立
        CHECK_NEAR(sin(x) * sin(x) + cos(x) * cos(x), 1.0);
    }
}

// 键盘驱动共用的：一个键在 Shift、Caps Lock、Ctrl 下产生的字符
static void test_keys(void) {
    CHECK(key_char('a', 'A', false, false, false) == 'a');
    CHECK(key_char('a', 'A', true, false, false) == 'A');
    CHECK(key_char('a', 'A', false, true, false) == 'A');
    CHECK(key_char('a', 'A', true, true, false) == 'a');        // Caps Lock 和 Shift 互相抵消
    CHECK(key_char('1', '!', false, true, false) == '1');       // Caps Lock 只影响字母
    CHECK(key_char('1', '!', true, true, false) == '!');
    CHECK(key_char('c', 'C', false, false, true) == 0x03);      // Ctrl-C
    CHECK(key_char('d', 'D', true, false, true) == 0x04);
    CHECK(key_char('[', '{', false, false, true) == 0x1B);      // Ctrl-[ 是 ESC
    CHECK(key_char('2', '@', true, false, true) == 0x00);       // Ctrl-@
    CHECK(key_char('1', '!', false, false, true) == '1');       // 别的键 Ctrl 不起作用
    CHECK(key_char('\n', '\n', false, false, true) == '\n');
}

// ---- 行编辑：编辑器写出来的东西喂给一块假的屏幕，屏幕上的内容和光标要跟缓冲区一致 ----

static char screen[512];        // 当前这一行
static size_t screen_cursor;
static int screen_newlines;

ssize_t console_write(const void *buf, size_t count) {
    const char *s = (const char *)buf;
    for (size_t i = 0; i < count; i++) {
        if (s[i] == '\n') {
            memset(screen, 0, sizeof(screen));
            screen_cursor = 0;
            screen_newlines++;
        } else if (s[i] == '\b') {
            screen_cursor -= screen_cursor > 0;
        } else {
            screen[screen_cursor++] = s[i];
        }
    }
    return (ssize_t)count;
}

/** 屏幕上是不是这一行（末尾擦掉留下的空格不算），光标在不在 cursor */
static bool shows(const struct line_editor *e, const char *want, size_t cursor) {
    size_t n = strlen(screen);
    while (n > 0 && screen[n - 1] == ' ' && n > strlen(want)) {
        n--;
    }
    return strcmp(e->text, want) == 0 && e->len == strlen(want) && e->cursor == cursor &&
           n == strlen(want) && memcmp(screen, want, n) == 0 && screen_cursor == cursor;
}

/** 喂一串字节，返回最后一个字节的结果 */
static int type(struct line_editor *e, const char *keys) {
    int key = 0;
    for (; *keys; keys++) {
        key = line_edit_feed(e, *keys);
    }
    return key;
}

#define UP      "\033[A"
#define DOWN    "\033[B"
#define RIGHT   "\033[C"
#define LEFT    "\033[D"

static void test_line_editor(void) {
    static struct line_history history;
    static char buf[16];
    struct line_editor e;
    line_edit_init(&e, buf, sizeof(buf), &history);
    memset(screen, 0, sizeof(screen));
    screen_cursor = 0;

    // 插入、退格、左右移动、在中间改
    CHECK(type(&e, "helo") == 0 && shows(&e, "helo", 4));
    CHECK(type(&e, LEFT "l") == 0 && shows(&e, "hello", 4));
    CHECK(type(&e, RIGHT RIGHT RIGHT "!") == 0 && shows(&e, "hello!", 6));      // 到头了不再往右
    CHECK(type(&e, "\x7f") == 0 && shows(&e, "hello", 5));
    CHECK(type(&e, LEFT LEFT LEFT "\b") == 0 && shows(&e, "hllo", 1));
    CHECK(type(&e, LEFT LEFT LEFT "\b") == 0 && shows(&e, "hllo", 0));          // 行首：退格和左都不动
    CHECK(type(&e, "\033[3~") == 0 && shows(&e, "llo", 0));                     // Delete
    CHECK(type(&e, "\033[F" "\033[3~") == 0 && shows(&e, "llo", 3));            // End；行尾 Delete 不动
    CHECK(type(&e, "\033[H") == 0 && shows(&e, "llo", 0));                      // Home
    CHECK(type(&e, "\x05" "\x01" "\033[4~" "\033[1~" "\033OC") == 0 && shows(&e, "llo", 1));   // 别的写法
    CHECK(type(&e, "\033[1;2C" "\033x" "\x02") == 0 && shows(&e, "llo", 2));    // 带 Shift 的照样走；不认识的丢掉
    // 要调用者决定的键
    CHECK(type(&e, "\t") == '\t' && type(&e, "\x03") == 0x03 && type(&e, "\x04") == 0x04 && shows(&e, "llo", 2));
    // 满了：多的不要
    CHECK(type(&e, "0123456789abcdef") == 0 && shows(&e, "ll0123456789abo", 14));
    CHECK(!line_edit_replace(&e, 0, 1, "xx", 2, 0) && shows(&e, "ll0123456789abo", 14));
    // 替换一段：补全用的
    CHECK(line_edit_replace(&e, 2, 12, "x y", 3, 5) && shows(&e, "llx yo", 5));
    CHECK(line_edit_replace(&e, 0, 3, "llama", 5, 5) && shows(&e, "llama yo", 5));
    CHECK(line_edit_replace(&e, 0, 8, "", 0, 0) && shows(&e, "", 0));

    // 回车：换行，记进历史；空行和重复的不记
    int newlines = screen_newlines;
    CHECK(type(&e, "one\r") == '\n' && strcmp(buf, "one") == 0 && screen_newlines == newlines + 1);
    line_edit_reset(&e);
    CHECK(type(&e, "\n") == '\n' && e.len == 0);
    line_edit_reset(&e);
    type(&e, "two\n");
    line_edit_reset(&e);
    type(&e, "two\n");
    line_edit_reset(&e);
    CHECK(history.count == 2);

    // 上下翻历史；翻回来时敲了一半的那一行还在；翻出来的可以改，改的不写回历史
    CHECK(type(&e, "th" UP) == 0 && shows(&e, "two", 3));
    CHECK(type(&e, UP) == 0 && shows(&e, "one", 3));
    CHECK(type(&e, UP) == 0 && shows(&e, "one", 3));        // 没有更早的了
    CHECK(type(&e, DOWN DOWN) == 0 && shows(&e, "th", 2));
    CHECK(type(&e, DOWN) == 0 && shows(&e, "th", 2));
    CHECK(type(&e, UP UP "s\n") == '\n' && strcmp(buf, "ones") == 0 && history.count == 3);
    line_edit_reset(&e);
    CHECK(type(&e, UP UP UP) == 0 && shows(&e, "one", 3));
    line_edit_reset(&e);
    memset(screen, 0, sizeof(screen));
    screen_cursor = 0;

    // 记满了挤掉最早的
    for (int i = 0; i < LINE_HISTORY_MAX; i++) {
        char line[8];
        snprintf(line, sizeof(line), "n%d\n", i);
        type(&e, line);
        line_edit_reset(&e);
    }
    CHECK(history.count == LINE_HISTORY_MAX && strcmp(history.lines[0], "n0") == 0);
    CHECK(type(&e, UP) == 0 && shows(&e, "n15", 3));

    // 没有历史的编辑器：上下键和 Ctrl-R 什么也不做
    struct line_editor plain_editor;
    char small[8];
    line_edit_init(&plain_editor, small, sizeof(small), NULL);
    memset(screen, 0, sizeof(screen));
    screen_cursor = 0;
    CHECK(type(&plain_editor, "ab" UP DOWN "\x12") == 0 && shows(&plain_editor, "ab", 2));

    CHECK(strcmp(key_sequence(EDIT_KEY_UP), UP) == 0 && strcmp(key_sequence(EDIT_KEY_DELETE), "\033[3~") == 0);
}

/** 查找的时候屏幕上是不是这一行，光标在它的末尾 */
static bool screen_is(const char *want) {
    size_t n = strlen(screen);
    while (n > strlen(want) && screen[n - 1] == ' ') {
        n--;
    }
    return n == strlen(want) && memcmp(screen, want, n) == 0 && screen_cursor == n;
}

#define CTRL_R  "\x12"

static void test_line_editor_words_and_search(void) {
    static struct line_history history;
    static char buf[64];
    struct line_editor e;
    line_edit_init(&e, buf, sizeof(buf), &history);
    memset(screen, 0, sizeof(screen));
    screen_cursor = 0;

    // 按词移动：Ctrl-左右、Alt-左右、Alt-B / Alt-F
    CHECK(type(&e, "one  two three") == 0 && shows(&e, "one  two three", 14));
    CHECK(type(&e, "\033[1;5D") == 0 && shows(&e, "one  two three", 9));
    CHECK(type(&e, "\033" "b") == 0 && shows(&e, "one  two three", 5));
    CHECK(type(&e, "\033[1;3D" "\033[1;5D") == 0 && shows(&e, "one  two three", 0));
    CHECK(type(&e, "\033[1;5C") == 0 && shows(&e, "one  two three", 3));
    CHECK(type(&e, "\033" "f") == 0 && shows(&e, "one  two three", 8));
    CHECK(type(&e, "\033[1;3C" "\033[1;5C") == 0 && shows(&e, "one  two three", 14));
    // 删一个词、删到行尾、删到行首
    CHECK(type(&e, "\x17") == 0 && shows(&e, "one  two ", 9));                  // Ctrl-W
    CHECK(type(&e, "\033\x7f") == 0 && shows(&e, "one  ", 5));                  // Alt-退格：连同词后面的空格
    CHECK(type(&e, "two three" "\033[1;5D" "\x0b") == 0 && shows(&e, "one  two ", 9));      // Ctrl-K
    CHECK(type(&e, "\033[1;5D" "\x15") == 0 && shows(&e, "two ", 0));                       // Ctrl-U
    CHECK(type(&e, "\x15" "\x17" "\033[F" "\x0b") == 0 && shows(&e, "two ", 4));            // 没有可删的
    // Ctrl-Y：贴回刚删掉的；连着删的几次合在一起，中间隔了别的键就只有最后一次的
    CHECK(type(&e, "\x15" "a b c" "\x17\x17" "\x01\x19") == 0 && shows(&e, "b ca ", 3));
    CHECK(type(&e, "\033\x7f" "\033\x7f" "\x19\x19") == 0 && shows(&e, "b cb ca ", 6));       // Alt-退格也算；可以贴几次
    CHECK(type(&e, "\x01\x0b" "x" "\x19") == 0 && shows(&e, "xb cb ca ", 9));
    CHECK(type(&e, "\033[1;5D\033[1;5D" "\x0b" "\x15" "\x19") == 0 && shows(&e, "xb cb ca ", 9));  // 往前删的接在前面
    CHECK(type(&e, "\x17" LEFT "\x17" "\x19") == 0 && shows(&e, "xb cb ", 5));     // 隔了别的键：只有后一次的
    type(&e, "\x15\x0b");
    // 没有可删的不冲掉记着的；Alt-Y：把刚贴的换成更早删的一段，再按再往前
    CHECK(type(&e, "\x0b\x15" "\x19") == 0 && shows(&e, "xb cb ", 6));
    type(&e, "\x15" "aa" "\x15" "x\x7f" "bb" "\x15" "x\x7f" "cc" "\x15");
    CHECK(type(&e, "<>" LEFT "\x19") == 0 && shows(&e, "<cc>", 3));
    CHECK(type(&e, "\033y") == 0 && shows(&e, "<bb>", 3));
    CHECK(type(&e, "\033y") == 0 && shows(&e, "<aa>", 3));
    CHECK(type(&e, LEFT "\033y") == 0 && shows(&e, "<aa>", 2));                 // 中间隔了别的键：Alt-Y 不起作用
    CHECK(type(&e, "\x19" "\033y" "\033[C" "\033y") == 0 && shows(&e, "<abba>", 5));
    // 记满 8 段之后转一圈回到最近的
    type(&e, "\x01\x0b");
    for (int i = 0; i < 9; i++) {
        char keys[8];
        snprintf(keys, sizeof(keys), "k%d\x15x\x7f", i);
        type(&e, keys);
    }
    CHECK(type(&e, "\x19") == 0 && shows(&e, "k8", 2));
    CHECK(type(&e, "\033y\033y\033y\033y\033y\033y\033y") == 0 && shows(&e, "k1", 2));
    CHECK(type(&e, "\033y") == 0 && shows(&e, "k8", 2));
    type(&e, "\x15");
    line_edit_reset(&e);
    memset(screen, 0, sizeof(screen));
    screen_cursor = 0;

    const char *lines[] = { "ls /bin\n", "cat notes\n", "echo one\n", "cat other\n" };
    for (const char *line : lines) {
        type(&e, line);
        line_edit_reset(&e);
    }

    // Ctrl-R：边敲边找最近的；再按找更早的；找不到时上一次找到的留着
    CHECK(type(&e, "xy" CTRL_R) == 0 && screen_is("(search)'': xy"));
    CHECK(type(&e, "c") == 0 && screen_is("(search)'c': cat other"));
    CHECK(type(&e, "at n") == 0 && screen_is("(search)'cat n': cat notes"));
    CHECK(type(&e, "\x7f\x7f") == 0 && screen_is("(search)'cat': cat other"));
    CHECK(type(&e, CTRL_R) == 0 && screen_is("(search)'cat': cat notes"));
    CHECK(type(&e, CTRL_R) == 0 && screen_is("(not found)'cat': cat notes"));
    CHECK(type(&e, "z") == 0 && screen_is("(not found)'catz': cat notes"));
    // 回车：留下找到的那一行
    int newlines = screen_newlines;
    CHECK(type(&e, "\n") == '\n' && strcmp(buf, "cat notes") == 0 && screen_newlines == newlines + 1);
    line_edit_reset(&e);
    // 别的键：找到的那一行留下来接着编辑，这个键照常起作用；之后上下键从它那里接着翻
    CHECK(type(&e, CTRL_R "ls" LEFT) == 0 && shows(&e, "ls /bin", 6));
    CHECK(type(&e, DOWN) == 0 && shows(&e, "cat notes", 9));
    line_edit_reset(&e);
    memset(screen, 0, sizeof(screen));
    screen_cursor = 0;
    // Ctrl-G：放弃，回到找之前敲的那一行；Ctrl-C 和 Tab 交给调用者
    CHECK(type(&e, "half" LEFT CTRL_R "echo") == 0 && screen_is("(search)'echo': echo one"));
    CHECK(type(&e, "\x07") == 0 && shows(&e, "half", 4));
    CHECK(type(&e, CTRL_R "ls\t") == '\t' && shows(&e, "ls /bin", 7));
    CHECK(type(&e, CTRL_R "ec\x03") == 0x03 && shows(&e, "echo one", 8));       // 新的一次查找从最近的一行找起
    line_edit_reset(&e);
    memset(screen, 0, sizeof(screen));
    screen_cursor = 0;
    // 什么都没敲就结束：原来的行和光标都不变
    CHECK(type(&e, "abc" LEFT CTRL_R) == 0 && screen_is("(search)'': abc"));
    CHECK(type(&e, LEFT) == 0 && shows(&e, "abc", 1));

    CHECK(strcmp(key_sequence(EDIT_KEY_WORD_LEFT), "\033[1;5D") == 0);
}

int main() {
    test_keys();
    test_line_editor();
    test_line_editor_words_and_search();
    test_math();
    test_format_strings();
    test_format_integers();
    test_format_floats();
    test_format_bounds();
    test_strings();
    test_memory();
    test_conversions();

    printf("lib-test: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

// ============================================================================
// 库要的底层函数：在系统里它们由用户库的其余部分提供（最后落到系统调用上）
// ============================================================================

extern "C" long write(int fd, const void *buf, unsigned long count);

long write_out(const void *buf, size_t len) {
    return write(1, buf, len);
}

long write_err(const void *buf, size_t len) {
    return write(2, buf, len);
}

// i686 上 64 位除法要调这两个函数；宿主机上直接除
uint64_t __udivdi3(uint64_t n, uint64_t d) {
    return n / d;
}

uint64_t __umoddi3(uint64_t n, uint64_t d) {
    return n % d;
}
