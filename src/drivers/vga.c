#include <drivers/vga.h>
#include <types.h>
#include <kernel/io.h>

/* VGA 文本模式参数 */
#define VGA_ADDRESS 0x800B8000  // 物理地址 0xB8000 + 内核基址 0x80000000
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

/* VGA 光标控制端口 */
#define VGA_CTRL_REGISTER 0x3D4
#define VGA_DATA_REGISTER 0x3D5

/* 当前光标位置 */
static int vga_row = 0;
static int vga_col = 0;

/* 当前颜色属性 */
static uint8_t vga_color = 0x0F;  // 默认白色文字，黑色背景

/* ANSI 转义序列解析状态 */
typedef enum {
    ANSI_NORMAL,      // 正常模式
    ANSI_ESCAPE,      // 收到 ESC (0x1B)
    ANSI_BRACKET,     // 收到 '['
    ANSI_PARAM        // 解析参数
} ansi_state_t;

static ansi_state_t ansi_state = ANSI_NORMAL;
static int ansi_param = 0;  // 当前参数值

/**
 * 创建 VGA 颜色属性字节
 */
static inline uint8_t vga_make_color(vga_color_t fg, vga_color_t bg) {
    return fg | (bg << 4);
}

/**
 * 创建 VGA 条目（字符 + 属性）
 */
static inline uint16_t vga_make_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

/**
 * 更新硬件光标位置
 */
static void vga_update_cursor(void) {
    uint16_t position = vga_row * VGA_WIDTH + vga_col;
    
    // 发送光标位置的低字节
    outb(VGA_CTRL_REGISTER, 0x0F);
    outb(VGA_DATA_REGISTER, (uint8_t)(position & 0xFF));
    
    // 发送光标位置的高字节
    outb(VGA_CTRL_REGISTER, 0x0E);
    outb(VGA_DATA_REGISTER, (uint8_t)((position >> 8) & 0xFF));
}

/**
 * 滚动屏幕
 */
static void vga_scroll(void) {
    volatile uint16_t *vga = (volatile uint16_t *)VGA_ADDRESS;
    
    // 将第 1-24 行移动到第 0-23 行
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
        vga[i] = vga[i + VGA_WIDTH];
    }
    
    // 清空最后一行
    uint16_t blank = vga_make_entry(' ', vga_color);
    for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) {
        vga[i] = blank;
    }
}

/**
 * 换行处理
 */
static void vga_newline(void) {
    vga_col = 0;
    vga_row++;
    if (vga_row >= VGA_HEIGHT) {
        vga_scroll();
        vga_row = VGA_HEIGHT - 1;
    }
}

/**
 * 在指定位置写入字符
 */
static void vga_putentry_at(char c, uint8_t color, int x, int y) {
    volatile uint16_t *vga = (volatile uint16_t *)VGA_ADDRESS;
    const int index = y * VGA_WIDTH + x;
    vga[index] = vga_make_entry(c, color);
}

void vga_init(void) {
    vga_clear();
}

void vga_clear(void) {
    volatile uint16_t *vga = (volatile uint16_t *)VGA_ADDRESS;
    uint16_t blank = vga_make_entry(' ', vga_color);
    
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = blank;
    }
    
    vga_row = 0;
    vga_col = 0;
    vga_update_cursor();
}

void vga_putchar(char c) {
    // ANSI 转义序列解析
    if (ansi_state == ANSI_NORMAL) {
        if (c == '\033' || c == 0x1B) {
            // 开始 ANSI 转义序列
            ansi_state = ANSI_ESCAPE;
            return;
        }
    } else if (ansi_state == ANSI_ESCAPE) {
        if (c == '[') {
            ansi_state = ANSI_BRACKET;
            ansi_param = 0;
            return;
        } else {
            // 无效的转义序列，重置状态
            ansi_state = ANSI_NORMAL;
        }
    } else if (ansi_state == ANSI_BRACKET || ansi_state == ANSI_PARAM) {
        if (c >= '0' && c <= '9') {
            // 解析数字参数
            ansi_param = ansi_param * 10 + (c - '0');
            ansi_state = ANSI_PARAM;
            return;
        } else if (c == 'J') {
            // 清屏命令：\033[2J 或 \033[J
            if (ansi_param == 2 || ansi_param == 0) {
                vga_clear();
            }
            ansi_state = ANSI_NORMAL;
            ansi_param = 0;
            return;
        } else if (c == 'H') {
            // 光标定位：\033[H 或 \033[n;mH
            // 简化处理：只处理 \033[H (移到左上角)
            if (ansi_param == 0) {
                vga_row = 0;
                vga_col = 0;
                vga_update_cursor();
            }
            ansi_state = ANSI_NORMAL;
            ansi_param = 0;
            return;
        } else if (c == 'A') {
            // 光标上移：\033[nA
            int n = (ansi_param == 0) ? 1 : ansi_param;
            vga_row = (vga_row >= n) ? (vga_row - n) : 0;
            vga_update_cursor();
            ansi_state = ANSI_NORMAL;
            ansi_param = 0;
            return;
        } else if (c == 'B') {
            // 光标下移：\033[nB
            int n = (ansi_param == 0) ? 1 : ansi_param;
            vga_row = (vga_row + n < VGA_HEIGHT) ? (vga_row + n) : (VGA_HEIGHT - 1);
            vga_update_cursor();
            ansi_state = ANSI_NORMAL;
            ansi_param = 0;
            return;
        } else if (c == 'C') {
            // 光标右移：\033[nC
            int n = (ansi_param == 0) ? 1 : ansi_param;
            vga_col = (vga_col + n < VGA_WIDTH) ? (vga_col + n) : (VGA_WIDTH - 1);
            vga_update_cursor();
            ansi_state = ANSI_NORMAL;
            ansi_param = 0;
            return;
        } else if (c == 'D') {
            // 光标左移：\033[nD
            int n = (ansi_param == 0) ? 1 : ansi_param;
            vga_col = (vga_col >= n) ? (vga_col - n) : 0;
            vga_update_cursor();
            ansi_state = ANSI_NORMAL;
            ansi_param = 0;
            return;
        } else if (c == ';') {
            // 参数分隔符，忽略（简化处理）
            ansi_param = 0;
            return;
        } else {
            // 未知命令，重置状态
            ansi_state = ANSI_NORMAL;
            ansi_param = 0;
        }
    }
    
    // 正常字符处理
    if (c == '\n') {
        // 换行符：移到下一行行首
        vga_newline();
    } else if (c == '\r') {
        // 回车符：光标移到行首
        vga_col = 0;
    } else if (c == '\t') {
        // 制表符：移到下一个 4 字符对齐位置
        vga_col = (vga_col + 4) & ~3;
        if (vga_col >= VGA_WIDTH) {
            vga_newline();
        }
    } else if (c == '\b') {
        // 退格符：光标向左移动一格（不删除字符）
        if (vga_col > 0) {
            vga_col--;
        }
    } else {
        // 如果当前行已满，先换行再输出字符
        if (vga_col >= VGA_WIDTH) {
            vga_newline();
        }
        
        vga_putentry_at(c, vga_color, vga_col, vga_row);
        vga_col++;
    }
    
    vga_update_cursor();
}

void vga_print(const char *msg) {
    while (*msg) {
        vga_putchar(*msg++);
    }
}

void vga_set_color(vga_color_t fg, vga_color_t bg) {
    vga_color = vga_make_color(fg, bg);
}

uint8_t vga_get_color(void) {
    return vga_color;
}
