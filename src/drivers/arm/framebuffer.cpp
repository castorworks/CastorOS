/**
 * @file framebuffer.c
 * @brief ARM64 Framebuffer driver using virtio-gpu
 * 
 * Provides a framebuffer console for ARM64 using the virtio-gpu device.
 * Compatible with the x86 framebuffer API for kprintf integration.
 */

#include <drivers/arm/framebuffer.h>
#include <drivers/framebuffer.h>
#include <drivers/serial.h>
#include <drivers/arm/virtio_gpu.h>
#include <drivers/arm/font8x16.h>
#include <lib/string.h>

/* Forward declarations */
extern "C" void serial_puts(const char *str);

/* ============================================================================
 * Global State
 * ========================================================================== */

static bool fb_initialized = false;
static framebuffer_info_t fb_info;

/* Font settings */
static const uint8_t *current_font = NULL;
static int font_width = 8;
static int font_height = 16;

/* Terminal state */
static int term_cursor_col = 0;
static int term_cursor_row = 0;
static color_t term_fg = {170, 170, 170, 255};  /* Light grey */
static color_t term_bg = {0, 0, 0, 255};        /* Black */

/* VGA 16-color palette */
static const color_t vga_palette[16] = {
    {0, 0, 0, 255},         /* 0: BLACK */
    {0, 0, 170, 255},       /* 1: BLUE */
    {0, 170, 0, 255},       /* 2: GREEN */
    {0, 170, 170, 255},     /* 3: CYAN */
    {170, 0, 0, 255},       /* 4: RED */
    {170, 0, 170, 255},     /* 5: MAGENTA */
    {170, 85, 0, 255},      /* 6: BROWN */
    {170, 170, 170, 255},   /* 7: LIGHT_GREY */
    {85, 85, 85, 255},      /* 8: DARK_GREY */
    {85, 85, 255, 255},     /* 9: LIGHT_BLUE */
    {85, 255, 85, 255},     /* 10: LIGHT_GREEN */
    {85, 255, 255, 255},    /* 11: LIGHT_CYAN */
    {255, 85, 85, 255},     /* 12: LIGHT_RED */
    {255, 85, 255, 255},    /* 13: LIGHT_MAGENTA */
    {255, 255, 85, 255},    /* 14: YELLOW */
    {255, 255, 255, 255},   /* 15: WHITE */
};

/* ANSI escape sequence parsing */
typedef enum {
    ANSI_NORMAL,
    ANSI_ESCAPE,
    ANSI_BRACKET,
    ANSI_PARAM
} ansi_state_t;

static ansi_state_t ansi_state = ANSI_NORMAL;
#define ANSI_MAX_PARAMS 8
#define ANSI_PARAM_MAX 9999     // 单个参数的上限（饱和）
static int ansi_params[ANSI_MAX_PARAMS];
static int ansi_param_count = 0;
static bool ansi_bold = false;

/* ANSI to VGA color mapping */
static const uint8_t ansi_to_vga_fg[8] = {0, 4, 2, 6, 1, 5, 3, 7};
static const uint8_t ansi_to_vga_bright[8] = {8, 12, 10, 14, 9, 13, 11, 15};

/* Dirty region tracking for efficient flushing */
static int dirty_y_start = -1;
static int dirty_y_end = -1;

/* ============================================================================
 * Internal Helpers
 * ========================================================================== */

static inline uint32_t color_to_pixel(color_t c) {
    /* 
     * B8G8R8X8_UNORM format for virtio-gpu:
     * Memory layout (low to high): B, G, R, X
     * As 32-bit little-endian: 0xXXRRGGBB
     */
    return (0xFF << 24) | ((uint32_t)c.r << 16) | 
           ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

static inline void mark_dirty(int y_start, int y_end) {
    if (dirty_y_start < 0 || y_start < dirty_y_start) {
        dirty_y_start = y_start;
    }
    if (dirty_y_end < 0 || y_end > dirty_y_end) {
        dirty_y_end = y_end;
    }
}

static void fb_put_pixel_fast(int x, int y, uint32_t pixel) {
    if (x < 0 || x >= (int)fb_info.width || y < 0 || y >= (int)fb_info.height) {
        return;
    }
    fb_info.buffer[y * fb_info.width + x] = pixel;
}

int drivers::Framebuffer::get_cols() {
    return fb_info.width / font_width;
}

int drivers::Framebuffer::get_rows() {
    return fb_info.height / font_height;
}

/* ============================================================================
 * Drawing Functions
 * ========================================================================== */

void drivers::Framebuffer::draw_char(int x, int y, char c, color_t fg, color_t bg) {
    if (!fb_initialized || !current_font) return;
    
    const uint8_t *glyph = current_font + (unsigned char)c * font_height;
    uint32_t fg_pixel = color_to_pixel(fg);
    uint32_t bg_pixel = color_to_pixel(bg);
    
    for (int row = 0; row < font_height; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < font_width; col++) {
            uint32_t pixel = (bits & (0x80 >> col)) ? fg_pixel : bg_pixel;
            fb_put_pixel_fast(x + col, y + row, pixel);
        }
    }
    
    mark_dirty(y, y + font_height);
}

