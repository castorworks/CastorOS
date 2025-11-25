#include <drivers/vga.h>
#include <types.h>
#include <kernel/io.h>
#include <kernel/sync/spinlock.h>

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
static spinlock_t vga_lock;

/* ANSI 转义序列解析状态 */
typedef enum {
    ANSI_NORMAL,      // 正常模式
    ANSI_ESCAPE,      // 收到 ESC (0x1B)
    ANSI_BRACKET,     // 收到 '['
    ANSI_PARAM        // 解析参数
} ansi_state_t;

static ansi_state_t ansi_state = ANSI_NORMAL;
#define ANSI_MAX_PARAMS 8
static int ansi_params[ANSI_MAX_PARAMS];  // 参数数组
static int ansi_param_count = 0;          // 当前参数数量

/* 保存的默认颜色 */
static uint8_t default_vga_color = 0x0F;
static bool color_bold = false;  // 粗体/高亮标志

/* ANSI 颜色到 VGA 颜色的映射表 */
static const vga_color_t ansi_to_vga_fg[8] = {
    VGA_COLOR_BLACK,       // 30: 黑色
    VGA_COLOR_RED,         // 31: 红色
    VGA_COLOR_GREEN,       // 32: 绿色
    VGA_COLOR_BROWN,       // 33: 黄色（普通）
    VGA_COLOR_BLUE,        // 34: 蓝色
    VGA_COLOR_MAGENTA,     // 35: 紫色
    VGA_COLOR_CYAN,        // 36: 青色
    VGA_COLOR_LIGHT_GREY,  // 37: 白色
};

/* 亮色版本 */
static const vga_color_t ansi_to_vga_bright_fg[8] = {
    VGA_COLOR_DARK_GREY,      // 90: 亮黑（灰）
    VGA_COLOR_LIGHT_RED,      // 91: 亮红
    VGA_COLOR_LIGHT_GREEN,    // 92: 亮绿
    VGA_COLOR_YELLOW,         // 93: 亮黄
    VGA_COLOR_LIGHT_BLUE,     // 94: 亮蓝
    VGA_COLOR_LIGHT_MAGENTA,  // 95: 亮紫
    VGA_COLOR_LIGHT_CYAN,     // 96: 亮青
    VGA_COLOR_WHITE,          // 97: 亮白
};

static const vga_color_t ansi_to_vga_bg[8] = {
    VGA_COLOR_BLACK,       // 40: 黑色背景
    VGA_COLOR_RED,         // 41: 红色背景
    VGA_COLOR_GREEN,       // 42: 绿色背景
    VGA_COLOR_BROWN,       // 43: 黄色背景
    VGA_COLOR_BLUE,        // 44: 蓝色背景
    VGA_COLOR_MAGENTA,     // 45: 紫色背景
    VGA_COLOR_CYAN,        // 46: 青色背景
    VGA_COLOR_LIGHT_GREY,  // 47: 白色背景
};

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

/**
 * 处理 ANSI SGR (Select Graphic Rendition) 命令
 * 设置文本颜色和属性
 */
static void vga_handle_sgr(void) {
    // 如果没有参数，默认为 0（重置）
    if (ansi_param_count == 0) {
        ansi_params[0] = 0;
        ansi_param_count = 1;
    }
    
    for (int i = 0; i < ansi_param_count; i++) {
        int code = ansi_params[i];
        uint8_t fg = vga_color & 0x0F;
        uint8_t bg = (vga_color >> 4) & 0x0F;
        
        if (code == 0) {
            // 重置所有属性
            vga_color = default_vga_color;
            color_bold = false;
        } else if (code == 1) {
            // 粗体/高亮
            color_bold = true;
            // 如果当前前景色是普通色，转换为亮色
            if (fg < 8) {
                fg += 8;  // 转换为亮色
                vga_color = vga_make_color(fg, bg);
            }
        } else if (code == 22) {
            // 取消粗体
            color_bold = false;
            if (fg >= 8 && fg < 16) {
                fg -= 8;  // 转换回普通色
                vga_color = vga_make_color(fg, bg);
            }
        } else if (code >= 30 && code <= 37) {
            // 普通前景色
            fg = ansi_to_vga_fg[code - 30];
            if (color_bold && fg < 8) {
                fg += 8;  // 高亮模式下使用亮色
            }
            vga_color = vga_make_color(fg, bg);
        } else if (code == 39) {
            // 默认前景色
            vga_color = vga_make_color(VGA_COLOR_LIGHT_GREY, bg);
        } else if (code >= 40 && code <= 47) {
            // 普通背景色
            bg = ansi_to_vga_bg[code - 40];
            vga_color = vga_make_color(fg, bg);
        } else if (code == 49) {
            // 默认背景色
            vga_color = vga_make_color(fg, VGA_COLOR_BLACK);
        } else if (code >= 90 && code <= 97) {
            // 亮前景色
            fg = ansi_to_vga_bright_fg[code - 90];
            vga_color = vga_make_color(fg, bg);
        } else if (code >= 100 && code <= 107) {
            // 亮背景色
            bg = ansi_to_vga_bright_fg[code - 100];
            vga_color = vga_make_color(fg, bg);
        }
    }
}

