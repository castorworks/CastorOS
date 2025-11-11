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
    if (c == '\n') {
        vga_newline();
    } else {
        vga_putentry_at(c, vga_color, vga_col, vga_row);
        vga_col++;
        
        // 到达行末自动换行
        if (vga_col >= VGA_WIDTH) {
            vga_newline();
        }
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