void drivers::Framebuffer::fill_rect(int x, int y, int width, int height, color_t color) {
    if (!fb_initialized) return;
    
    /* Clamp to screen bounds */
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > (int)fb_info.width) width = fb_info.width - x;
    if (y + height > (int)fb_info.height) height = fb_info.height - y;
    if (width <= 0 || height <= 0) return;
    
    uint32_t pixel = color_to_pixel(color);
    
    for (int row = 0; row < height; row++) {
        uint32_t *line = fb_info.buffer + (y + row) * fb_info.width + x;
        for (int col = 0; col < width; col++) {
            line[col] = pixel;
        }
    }
    
    mark_dirty(y, y + height);
}

static void fb_scroll(int lines) {
    if (!fb_initialized || lines <= 0) return;
    
    int scroll_pixels = lines * font_height;
    int remaining = fb_info.height - scroll_pixels;
    
    if (remaining > 0) {
        /* Move screen content up */
        memmove(fb_info.buffer, 
                fb_info.buffer + scroll_pixels * fb_info.width,
                remaining * fb_info.width * sizeof(uint32_t));
        
        /* Clear bottom area */
        uint32_t bg_pixel = color_to_pixel(term_bg);
        uint32_t *bottom = fb_info.buffer + remaining * fb_info.width;
        uint32_t count = scroll_pixels * fb_info.width;
        for (uint32_t i = 0; i < count; i++) {
            bottom[i] = bg_pixel;
        }
        
        mark_dirty(0, fb_info.height);
    } else {
        drivers::Framebuffer::clear(term_bg);
    }
}

/* ============================================================================
 * ANSI Escape Sequence Handling
 * ========================================================================== */

static void fb_handle_sgr(void) {
    if (ansi_param_count == 0) {
        ansi_params[0] = 0;
        ansi_param_count = 1;
    }
    
    for (int i = 0; i < ansi_param_count; i++) {
        int code = ansi_params[i];
        
        if (code == 0) {
            /* Reset */
            term_fg = vga_palette[7];
            term_bg = vga_palette[0];
            ansi_bold = false;
        } else if (code == 1) {
            /* Bold */
            ansi_bold = true;
        } else if (code == 22) {
            /* Normal intensity */
            ansi_bold = false;
        } else if (code >= 30 && code <= 37) {
            /* Foreground color */
            uint8_t idx = ansi_to_vga_fg[code - 30];
            if (ansi_bold && idx < 8) idx += 8;
            term_fg = vga_palette[idx];
        } else if (code == 39) {
            /* Default foreground */
            term_fg = vga_palette[7];
        } else if (code >= 40 && code <= 47) {
            /* Background color */
            term_bg = vga_palette[ansi_to_vga_fg[code - 40]];
        } else if (code == 49) {
            /* Default background */
            term_bg = vga_palette[0];
        } else if (code >= 90 && code <= 97) {
            /* Bright foreground */
            term_fg = vga_palette[ansi_to_vga_bright[code - 90]];
        } else if (code >= 100 && code <= 107) {
            /* Bright background */
            term_bg = vga_palette[ansi_to_vga_bright[code - 100]];
        }
    }
}

/* ============================================================================
 * Public API
 * ========================================================================== */

bool drivers::Framebuffer::is_initialized() {
    return fb_initialized;
}

framebuffer_info_t *drivers::Framebuffer::get_info() {
    return fb_initialized ? &fb_info : NULL;
}

void drivers::Framebuffer::clear(color_t color) {
    if (!fb_initialized) return;
    
    uint32_t pixel = color_to_pixel(color);
    uint32_t count = fb_info.width * fb_info.height;
    
    for (uint32_t i = 0; i < count; i++) {
        fb_info.buffer[i] = pixel;
    }
    
    mark_dirty(0, fb_info.height);
    drivers::Framebuffer::flush();
}

void drivers::Framebuffer::terminal_init() {
    /* Initialize virtio-gpu */
    if (drivers::VirtioGpu::init() < 0) {
        serial_puts("fb: virtio-gpu init failed\n");
        return;
    }
    
    /* Setup framebuffer info */
    fb_info.width = drivers::VirtioGpu::get_width();
    fb_info.height = drivers::VirtioGpu::get_height();
    fb_info.buffer = drivers::VirtioGpu::get_framebuffer();
    fb_info.pitch = fb_info.width * 4;
    fb_info.bpp = 32;
    fb_info.format = FB_FORMAT_BGRA8888;
    fb_info.address = (uint32_t)(uintptr_t)fb_info.buffer;
    
    /* Set font */
    current_font = font8x16_data;
    font_width = 8;
    font_height = 16;
    
    /* Reset terminal state */
    term_cursor_col = 0;
    term_cursor_row = 0;
    term_fg = vga_palette[7];
    term_bg = vga_palette[0];
    ansi_state = ANSI_NORMAL;
    ansi_param_count = 0;
    ansi_bold = false;
    
    fb_initialized = true;
    
    /* Clear screen to black */
    drivers::Framebuffer::clear(term_bg);
    
    serial_puts("fb: Terminal initialized (");
    drivers::Serial::put_hex32(fb_info.width);
    serial_puts("x");
    drivers::Serial::put_hex32(fb_info.height);
    serial_puts(", ");
    drivers::Serial::put_hex32(drivers::Framebuffer::get_cols());
    serial_puts("x");
    drivers::Serial::put_hex32(drivers::Framebuffer::get_rows());
    serial_puts(" chars)\n");
}

