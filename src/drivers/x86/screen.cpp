/**
 * 屏幕驱动：VGA 文本模式
 *
 * 文本模式下显卡把物理地址 0xB8000 开始的一块内存当成 80x25 个格子，每格两个字节：
 * 低字节是字符，高字节是颜色（低 4 位前景，高 4 位背景）。往里写，字就出现在屏幕上。
 * 光标的位置另外通过 CRT 控制器的端口（0x3D4 选寄存器，0x3D5 读写）告诉显卡。
 *
 * BIOS 启动的 PC 交给内核时就在这个模式下，不需要设置显示模式。
 */

#include <drivers/screen.h>
#include <kernel/io.h>
#include <types.h>

#define VGA_PHYS        0xB8000
#define VGA_COLS        80
#define VGA_ROWS        25

#define CRTC_INDEX      0x3D4
#define CRTC_DATA       0x3D5
#define CRTC_CURSOR_START   0x0A    // 光标从字符格的第几条扫描线开始；第 5 位是“不显示光标”
#define CRTC_CURSOR_END     0x0B
#define CRTC_CURSOR_HIGH    0x0E    // 光标位置（行 * 80 + 列）的高、低字节
#define CRTC_CURSOR_LOW     0x0F

#define ATTR_DEFAULT    0x07        // 黑底浅灰字
#define ATTR_BRIGHT     0x08

static volatile uint16_t *const cells = (volatile uint16_t *)PHYS_TO_VIRT(VGA_PHYS);
static uint32_t row, col;
static uint8_t attr = ATTR_DEFAULT;

/* 转义序列 ESC [ 参数;参数 字母 的解析状态 */
enum class Escape { NONE, ESC, CSI };
static Escape escape = Escape::NONE;
#define CSI_MAX_PARAMS  4
static uint32_t csi_params[CSI_MAX_PARAMS];
static uint32_t csi_count;

static inline uint16_t blank(void) {
    return (uint16_t)(' ' | (ATTR_DEFAULT << 8));
}

static void move_cursor(void) {
    uint32_t pos = row * VGA_COLS + col;
    outb(CRTC_INDEX, CRTC_CURSOR_HIGH);
    outb(CRTC_DATA, (uint8_t)(pos >> 8));
    outb(CRTC_INDEX, CRTC_CURSOR_LOW);
    outb(CRTC_DATA, (uint8_t)pos);
}

static void clear_row(uint32_t r) {
    for (uint32_t c = 0; c < VGA_COLS; c++) {
        cells[r * VGA_COLS + c] = blank();
    }
}

/** 光标到了最后一行之下：所有行上移一行，最后一行清空 */
static void scroll(void) {
    for (uint32_t i = 0; i < (VGA_ROWS - 1) * VGA_COLS; i++) {
        cells[i] = cells[i + VGA_COLS];
    }
    clear_row(VGA_ROWS - 1);
    row = VGA_ROWS - 1;
}

static void newline(void) {
    col = 0;
    if (++row == VGA_ROWS) {
        scroll();
    }
}

/** “选择图形表现”（ESC [ ... m）的一个参数：ANSI 的颜色编号换成 VGA 的 */
static void set_graphics(uint32_t param) {
    // ANSI 的顺序是 黑 红 绿 黄 蓝 品红 青 白，VGA 的蓝和红是反过来的
    static const uint8_t ansi_to_vga[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
    if (param == 0) {
        attr = ATTR_DEFAULT;
    } else if (param == 1) {
        attr |= ATTR_BRIGHT;
    } else if (param >= 30 && param <= 37) {
        attr = (uint8_t)((attr & 0xF8) | ansi_to_vga[param - 30]);
    } else if (param >= 90 && param <= 97) {
        attr = (uint8_t)((attr & 0xF0) | ATTR_BRIGHT | ansi_to_vga[param - 90]);
    }
}

/** 正在一个转义序列里：吃掉 c，返回 true；否则返回 false */
static bool in_escape(char c) {
    switch (escape) {
    case Escape::NONE:
        if (c != '\033') {
            return false;
        }
        escape = Escape::ESC;
        return true;
    case Escape::ESC:
        if (c == '[') {
            escape = Escape::CSI;
            csi_count = 0;
            csi_params[0] = 0;
        } else {
            escape = Escape::NONE;      // 别的序列只有一个字符，丢掉
        }
        return true;
    case Escape::CSI:
        if (c >= '0' && c <= '9') {
            if (csi_count < CSI_MAX_PARAMS) {
                csi_params[csi_count] = csi_params[csi_count] * 10 + (uint32_t)(c - '0');
            }
        } else if (c == ';') {
            if (++csi_count < CSI_MAX_PARAMS) {
                csi_params[csi_count] = 0;
            }
        } else if (c >= 0x40 && c <= 0x7E) {    // 结束序列的字母
            if (c == 'm') {
                for (uint32_t i = 0; i <= csi_count && i < CSI_MAX_PARAMS; i++) {
                    set_graphics(csi_params[i]);
                }
            }
            escape = Escape::NONE;
        }
        return true;
    }
    return false;
}

namespace drivers {

void Screen::init() {
    for (uint32_t r = 0; r < VGA_ROWS; r++) {
        clear_row(r);
    }
    row = col = 0;
    // 固件可能把光标关了：打开，画成字符格底部的一条线
    outb(CRTC_INDEX, CRTC_CURSOR_START);
    outb(CRTC_DATA, 13);
    outb(CRTC_INDEX, CRTC_CURSOR_END);
    outb(CRTC_DATA, 14);
    move_cursor();
}

void Screen::putchar(char c) {
    if (in_escape(c)) {
        return;
    }
    switch (c) {
    case '\n':
        newline();
        break;
    case '\r':
        col = 0;
        break;
    case '\b':
        if (col > 0) {
            col--;
        }
        break;
    case '\t':
        do {
            Screen::putchar(' ');
        } while (col % 8 != 0);
        return;
    default:
        cells[row * VGA_COLS + col] = (uint16_t)((uint8_t)c | (attr << 8));
        if (++col == VGA_COLS) {
            newline();
        }
        break;
    }
    move_cursor();
}

} // namespace drivers