static void vga_clear_locked(void) {
    volatile uint16_t *vga = (volatile uint16_t *)VGA_ADDRESS;
    uint16_t blank = vga_make_entry(' ', vga_color);
    
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = blank;
    }
    
    vga_row = 0;
    vga_col = 0;
    vga_update_cursor();
    ansi_state = ANSI_NORMAL;
    ansi_param_count = 0;
}

void vga_init(void) {
    spinlock_init(&vga_lock);
    bool irq_state;
    spinlock_lock_irqsave(&vga_lock, &irq_state);
    vga_clear_locked();
    spinlock_unlock_irqrestore(&vga_lock, irq_state);
}

void vga_clear(void) {
    bool irq_state;
    spinlock_lock_irqsave(&vga_lock, &irq_state);
    vga_clear_locked();
    spinlock_unlock_irqrestore(&vga_lock, irq_state);
}

static void vga_handle_char(char c) {
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
            ansi_param_count = 0;
            return;
        } else {
            // 无效的转义序列，重置状态
            ansi_state = ANSI_NORMAL;
        }
    } else if (ansi_state == ANSI_BRACKET || ansi_state == ANSI_PARAM) {
        if (c >= '0' && c <= '9') {
            // 解析数字参数
            if (ansi_param_count == 0) {
                ansi_param_count = 1;
                ansi_params[0] = 0;
            }
            ansi_params[ansi_param_count - 1] = ansi_params[ansi_param_count - 1] * 10 + (c - '0');
            ansi_state = ANSI_PARAM;
            return;
        } else if (c == ';') {
            // 参数分隔符，开始新参数
            if (ansi_param_count < ANSI_MAX_PARAMS) {
                if (ansi_param_count == 0) {
                    ansi_param_count = 1;
                    ansi_params[0] = 0;
                }
                ansi_param_count++;
                ansi_params[ansi_param_count - 1] = 0;
            }
            return;
        } else if (c == 'm') {
            // SGR (Select Graphic Rendition) - 设置颜色和属性
            vga_handle_sgr();
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
            return;
        } else if (c == 'J') {
            // 清屏命令：\033[2J 或 \033[J
            int param = (ansi_param_count > 0) ? ansi_params[0] : 0;
            if (param == 2 || param == 0) {
                vga_clear_locked();
            }
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
            return;
        } else if (c == 'H') {
            // 光标定位：\033[H 或 \033[n;mH
            // 简化处理：只处理 \033[H (移到左上角)
            int param = (ansi_param_count > 0) ? ansi_params[0] : 0;
            if (param == 0) {
                vga_row = 0;
                vga_col = 0;
                vga_update_cursor();
            }
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
            return;
        } else if (c == 'A') {
            // 光标上移：\033[nA
            int param = (ansi_param_count > 0) ? ansi_params[0] : 0;
            int n = (param == 0) ? 1 : param;
            vga_row = (vga_row >= n) ? (vga_row - n) : 0;
            vga_update_cursor();
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
            return;
        } else if (c == 'B') {
            // 光标下移：\033[nB
            int param = (ansi_param_count > 0) ? ansi_params[0] : 0;
            int n = (param == 0) ? 1 : param;
            vga_row = (vga_row + n < VGA_HEIGHT) ? (vga_row + n) : (VGA_HEIGHT - 1);
            vga_update_cursor();
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
            return;
        } else if (c == 'C') {
            // 光标右移：\033[nC
            int param = (ansi_param_count > 0) ? ansi_params[0] : 0;
            int n = (param == 0) ? 1 : param;
            vga_col = (vga_col + n < VGA_WIDTH) ? (vga_col + n) : (VGA_WIDTH - 1);
            vga_update_cursor();
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
            return;
        } else if (c == 'D') {
            // 光标左移：\033[nD
            int param = (ansi_param_count > 0) ? ansi_params[0] : 0;
            int n = (param == 0) ? 1 : param;
            vga_col = (vga_col >= n) ? (vga_col - n) : 0;
            vga_update_cursor();
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
            return;
        } else {
            // 未知命令，重置状态
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
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

void vga_putchar(char c) {
    bool irq_state;
    spinlock_lock_irqsave(&vga_lock, &irq_state);
    vga_handle_char(c);
    spinlock_unlock_irqrestore(&vga_lock, irq_state);
}

void vga_print(const char *msg) {
    if (msg == NULL) {
        return;
    }

    bool irq_state;
    spinlock_lock_irqsave(&vga_lock, &irq_state);
    while (*msg) {
        vga_handle_char(*msg++);
    }
    spinlock_unlock_irqrestore(&vga_lock, irq_state);
}

void vga_set_color(vga_color_t fg, vga_color_t bg) {
    bool irq_state;
    spinlock_lock_irqsave(&vga_lock, &irq_state);
    vga_color = vga_make_color(fg, bg);
    spinlock_unlock_irqrestore(&vga_lock, irq_state);
}

uint8_t vga_get_color(void) {
    bool irq_state;
    spinlock_lock_irqsave(&vga_lock, &irq_state);
    uint8_t color = vga_color;
    spinlock_unlock_irqrestore(&vga_lock, irq_state);
    return color;
}