void drivers::Framebuffer::terminal_clear() {
    if (!fb_initialized) return;
    
    drivers::Framebuffer::clear(term_bg);
    term_cursor_col = 0;
    term_cursor_row = 0;
    ansi_state = ANSI_NORMAL;
    ansi_param_count = 0;
}

void drivers::Framebuffer::terminal_putchar(char c) {
    if (!fb_initialized) return;
    
    int max_cols = drivers::Framebuffer::get_cols();
    int max_rows = drivers::Framebuffer::get_rows();
    
    /* ANSI escape sequence parsing */
    if (ansi_state == ANSI_NORMAL) {
        if (c == '\033' || c == 0x1B) {
            ansi_state = ANSI_ESCAPE;
            return;
        }
    } else if (ansi_state == ANSI_ESCAPE) {
        if (c == '[') {
            ansi_state = ANSI_BRACKET;
            ansi_param_count = 0;
            return;
        } else {
            ansi_state = ANSI_NORMAL;
        }
    } else if (ansi_state == ANSI_BRACKET || ansi_state == ANSI_PARAM) {
        if (c >= '0' && c <= '9') {
            if (ansi_param_count == 0) {
                ansi_param_count = 1;
                ansi_params[0] = 0;
            }
            // 参数饱和在 ANSI_PARAM_MAX：任意长的数字串不能让 int 回绕成负数，
            // 否则后面的光标移动会算出屏幕之外的位置
            if (ansi_params[ansi_param_count - 1] < ANSI_PARAM_MAX) {
                ansi_params[ansi_param_count - 1] = 
                    ansi_params[ansi_param_count - 1] * 10 + (c - '0');
            }
            ansi_state = ANSI_PARAM;
            return;
        } else if (c == ';') {
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
            fb_handle_sgr();
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
            return;
        } else if (c == 'J') {
            int param = (ansi_param_count > 0) ? ansi_params[0] : 0;
            if (param == 2 || param == 0) {
                drivers::Framebuffer::terminal_clear();
            }
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
            return;
        } else if (c == 'H') {
            int row = (ansi_param_count > 0 && ansi_params[0] > 0) ? ansi_params[0] - 1 : 0;
            int col = (ansi_param_count > 1 && ansi_params[1] > 0) ? ansi_params[1] - 1 : 0;
            term_cursor_row = (row < max_rows) ? row : max_rows - 1;
            term_cursor_col = (col < max_cols) ? col : max_cols - 1;
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
            return;
        } else {
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
        }
    }
    
    /* Normal character handling */
    switch (c) {
        case '\n':
            term_cursor_col = 0;
            term_cursor_row++;
            break;
            
        case '\r':
            term_cursor_col = 0;
            break;
            
        case '\t':
            term_cursor_col = (term_cursor_col + 4) & ~3;
            break;
            
        case '\b':
            if (term_cursor_col > 0) {
                term_cursor_col--;
                drivers::Framebuffer::fill_rect(term_cursor_col * font_width,
                            term_cursor_row * font_height,
                            font_width, font_height, term_bg);
            }
            break;
            
        default:
            drivers::Framebuffer::draw_char(term_cursor_col * font_width,
                        term_cursor_row * font_height,
                        c, term_fg, term_bg);
            term_cursor_col++;
            break;
    }
    
    /* Handle line wrap */
    if (term_cursor_col >= max_cols) {
        term_cursor_col = 0;
        term_cursor_row++;
    }
    
    /* Handle scroll */
    if (term_cursor_row >= max_rows) {
        fb_scroll(1);
        term_cursor_row = max_rows - 1;
    }
}

void drivers::Framebuffer::terminal_write(const char *str) {
    if (!str) return;
    
    while (*str) {
        drivers::Framebuffer::terminal_putchar(*str++);
    }
    
    drivers::Framebuffer::flush();
}

void drivers::Framebuffer::terminal_set_vga_color(uint8_t fg, uint8_t bg) {
    if (fg > 15) fg = 15;
    if (bg > 15) bg = 15;
    term_fg = vga_palette[fg];
    term_bg = vga_palette[bg];
}

void drivers::Framebuffer::flush() {
    if (!fb_initialized) return;
    
    if (dirty_y_start >= 0 && dirty_y_end > dirty_y_start) {
        /* Clamp to screen bounds */
        if (dirty_y_start < 0) dirty_y_start = 0;
        if (dirty_y_end > (int)fb_info.height) dirty_y_end = fb_info.height;
        
        drivers::VirtioGpu::flush(0, dirty_y_start, fb_info.width, dirty_y_end - dirty_y_start);
        
        dirty_y_start = -1;
        dirty_y_end = -1;
    }
}
