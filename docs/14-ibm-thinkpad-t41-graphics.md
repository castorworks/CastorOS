# 阶段 14: IBM ThinkPad T41 显卡驱动

## 概述

本阶段将为 IBM ThinkPad T41 笔记本电脑实现图形显示支持。T41 系列配备 ATI Mobility Radeon 7500/9000 显卡，支持两种屏幕配置：

- **XGA 型号**：1024×768 分辨率
- **SXGA+ 型号**：1400×1050 分辨率

由于当前内核仅支持 VGA 文本模式（80×25），本阶段将实现从文本模式到图形模式的完整升级，并**自动适配不同分辨率的屏幕**。

**📝 设计理念**：

本阶段采用**渐进式图形支持**架构：

✅ **PCI 总线驱动**
   - 实现 PCI 配置空间访问
   - 设备枚举与资源分配
   - 为显卡和其他 PCI 设备提供统一访问接口

✅ **VESA VBE 帧缓冲**
   - 通过 VESA BIOS Extensions 设置图形模式
   - 线性帧缓冲（Linear Framebuffer）支持
   - 硬件无关的基础图形模式

✅ **ATI Radeon 基础驱动**（可选）
   - 直接访问显卡寄存器
   - 模式设置（Modesetting）
   - 2D 加速操作

✅ **帧缓冲抽象层**
   - 统一的图形操作接口
   - 像素绘制、矩形填充、位图传输
   - 为未来 GUI 系统奠定基础

---

## 目标

- [ ] 实现 PCI 总线枚举和配置空间访问
- [ ] 实现 VESA VBE 图形模式设置
- [ ] 实现帧缓冲驱动抽象层
- [ ] 实现基础图形操作（像素、线条、矩形、字符渲染）
- [ ] 支持多分辨率：1024×768 和 1400×1050（32bpp）
- [ ] 实现 EDID 读取，自动检测显示器原生分辨率
- [ ] （可选）ATI Radeon 7500/9000 原生驱动

---

## 技术背景

### IBM ThinkPad T41 显卡规格

**ATI Mobility Radeon 7500** 规格：
- GPU 架构：RV200（R200 系列移动版）
- 显存：16MB/32MB DDR SDRAM
- 核心频率：230-290 MHz
- 显存频率：183-230 MHz
- 接口：AGP 4x
- 最大分辨率：2048×1536（外接）/ 1400×1050（内置 LCD）

**ATI Mobility Radeon 9000** 规格（部分 T41 型号）：
- GPU 架构：RV250
- 显存：32MB/64MB DDR
- 核心频率：250 MHz
- DirectX 8.1 支持

**T41 内置显示屏**：
- 尺寸：14.1 英寸
- 分辨率：1024×768（XGA）或 1400×1050（SXGA+）
- 类型：TFT LCD

### PCI 总线基础

**PCI（Peripheral Component Interconnect）** 是 x86 平台的标准外设总线。

**PCI 配置空间**：
- 每个 PCI 设备有 256 字节的配置空间
- 通过 I/O 端口 0xCF8（地址）和 0xCFC（数据）访问
- 包含设备 ID、厂商 ID、BAR（基地址寄存器）等信息

**配置空间地址格式**（端口 0xCF8）：
```
31  30-24   23-16    15-11      10-8       7-2       1-0
+---+-------+--------+----------+----------+---------+---+
| E | Rsvd  |  Bus   | Device   | Function | Register| 0 |
+---+-------+--------+----------+----------+---------+---+
  1    7        8         5          3          6       2

E = Enable Bit (必须为 1)
```

**常用配置空间寄存器**：
```
偏移量  大小  名称
0x00    2    Vendor ID (厂商 ID)
0x02    2    Device ID (设备 ID)
0x04    2    Command (命令寄存器)
0x06    2    Status (状态寄存器)
0x08    1    Revision ID
0x09    3    Class Code (设备类型)
0x0C    1    Cache Line Size
0x0D    1    Latency Timer
0x0E    1    Header Type
0x10-0x24    BAR 0-5 (基地址寄存器)
0x2C    2    Subsystem Vendor ID
0x2E    2    Subsystem ID
0x3C    1    Interrupt Line
0x3D    1    Interrupt Pin
```

**ATI 显卡识别**：
- Vendor ID：0x1002（ATI Technologies）
- Radeon 7500 Device ID：0x4C57（RV200 [Mobility Radeon 7500]）
- Radeon 9000 Device ID：0x4C66（RV250 [Mobility Radeon 9000]）

### VESA VBE（Video BIOS Extensions）

**VBE** 提供了硬件无关的图形模式设置方法。

**VBE 版本**：
- VBE 1.x：基础图形模式
- VBE 2.0：线性帧缓冲支持
- VBE 3.0：刷新率控制

**VBE 调用方式**：
由于 VBE 通过 BIOS 中断（INT 10h）调用，在保护模式下需要：
1. 切换到实模式或 V86 模式
2. 或者在引导阶段（GRUB）预先设置图形模式
3. 或者通过 Multiboot2 的 framebuffer tag 获取帧缓冲信息

**VBE 模式信息结构**：
```c
typedef struct vbe_mode_info {
    uint16_t attributes;         // 模式属性
    uint8_t  window_a;           // 窗口 A 属性
    uint8_t  window_b;           // 窗口 B 属性
    uint16_t granularity;        // 窗口粒度
    uint16_t window_size;        // 窗口大小
    uint16_t segment_a;          // 窗口 A 段地址
    uint16_t segment_b;          // 窗口 B 段地址
    uint32_t win_func_ptr;       // 窗口函数指针
    uint16_t pitch;              // 每行字节数
    uint16_t width;              // 水平分辨率
    uint16_t height;             // 垂直分辨率
    uint8_t  w_char;             // 字符单元宽度
    uint8_t  y_char;             // 字符单元高度
    uint8_t  planes;             // 内存平面数
    uint8_t  bpp;                // 每像素位数
    uint8_t  banks;              // 内存 bank 数
    uint8_t  memory_model;       // 内存模型
    uint8_t  bank_size;          // bank 大小
    uint8_t  image_pages;        // 图像页数
    uint8_t  reserved0;
    // 直接颜色字段
    uint8_t  red_mask;           // 红色掩码大小
    uint8_t  red_position;       // 红色位位置
    uint8_t  green_mask;         // 绿色掩码大小
    uint8_t  green_position;     // 绿色位位置
    uint8_t  blue_mask;          // 蓝色掩码大小
    uint8_t  blue_position;      // 蓝色位位置
    uint8_t  reserved_mask;      // 保留掩码大小
    uint8_t  reserved_position;  // 保留位位置
    uint8_t  direct_color_attributes;
    uint32_t framebuffer;        // 帧缓冲物理地址
    uint32_t off_screen_mem_off; // 离屏内存偏移
    uint16_t off_screen_mem_size;// 离屏内存大小
    // VBE 3.0 字段...
} __attribute__((packed)) vbe_mode_info_t;
```

### 帧缓冲（Framebuffer）概念

**帧缓冲** 是显存中存储屏幕图像的内存区域。

**线性帧缓冲**：
- 屏幕像素按行连续存储
- 像素地址 = 基地址 + y × pitch + x × bytes_per_pixel
- 无需处理内存分页（bank switching）

**像素格式**（以 32bpp 为例）：
```
32bpp ARGB (8-8-8-8):
  31-24: Alpha (或保留)
  23-16: Red
  15-8:  Green
  7-0:   Blue

24bpp BGR:
  23-16: Blue
  15-8:  Green
  7-0:   Red
```

### ATI Radeon 寄存器架构

**内存映射 I/O（MMIO）**：
- BAR0：帧缓冲内存（显存）
- BAR2：MMIO 寄存器

**关键寄存器组**：
```
CRTC 寄存器 (0x0200-0x03FF)：显示时序控制
DAC 寄存器 (0x0400-0x04FF)：调色板和输出控制
OV0 寄存器 (0x0800-0x08FF)：视频叠加
2D 寄存器 (0x1400-0x17FF)：2D 引擎
```

**模式设置流程**：
1. 禁用显示输出
2. 配置 PLL（时钟生成器）
3. 设置 CRTC 时序参数
4. 配置显存映射
5. 启用显示输出

### 多分辨率支持策略

IBM ThinkPad T41 有两种屏幕配置，需要在启动时自动检测并适配：

| 型号 | 分辨率 | 宽高比 | 像素时钟 | 显存需求 |
|------|--------|--------|----------|----------|
| XGA | 1024×768 | 4:3 | 65 MHz | 3 MB (32bpp) |
| SXGA+ | 1400×1050 | 4:3 | 108 MHz | 5.6 MB (32bpp) |

**检测方案对比**：

| 方案 | 优点 | 缺点 | 推荐度 |
|------|------|------|--------|
| GRUB 自动检测 | 最简单，无需内核代码 | 依赖 GRUB 支持 | ⭐⭐⭐⭐⭐ |
| EDID 读取 | 精确检测显示器参数 | 需要 I2C/DDC 驱动 | ⭐⭐⭐⭐ |
| VESA 模式枚举 | 列出所有支持的模式 | 需要实模式调用 | ⭐⭐⭐ |
| 硬编码 + 用户选择 | 简单可靠 | 用户体验差 | ⭐⭐ |

**推荐方案：GRUB 自动检测 + EDID 验证**

1. **引导阶段**：GRUB 尝试设置最高可用分辨率
2. **内核阶段**：通过 EDID 验证并记录显示器信息
3. **运行时**：支持用户手动切换分辨率（未来扩展）

### EDID（Extended Display Identification Data）

**EDID** 是显示器通过 DDC（Display Data Channel）传输的标识数据，包含：
- 制造商信息
- 产品型号
- **首选分辨率（原生分辨率）**
- 支持的显示模式列表
- 物理尺寸

**EDID 读取方式**：
```
显卡 GPIO/I2C 引脚 <--DDC--> LCD 面板 EDID ROM
```

**ATI Radeon 的 DDC 端口**：
- `GPIO_VGA_DDC` (0x0060)：VGA 接口 DDC
- `GPIO_DVI_DDC` (0x0064)：DVI/LVDS 接口 DDC（内置 LCD）

**EDID 数据结构**（标准 128 字节）：
```c
typedef struct edid_block {
    uint8_t  header[8];           // 固定头：00 FF FF FF FF FF FF 00
    uint16_t manufacturer_id;     // 制造商 ID（压缩 ASCII）
    uint16_t product_code;        // 产品代码
    uint32_t serial_number;       // 序列号
    uint8_t  week_of_manufacture; // 生产周
    uint8_t  year_of_manufacture; // 生产年份（相对 1990）
    uint8_t  edid_version;        // EDID 版本
    uint8_t  edid_revision;       // EDID 修订
    uint8_t  video_input;         // 视频输入定义
    uint8_t  max_horiz_size;      // 最大水平尺寸（cm）
    uint8_t  max_vert_size;       // 最大垂直尺寸（cm）
    uint8_t  gamma;               // Gamma 值
    uint8_t  feature_support;     // 特性支持
    // ... 色度坐标、时序信息等
    uint8_t  detailed_timing[4][18]; // 详细时序描述符
    uint8_t  extension_flag;      // 扩展块数量
    uint8_t  checksum;            // 校验和
} __attribute__((packed)) edid_block_t;
```

**从 EDID 提取首选分辨率**（第一个详细时序描述符）：
```c
// 详细时序描述符格式（18 字节）
// 偏移 0-1: 像素时钟（×10 kHz）
// 偏移 2: 水平活动像素低 8 位
// 偏移 4: 水平活动像素高 4 位 + 水平消隐高 4 位
// 偏移 5: 垂直活动行低 8 位
// 偏移 7: 垂直活动行高 4 位 + 垂直消隐高 4 位

uint16_t pixel_clock = (dtd[1] << 8) | dtd[0];  // ×10 kHz
uint16_t h_active = dtd[2] | ((dtd[4] & 0xF0) << 4);
uint16_t v_active = dtd[5] | ((dtd[7] & 0xF0) << 4);
```

---

## 实现设计

### 1. PCI 总线驱动

**头文件**: `src/include/drivers/pci.h`

```c
#ifndef _DRIVERS_PCI_H_
#define _DRIVERS_PCI_H_

#include <types.h>

/* PCI 配置空间端口 */
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

/* PCI 配置空间寄存器偏移 */
#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_REVISION_ID     0x08
#define PCI_CLASS_CODE      0x09
#define PCI_CACHE_LINE      0x0C
#define PCI_LATENCY_TIMER   0x0D
#define PCI_HEADER_TYPE     0x0E
#define PCI_BAR0            0x10
#define PCI_BAR1            0x14
#define PCI_BAR2            0x18
#define PCI_BAR3            0x1C
#define PCI_BAR4            0x20
#define PCI_BAR5            0x24
#define PCI_INTERRUPT_LINE  0x3C
#define PCI_INTERRUPT_PIN   0x3D

/* PCI 设备类别 */
#define PCI_CLASS_DISPLAY       0x03
#define PCI_SUBCLASS_VGA        0x00

/* ATI 厂商 ID */
#define PCI_VENDOR_ATI          0x1002
#define PCI_DEVICE_RADEON_7500  0x4C57  /* RV200 Mobility Radeon 7500 */
#define PCI_DEVICE_RADEON_9000  0x4C66  /* RV250 Mobility Radeon 9000 */

/* PCI 设备信息结构 */
typedef struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    uint32_t bar[6];
} pci_device_t;

/* PCI 函数声明 */
void pci_init(void);

/* 配置空间读写 */
uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value);
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

/* 设备枚举 */
int pci_scan_devices(void);
pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id);
pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass);
void pci_print_devices(void);

/* BAR 解析 */
uint32_t pci_get_bar_address(pci_device_t *dev, int bar_index);
uint32_t pci_get_bar_size(pci_device_t *dev, int bar_index);
bool pci_bar_is_mmio(pci_device_t *dev, int bar_index);

/* 设备控制 */
void pci_enable_bus_master(pci_device_t *dev);
void pci_enable_memory_space(pci_device_t *dev);
void pci_enable_io_space(pci_device_t *dev);

#endif /* _DRIVERS_PCI_H_ */
```

**核心功能**：

1. **配置空间访问**：通过 I/O 端口 0xCF8/0xCFC 读写 PCI 配置空间
2. **设备枚举**：扫描所有总线/设备/功能，发现已连接的设备
3. **BAR 解析**：确定设备的内存/IO 资源地址和大小
4. **设备控制**：启用总线主控、内存空间访问等

### 2. 帧缓冲抽象层

**头文件**: `src/include/drivers/framebuffer.h`

```c
#ifndef _DRIVERS_FRAMEBUFFER_H_
#define _DRIVERS_FRAMEBUFFER_H_

#include <types.h>

/* 像素格式 */
typedef enum {
    FB_FORMAT_RGB565,     // 16bpp: RRRRRGGGGGGBBBBB
    FB_FORMAT_RGB888,     // 24bpp: RRRRRRRRGGGGGGGGBBBBBBBB
    FB_FORMAT_ARGB8888,   // 32bpp: AAAAAAAARRRRRRRRGGGGGGGGBBBBBBBB
    FB_FORMAT_BGRA8888,   // 32bpp: BBBBBBBBGGGGGGGGRRRRRRRRAAAAAAAAA
} fb_format_t;

/* 帧缓冲信息结构 */
typedef struct framebuffer_info {
    uint32_t address;       // 帧缓冲物理地址
    uint32_t *buffer;       // 映射后的虚拟地址
    uint32_t width;         // 水平分辨率
    uint32_t height;        // 垂直分辨率
    uint32_t pitch;         // 每行字节数
    uint8_t  bpp;           // 每像素位数
    fb_format_t format;     // 像素格式
    uint8_t  red_mask_size;
    uint8_t  red_field_pos;
    uint8_t  green_mask_size;
    uint8_t  green_field_pos;
    uint8_t  blue_mask_size;
    uint8_t  blue_field_pos;
} framebuffer_info_t;

/* 颜色结构 */
typedef struct {
    uint8_t r, g, b, a;
} color_t;

/* 预定义颜色 */
#define COLOR_BLACK     (color_t){0, 0, 0, 255}
#define COLOR_WHITE     (color_t){255, 255, 255, 255}
#define COLOR_RED       (color_t){255, 0, 0, 255}
#define COLOR_GREEN     (color_t){0, 255, 0, 255}
#define COLOR_BLUE      (color_t){0, 0, 255, 255}
#define COLOR_YELLOW    (color_t){255, 255, 0, 255}
#define COLOR_CYAN      (color_t){0, 255, 255, 255}
#define COLOR_MAGENTA   (color_t){255, 0, 255, 255}

/* 帧缓冲函数 */
int fb_init(void);
framebuffer_info_t *fb_get_info(void);

/* 基础绘图函数 */
void fb_clear(color_t color);
void fb_put_pixel(int x, int y, color_t color);
color_t fb_get_pixel(int x, int y);
void fb_draw_hline(int x, int y, int length, color_t color);
void fb_draw_vline(int x, int y, int length, color_t color);
void fb_draw_line(int x1, int y1, int x2, int y2, color_t color);
void fb_draw_rect(int x, int y, int width, int height, color_t color);
void fb_fill_rect(int x, int y, int width, int height, color_t color);

/* 位图操作 */
void fb_blit(int x, int y, int width, int height, const uint32_t *data);
void fb_copy_rect(int src_x, int src_y, int dst_x, int dst_y, int width, int height);

/* 文本渲染（基于位图字体） */
void fb_set_font(const uint8_t *font_data, int char_width, int char_height);
void fb_draw_char(int x, int y, char c, color_t fg, color_t bg);
void fb_draw_string(int x, int y, const char *str, color_t fg, color_t bg);

/* 双缓冲支持（可选） */
void fb_swap_buffers(void);
void fb_set_double_buffer(bool enable);

#endif /* _DRIVERS_FRAMEBUFFER_H_ */
```

### 3. VESA VBE 驱动

**设计方案**：

由于 VBE 调用需要 BIOS 中断，有以下实现选择：

**方案 A：Multiboot2 帧缓冲（推荐）**
- 在 GRUB 引导时设置图形模式
- 内核通过 Multiboot2 信息结构获取帧缓冲地址
- 最简单，无需复杂的实模式切换

**方案 B：V86 模式调用 BIOS**
- 实现虚拟 8086 模式
- 在保护模式下调用 VBE BIOS
- 较复杂，但灵活性高

**方案 C：直接模式设置**
- 直接操作显卡寄存器
- 不依赖 BIOS
- 需要显卡特定的代码

**推荐采用方案 A**，修改 Multiboot 头部以请求图形模式：

```nasm
; multiboot2.asm - Multiboot2 头部（支持图形模式）
section .multiboot2
align 8

multiboot2_header:
    dd 0xE85250D6                   ; Multiboot2 魔数
    dd 0                            ; 架构：i386
    dd multiboot2_header_end - multiboot2_header  ; 头部长度
    dd -(0xE85250D6 + 0 + (multiboot2_header_end - multiboot2_header))  ; 校验和

    ; 帧缓冲标签
framebuffer_tag:
    dw 5                            ; 类型：framebuffer
    dw 0                            ; 标志
    dd framebuffer_tag_end - framebuffer_tag
    dd 1024                         ; 首选宽度
    dd 768                          ; 首选高度
    dd 32                           ; 首选位深度
framebuffer_tag_end:
    align 8

    ; 结束标签
    dw 0                            ; 类型：结束
    dw 0                            ; 标志
    dd 8                            ; 大小
multiboot2_header_end:
```

### 4. ATI Radeon 驱动（可选）

**头文件**: `src/include/drivers/radeon.h`

```c
#ifndef _DRIVERS_RADEON_H_
#define _DRIVERS_RADEON_H_

#include <types.h>
#include <drivers/pci.h>
#include <drivers/framebuffer.h>

/* Radeon 寄存器偏移 */
#define RADEON_MM_INDEX         0x0000
#define RADEON_MM_DATA          0x0004
#define RADEON_BIOS_SCRATCH_0   0x0010
#define RADEON_GEN_INT_CNTL     0x0040
#define RADEON_GEN_INT_STATUS   0x0044
#define RADEON_CRTC_GEN_CNTL    0x0050
#define RADEON_CRTC_EXT_CNTL    0x0054
#define RADEON_DAC_CNTL         0x0058
#define RADEON_CRTC_STATUS      0x005C
#define RADEON_GPIO_VGA_DDC     0x0060
#define RADEON_GPIO_DVI_DDC     0x0064
#define RADEON_PALETTE_INDEX    0x00B0
#define RADEON_PALETTE_DATA     0x00B4
#define RADEON_CRTC_H_TOTAL_DISP    0x0200
#define RADEON_CRTC_H_SYNC_STRT_WID 0x0204
#define RADEON_CRTC_V_TOTAL_DISP    0x0208
#define RADEON_CRTC_V_SYNC_STRT_WID 0x020C
#define RADEON_CRTC_OFFSET      0x0224
#define RADEON_CRTC_OFFSET_CNTL 0x0228
#define RADEON_CRTC_PITCH       0x022C
#define RADEON_OVR_CLR          0x0230
#define RADEON_OVR_WID_LEFT_RIGHT   0x0234
#define RADEON_OVR_WID_TOP_BOTTOM   0x0238
#define RADEON_DISPLAY_BASE_ADDR    0x023C
#define RADEON_SNAPSHOT_VH_COUNTS   0x0240
#define RADEON_SNAPSHOT_F_COUNT     0x0244
#define RADEON_CRTC_GUI_TRIG_VLINE  0x0218
#define RADEON_SURFACE_CNTL     0x0B00
#define RADEON_SURFACE0_INFO    0x0B0C
#define RADEON_SURFACE0_LOWER_BOUND 0x0B04
#define RADEON_SURFACE0_UPPER_BOUND 0x0B08
#define RADEON_DEFAULT_OFFSET   0x16E0
#define RADEON_DEFAULT_PITCH    0x16E4
#define RADEON_DEFAULT_SC_BOTTOM_RIGHT  0x16E8
#define RADEON_DP_GUI_MASTER_CNTL   0x146C
#define RADEON_DP_BRUSH_BKGD_CLR    0x1478
#define RADEON_DP_BRUSH_FRGD_CLR    0x147C
#define RADEON_DP_WRITE_MASK    0x16CC
#define RADEON_DP_CNTL          0x16C0
#define RADEON_DST_OFFSET       0x1404
#define RADEON_DST_PITCH        0x1408
#define RADEON_DST_WIDTH        0x140C
#define RADEON_DST_HEIGHT       0x1410
#define RADEON_DST_Y_X          0x1438
#define RADEON_SRC_OFFSET       0x15AC
#define RADEON_SRC_PITCH        0x15B0
#define RADEON_SRC_Y_X          0x1434
#define RADEON_GUI_STAT         0x14E4

/* Radeon 设备结构 */
typedef struct radeon_device {
    pci_device_t *pci_dev;
    volatile uint32_t *mmio;        // MMIO 寄存器基地址
    volatile uint32_t *fb;          // 帧缓冲基地址
    uint32_t fb_size;               // 显存大小
    uint32_t mmio_size;             // MMIO 区域大小
    uint32_t chip_id;               // 芯片 ID
    bool is_mobility;               // 是否为移动版
    framebuffer_info_t fb_info;     // 帧缓冲信息
} radeon_device_t;

/* Radeon 驱动函数 */
int radeon_init(void);
radeon_device_t *radeon_get_device(void);

/* 寄存器访问 */
uint32_t radeon_read_reg(radeon_device_t *dev, uint32_t reg);
void radeon_write_reg(radeon_device_t *dev, uint32_t reg, uint32_t value);

/* 模式设置 */
int radeon_set_mode(radeon_device_t *dev, uint32_t width, uint32_t height, uint32_t bpp);
int radeon_get_modes(radeon_device_t *dev, void *mode_list, int max_modes);

/* 2D 加速 */
void radeon_wait_idle(radeon_device_t *dev);
void radeon_fill_rect(radeon_device_t *dev, int x, int y, int w, int h, uint32_t color);
void radeon_copy_rect(radeon_device_t *dev, int sx, int sy, int dx, int dy, int w, int h);

#endif /* _DRIVERS_RADEON_H_ */
```

---

## 实现步骤

### 步骤 1: 实现 PCI 总线驱动

PCI 驱动是访问显卡的基础。

**文件**: `src/include/drivers/pci.h`
**文件**: `src/drivers/pci.c`

**核心实现**：

```c
// pci.c - PCI 总线驱动核心实现

#include <drivers/pci.h>
#include <kernel/io.h>
#include <lib/kprintf.h>
#include <lib/string.h>
#include <mm/heap.h>

/* 最大支持的 PCI 设备数 */
#define MAX_PCI_DEVICES 64

/* 已发现的设备列表 */
static pci_device_t pci_devices[MAX_PCI_DEVICES];
static int pci_device_count = 0;

/**
 * 构造 PCI 配置空间地址
 */
static uint32_t pci_make_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (uint32_t)((1 << 31) |              // Enable bit
                      ((uint32_t)bus << 16) |   // Bus number
                      ((uint32_t)slot << 11) |  // Device number
                      ((uint32_t)func << 8) |   // Function number
                      (offset & 0xFC));         // Register offset (aligned)
}

/**
 * 读取 PCI 配置空间（32位）
 */
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = pci_make_address(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

/**
 * 读取 PCI 配置空间（16位）
 */
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, slot, func, offset & ~3);
    return (uint16_t)((value >> ((offset & 2) * 8)) & 0xFFFF);
}

/**
 * 读取 PCI 配置空间（8位）
 */
uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, slot, func, offset & ~3);
    return (uint8_t)((value >> ((offset & 3) * 8)) & 0xFF);
}

/**
 * 扫描 PCI 设备
 */
int pci_scan_devices(void) {
    pci_device_count = 0;
    
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint16_t vendor = pci_config_read16(bus, slot, func, PCI_VENDOR_ID);
                
                // 无效的 Vendor ID 表示设备不存在
                if (vendor == 0xFFFF) {
                    if (func == 0) break;  // 非多功能设备
                    continue;
                }
                
                if (pci_device_count >= MAX_PCI_DEVICES) {
                    return pci_device_count;
                }
                
                pci_device_t *dev = &pci_devices[pci_device_count++];
                dev->bus = bus;
                dev->slot = slot;
                dev->func = func;
                dev->vendor_id = vendor;
                dev->device_id = pci_config_read16(bus, slot, func, PCI_DEVICE_ID);
                
                uint32_t class_info = pci_config_read32(bus, slot, func, PCI_CLASS_CODE);
                dev->class_code = (class_info >> 24) & 0xFF;
                dev->subclass = (class_info >> 16) & 0xFF;
                dev->prog_if = (class_info >> 8) & 0xFF;
                dev->revision = class_info & 0xFF;
                
                dev->header_type = pci_config_read8(bus, slot, func, PCI_HEADER_TYPE);
                dev->interrupt_line = pci_config_read8(bus, slot, func, PCI_INTERRUPT_LINE);
                dev->interrupt_pin = pci_config_read8(bus, slot, func, PCI_INTERRUPT_PIN);
                
                // 读取 BAR
                for (int i = 0; i < 6; i++) {
                    dev->bar[i] = pci_config_read32(bus, slot, func, PCI_BAR0 + i * 4);
                }
                
                // 如果不是多功能设备，跳过其他 function
                if (func == 0 && !(dev->header_type & 0x80)) {
                    break;
                }
            }
        }
    }
    
    return pci_device_count;
}

/**
 * 查找指定 Vendor/Device ID 的设备
 */
pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id &&
            pci_devices[i].device_id == device_id) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

/**
 * 查找指定类别的设备
 */
pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].class_code == class_code &&
            pci_devices[i].subclass == subclass) {
            return &pci_devices[i];
        }
    }
    return NULL;
}
```

### 步骤 2: 实现帧缓冲驱动

基于 Multiboot 信息实现帧缓冲。

**文件**: `src/include/drivers/framebuffer.h`
**文件**: `src/drivers/framebuffer.c`

**核心实现**：

```c
// framebuffer.c - 帧缓冲驱动实现

#include <drivers/framebuffer.h>
#include <kernel/multiboot.h>
#include <mm/vmm.h>
#include <lib/string.h>

static framebuffer_info_t fb_info;
static bool fb_initialized = false;

/* 8x16 位图字体（仅示例，实际需完整字体数据） */
static const uint8_t *current_font = NULL;
static int font_width = 8;
static int font_height = 16;

/**
 * 从 Multiboot 信息初始化帧缓冲
 */
int fb_init_from_multiboot(multiboot_info_t *mbi) {
    if (!(mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO)) {
        return -1;  // 没有帧缓冲信息
    }
    
    // 检查是否为图形模式（类型 1 = 图形，0 = 文本）
    if (mbi->framebuffer_type != 1) {
        return -2;  // 不是图形模式
    }
    
    fb_info.address = (uint32_t)mbi->framebuffer_addr;
    fb_info.width = mbi->framebuffer_width;
    fb_info.height = mbi->framebuffer_height;
    fb_info.pitch = mbi->framebuffer_pitch;
    fb_info.bpp = mbi->framebuffer_bpp;
    
    fb_info.red_mask_size = mbi->framebuffer_red_mask_size;
    fb_info.red_field_pos = mbi->framebuffer_red_field_position;
    fb_info.green_mask_size = mbi->framebuffer_green_mask_size;
    fb_info.green_field_pos = mbi->framebuffer_green_field_position;
    fb_info.blue_mask_size = mbi->framebuffer_blue_mask_size;
    fb_info.blue_field_pos = mbi->framebuffer_blue_field_position;
    
    // 确定像素格式
    if (fb_info.bpp == 32) {
        if (fb_info.red_field_pos == 16) {
            fb_info.format = FB_FORMAT_ARGB8888;
        } else {
            fb_info.format = FB_FORMAT_BGRA8888;
        }
    } else if (fb_info.bpp == 24) {
        fb_info.format = FB_FORMAT_RGB888;
    } else if (fb_info.bpp == 16) {
        fb_info.format = FB_FORMAT_RGB565;
    }
    
    // 映射帧缓冲到虚拟地址空间
    uint32_t fb_size = fb_info.pitch * fb_info.height;
    uint32_t fb_pages = (fb_size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    // 将帧缓冲映射到内核虚拟地址空间
    fb_info.buffer = (uint32_t *)vmm_map_physical(fb_info.address, fb_pages, 
                                                   VMM_FLAG_PRESENT | VMM_FLAG_WRITE);
    if (!fb_info.buffer) {
        return -3;  // 映射失败
    }
    
    fb_initialized = true;
    return 0;
}

/**
 * 将 color_t 转换为像素值
 */
static inline uint32_t color_to_pixel(color_t c) {
    switch (fb_info.format) {
        case FB_FORMAT_ARGB8888:
            return (c.a << 24) | (c.r << 16) | (c.g << 8) | c.b;
        case FB_FORMAT_BGRA8888:
            return (c.b << 24) | (c.g << 16) | (c.r << 8) | c.a;
        case FB_FORMAT_RGB888:
            return (c.r << 16) | (c.g << 8) | c.b;
        case FB_FORMAT_RGB565:
            return ((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3);
        default:
            return 0;
    }
}

/**
 * 清屏
 */
void fb_clear(color_t color) {
    if (!fb_initialized) return;
    
    uint32_t pixel = color_to_pixel(color);
    uint32_t *p = fb_info.buffer;
    uint32_t count = (fb_info.pitch * fb_info.height) / 4;
    
    for (uint32_t i = 0; i < count; i++) {
        p[i] = pixel;
    }
}

/**
 * 绘制像素
 */
void fb_put_pixel(int x, int y, color_t color) {
    if (!fb_initialized) return;
    if (x < 0 || x >= (int)fb_info.width || y < 0 || y >= (int)fb_info.height) return;
    
    uint32_t pixel = color_to_pixel(color);
    uint32_t offset = y * fb_info.pitch + x * (fb_info.bpp / 8);
    
    if (fb_info.bpp == 32) {
        *((uint32_t *)((uint8_t *)fb_info.buffer + offset)) = pixel;
    } else if (fb_info.bpp == 24) {
        uint8_t *p = (uint8_t *)fb_info.buffer + offset;
        p[0] = pixel & 0xFF;
        p[1] = (pixel >> 8) & 0xFF;
        p[2] = (pixel >> 16) & 0xFF;
    } else if (fb_info.bpp == 16) {
        *((uint16_t *)((uint8_t *)fb_info.buffer + offset)) = (uint16_t)pixel;
    }
}

/**
 * 填充矩形
 */
void fb_fill_rect(int x, int y, int width, int height, color_t color) {
    if (!fb_initialized) return;
    
    // 裁剪到屏幕范围
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > (int)fb_info.width) { width = fb_info.width - x; }
    if (y + height > (int)fb_info.height) { height = fb_info.height - y; }
    if (width <= 0 || height <= 0) return;
    
    uint32_t pixel = color_to_pixel(color);
    uint32_t bytes_per_pixel = fb_info.bpp / 8;
    
    for (int row = 0; row < height; row++) {
        uint8_t *line = (uint8_t *)fb_info.buffer + (y + row) * fb_info.pitch + x * bytes_per_pixel;
        
        if (fb_info.bpp == 32) {
            uint32_t *p = (uint32_t *)line;
            for (int col = 0; col < width; col++) {
                p[col] = pixel;
            }
        }
    }
}

/**
 * 绘制字符（8x16 位图字体）
 */
void fb_draw_char(int x, int y, char c, color_t fg, color_t bg) {
    if (!fb_initialized || !current_font) return;
    
    const uint8_t *glyph = current_font + (unsigned char)c * font_height;
    
    for (int row = 0; row < font_height; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < font_width; col++) {
            color_t color = (bits & (0x80 >> col)) ? fg : bg;
            fb_put_pixel(x + col, y + row, color);
        }
    }
}

/**
 * 绘制字符串
 */
void fb_draw_string(int x, int y, const char *str, color_t fg, color_t bg) {
    if (!str) return;
    
    int cx = x;
    while (*str) {
        if (*str == '\n') {
            cx = x;
            y += font_height;
        } else {
            fb_draw_char(cx, y, *str, fg, bg);
            cx += font_width;
        }
        str++;
    }
}

/**
 * 获取帧缓冲信息
 */
framebuffer_info_t *fb_get_info(void) {
    return fb_initialized ? &fb_info : NULL;
}
```

### 步骤 3: 修改 Multiboot 头部支持图形模式

**文件**: `src/boot/multiboot.asm`

**关键点**：将 width 和 height 设为 0，让 GRUB 自动选择最佳分辨率：

```nasm
; ============================================================================
; multiboot.asm - Multiboot 头部（支持图形模式）
; ============================================================================

section .multiboot
align 4

; Multiboot 常量定义
MULTIBOOT_MAGIC        equ 0x1BADB002
MULTIBOOT_PAGE_ALIGN   equ 1 << 0      ; 页对齐
MULTIBOOT_MEMORY_INFO  equ 1 << 1      ; 内存信息
MULTIBOOT_VIDEO_MODE   equ 1 << 2      ; 视频模式信息
MULTIBOOT_FLAGS        equ MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO | MULTIBOOT_VIDEO_MODE
MULTIBOOT_CHECKSUM     equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

; Multiboot 头部结构
multiboot_header:
    dd MULTIBOOT_MAGIC              ; 魔数
    dd MULTIBOOT_FLAGS              ; 标志
    dd MULTIBOOT_CHECKSUM           ; 校验和
    ; 以下字段仅在 flags[16] 置位时使用（我们不使用）
    dd 0                            ; header_addr
    dd 0                            ; load_addr
    dd 0                            ; load_end_addr
    dd 0                            ; bss_end_addr
    dd 0                            ; entry_addr
    ; 视频模式字段（flags[2] 置位时使用）
    dd 0                            ; mode_type: 0 = 线性图形模式
    dd 0                            ; width: 0 = 让 GRUB 根据 gfxmode 选择
    dd 0                            ; height: 0 = 让 GRUB 根据 gfxmode 选择
    dd 32                           ; depth: 首选位深度
```

### 步骤 4: 修改 grub.cfg 配置多分辨率支持

**文件**: `grub.cfg`

**多分辨率配置策略**：

```
set timeout=3
set default=0

# ============================================================================
# 多分辨率支持配置
# ============================================================================
# GRUB 会按顺序尝试这些分辨率，选择第一个可用的
# 1400x1050 (SXGA+) - ThinkPad T41 高分屏
# 1024x768 (XGA)    - ThinkPad T41 标准屏
# 800x600 (SVGA)    - 后备分辨率
# ============================================================================

set gfxmode=1400x1050x32,1024x768x32,800x600x32,auto
set gfxpayload=keep

# 加载 GRUB 图形终端（用于显示启动菜单）
insmod all_video
insmod gfxterm
terminal_output gfxterm

menuentry "CastorOS (Auto Resolution)" {
    # gfxpayload=keep 使用 gfxmode 设置的分辨率
    set gfxpayload=keep
    multiboot /boot/castor.bin
    boot
}

menuentry "CastorOS (1400x1050 SXGA+)" {
    set gfxpayload=1400x1050x32
    multiboot /boot/castor.bin
    boot
}

menuentry "CastorOS (1024x768 XGA)" {
    set gfxpayload=1024x768x32
    multiboot /boot/castor.bin
    boot
}

menuentry "CastorOS (Text Mode)" {
    set gfxpayload=text
    multiboot /boot/castor.bin
    boot
}
```

**gfxmode 说明**：
- 使用逗号分隔多个分辨率，GRUB 按顺序尝试
- `auto` 表示使用显示器/显卡的默认分辨率
- GRUB 会自动跳过不支持的分辨率

**验证当前分辨率**：在 GRUB 命令行中输入 `videoinfo` 可查看所有支持的分辨率

### 步骤 5: 实现 EDID 读取（可选，用于验证/显示信息）

通过 ATI Radeon 的 I2C/DDC 接口读取显示器 EDID 信息。

**头文件**: `src/include/drivers/edid.h`

```c
#ifndef _DRIVERS_EDID_H_
#define _DRIVERS_EDID_H_

#include <types.h>

/* EDID 数据块大小 */
#define EDID_BLOCK_SIZE 128

/* EDID 信息结构 */
typedef struct edid_info {
    bool valid;                      // EDID 是否有效
    char manufacturer[4];            // 制造商 ID (3字符 + null)
    uint16_t product_code;           // 产品代码
    uint32_t serial_number;          // 序列号
    uint8_t  week;                   // 生产周
    uint16_t year;                   // 生产年份
    uint8_t  version;                // EDID 版本
    uint8_t  revision;               // EDID 修订
    uint16_t preferred_width;        // 首选宽度（原生分辨率）
    uint16_t preferred_height;       // 首选高度
    uint8_t  preferred_refresh;      // 首选刷新率
    uint8_t  max_horiz_size_cm;      // 最大水平尺寸 (cm)
    uint8_t  max_vert_size_cm;       // 最大垂直尺寸 (cm)
    bool is_digital;                 // 是否为数字显示器
    uint8_t raw[EDID_BLOCK_SIZE];    // 原始 EDID 数据
} edid_info_t;

/* EDID 函数声明 */
int edid_read(edid_info_t *info);
int edid_read_from_radeon(void *mmio_base, edid_info_t *info);
void edid_print_info(const edid_info_t *info);
bool edid_validate(const uint8_t *data);

#endif /* _DRIVERS_EDID_H_ */
```

**实现文件**: `src/drivers/edid.c`

```c
// edid.c - EDID 读取实现

#include <drivers/edid.h>
#include <drivers/radeon.h>
#include <lib/kprintf.h>
#include <lib/string.h>

/* I2C 时序延迟（微秒） */
#define I2C_DELAY_US 10

/* DDC I2C 地址 */
#define DDC_ADDR_WRITE  0xA0  // EDID 写地址
#define DDC_ADDR_READ   0xA1  // EDID 读地址

/* ATI Radeon GPIO 寄存器位 */
#define GPIO_DDC_CLK_OUT    (1 << 0)   // SCL 输出
#define GPIO_DDC_DATA_OUT   (1 << 8)   // SDA 输出
#define GPIO_DDC_CLK_IN     (1 << 16)  // SCL 输入
#define GPIO_DDC_DATA_IN    (1 << 24)  // SDA 输入
#define GPIO_DDC_CLK_EN     (1 << 1)   // SCL 输出使能
#define GPIO_DDC_DATA_EN    (1 << 9)   // SDA 输出使能

/**
 * 通过 Radeon GPIO 实现 I2C bit-bang
 */
static void i2c_set_scl(volatile uint32_t *gpio_reg, bool high) {
    uint32_t val = *gpio_reg;
    if (high) {
        val |= GPIO_DDC_CLK_OUT;   // SCL = 1
    } else {
        val &= ~GPIO_DDC_CLK_OUT;  // SCL = 0
    }
    val |= GPIO_DDC_CLK_EN;        // 启用输出
    *gpio_reg = val;
}

static void i2c_set_sda(volatile uint32_t *gpio_reg, bool high) {
    uint32_t val = *gpio_reg;
    if (high) {
        val |= GPIO_DDC_DATA_OUT;  // SDA = 1
    } else {
        val &= ~GPIO_DDC_DATA_OUT; // SDA = 0
    }
    val |= GPIO_DDC_DATA_EN;       // 启用输出
    *gpio_reg = val;
}

static bool i2c_get_sda(volatile uint32_t *gpio_reg) {
    uint32_t val = *gpio_reg;
    val &= ~GPIO_DDC_DATA_EN;      // 禁用输出（高阻态，用于读取）
    *gpio_reg = val;
    val = *gpio_reg;
    return (val & GPIO_DDC_DATA_IN) != 0;
}

/**
 * 解析 EDID 首选分辨率（从第一个详细时序描述符）
 */
static void edid_parse_preferred_timing(const uint8_t *dtd, edid_info_t *info) {
    // 详细时序描述符格式（18 字节）
    // 检查是否为有效的时序描述符（像素时钟 != 0）
    uint16_t pixel_clock = dtd[0] | (dtd[1] << 8);
    if (pixel_clock == 0) {
        return;  // 不是时序描述符
    }
    
    // 解析水平分辨率
    info->preferred_width = dtd[2] | ((dtd[4] & 0xF0) << 4);
    
    // 解析垂直分辨率
    info->preferred_height = dtd[5] | ((dtd[7] & 0xF0) << 4);
    
    // 计算刷新率（像素时钟 / 总像素数）
    uint16_t h_total = info->preferred_width + (dtd[3] | ((dtd[4] & 0x0F) << 8));
    uint16_t v_total = info->preferred_height + (dtd[6] | ((dtd[7] & 0x0F) << 8));
    if (h_total > 0 && v_total > 0) {
        info->preferred_refresh = (pixel_clock * 10000UL) / (h_total * v_total);
    }
}

/**
 * 解析 EDID 数据
 */
static void edid_parse(const uint8_t *data, edid_info_t *info) {
    // 解析制造商 ID（压缩 ASCII）
    uint16_t mfg = (data[8] << 8) | data[9];
    info->manufacturer[0] = ((mfg >> 10) & 0x1F) + 'A' - 1;
    info->manufacturer[1] = ((mfg >> 5) & 0x1F) + 'A' - 1;
    info->manufacturer[2] = (mfg & 0x1F) + 'A' - 1;
    info->manufacturer[3] = '\0';
    
    // 产品代码和序列号
    info->product_code = data[10] | (data[11] << 8);
    info->serial_number = data[12] | (data[13] << 8) | 
                          (data[14] << 16) | (data[15] << 24);
    
    // 生产日期
    info->week = data[16];
    info->year = data[17] + 1990;
    
    // 版本
    info->version = data[18];
    info->revision = data[19];
    
    // 视频输入类型
    info->is_digital = (data[20] & 0x80) != 0;
    
    // 物理尺寸
    info->max_horiz_size_cm = data[21];
    info->max_vert_size_cm = data[22];
    
    // 解析首选分辨率（第一个详细时序描述符，偏移 54）
    edid_parse_preferred_timing(&data[54], info);
    
    // 保存原始数据
    memcpy(info->raw, data, EDID_BLOCK_SIZE);
    info->valid = true;
}

/**
 * 验证 EDID 校验和
 */
bool edid_validate(const uint8_t *data) {
    // 检查 EDID 头
    static const uint8_t edid_header[] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    if (memcmp(data, edid_header, 8) != 0) {
        return false;
    }
    
    // 校验和
    uint8_t sum = 0;
    for (int i = 0; i < EDID_BLOCK_SIZE; i++) {
        sum += data[i];
    }
    return sum == 0;
}

/**
 * 打印 EDID 信息
 */
void edid_print_info(const edid_info_t *info) {
    if (!info->valid) {
        kprintf("EDID: Invalid or not available\n");
        return;
    }
    
    kprintf("EDID Information:\n");
    kprintf("  Manufacturer: %s\n", info->manufacturer);
    kprintf("  Product Code: 0x%04X\n", info->product_code);
    kprintf("  Serial: %u\n", info->serial_number);
    kprintf("  Manufactured: Week %d, %d\n", info->week, info->year);
    kprintf("  EDID Version: %d.%d\n", info->version, info->revision);
    kprintf("  Display Type: %s\n", info->is_digital ? "Digital" : "Analog");
    kprintf("  Physical Size: %d x %d cm\n", info->max_horiz_size_cm, info->max_vert_size_cm);
    kprintf("  Native Resolution: %dx%d @ %dHz\n", 
            info->preferred_width, info->preferred_height, info->preferred_refresh);
}
```

**使用示例**：

```c
// 在内核初始化时读取 EDID
edid_info_t edid;
if (edid_read(&edid) == 0 && edid.valid) {
    edid_print_info(&edid);
    
    // 验证当前分辨率是否为原生分辨率
    framebuffer_info_t *fb = fb_get_info();
    if (fb->width != edid.preferred_width || fb->height != edid.preferred_height) {
        LOG_WARN_MSG("Warning: Current resolution %dx%d differs from native %dx%d\n",
                     fb->width, fb->height, 
                     edid.preferred_width, edid.preferred_height);
    }
}
```

### 步骤 6: 集成到内核

**更新文件**: `src/kernel/kernel.c`

```c
// ============================================================================
// kernel.c - 内核主函数（支持多分辨率图形模式）
// ============================================================================

// ... 其他 include ...
#include <drivers/pci.h>
#include <drivers/framebuffer.h>
#include <drivers/edid.h>

void kernel_main(multiboot_info_t* mbi) {
    // ========================================================================
    // 阶段 0: 早期初始化
    // ========================================================================    
    vga_init(); // 初始化 VGA（文本模式备用）
    serial_init(); // 初始化串口
    
    // ========================================================================
    // 阶段 4: 设备驱动（Device Drivers）
    // ========================================================================
    
    LOG_INFO_MSG("[Stage 4] Initializing device drivers...\n");
    
    // ... 其他驱动初始化 ...
    
    // 4.5 初始化 PCI 总线
    pci_init();
    int pci_count = pci_scan_devices();
    LOG_INFO_MSG("  [4.5] PCI initialized, found %d devices\n", pci_count);
    pci_print_devices();
    
    // 4.6 初始化图形驱动（支持多分辨率）
    if (fb_init_from_multiboot(mbi) == 0) {
        framebuffer_info_t *fb = fb_get_info();
        LOG_INFO_MSG("  [4.6] Framebuffer initialized: %dx%d @ %dbpp\n",
                     fb->width, fb->height, fb->bpp);
        
        // 根据分辨率显示不同的信息
        const char *resolution_name;
        if (fb->width == 1400 && fb->height == 1050) {
            resolution_name = "SXGA+ (1400x1050)";
        } else if (fb->width == 1024 && fb->height == 768) {
            resolution_name = "XGA (1024x768)";
        } else {
            resolution_name = "Custom";
        }
        LOG_INFO_MSG("  Display mode: %s\n", resolution_name);
        
        // 4.7 读取 EDID 验证原生分辨率（可选）
        edid_info_t edid;
        if (edid_read(&edid) == 0 && edid.valid) {
            LOG_INFO_MSG("  [4.7] EDID: Native resolution %dx%d\n",
                         edid.preferred_width, edid.preferred_height);
            
            // 检查是否使用原生分辨率
            if (fb->width != edid.preferred_width || 
                fb->height != edid.preferred_height) {
                LOG_WARN_MSG("  Warning: Not using native resolution!\n");
            }
        }
        
        // 测试：清屏并绘制图形（位置根据分辨率自适应）
        fb_clear(COLOR_BLACK);
        int center_x = fb->width / 2 - 100;
        int center_y = fb->height / 2 - 50;
        fb_fill_rect(center_x, center_y, 200, 100, COLOR_BLUE);
        fb_draw_string(center_x + 10, center_y + 40, 
                       "CastorOS Graphics Mode!", COLOR_WHITE, COLOR_BLUE);
        
        // 显示分辨率信息
        char res_info[64];
        ksnprintf(res_info, sizeof(res_info), "Resolution: %dx%d", fb->width, fb->height);
        fb_draw_string(10, 10, res_info, COLOR_WHITE, COLOR_BLACK);
    } else {
        LOG_WARN_MSG("  [4.6] Framebuffer not available, using text mode\n");
    }
    
    // ... 其余初始化代码 ...
}
```

---

## 测试计划

### 单元测试

1. **PCI 测试**
   - 验证 PCI 配置空间读写正确性
   - 验证设备枚举完整性
   - 验证 BAR 解析正确性

2. **帧缓冲测试**
   - 验证像素绘制正确性
   - 验证矩形填充正确性
   - 验证文本渲染正确性
   - 验证颜色格式转换

### 集成测试

1. **图形模式启动测试**
   - 验证 GRUB 正确设置图形模式
   - 验证 Multiboot 信息正确传递
   - 验证帧缓冲映射正确

2. **多分辨率测试**
   - 在 QEMU 中测试 1024×768 模式
   - 在 QEMU 中测试 1400×1050 模式
   - 验证 GRUB 分辨率回退机制
   - 验证分辨率自适应 UI 布局

3. **EDID 测试**
   - 验证 EDID 读取正确性
   - 验证原生分辨率检测
   - 测试无 EDID 时的优雅降级

4. **硬件测试**
   - 在 VirtualBox 中测试
   - 在实际 ThinkPad T41 XGA 屏幕上测试
   - 在实际 ThinkPad T41 SXGA+ 屏幕上测试

### Shell 命令

```
lspci           - 列出所有 PCI 设备
fbtest          - 运行帧缓冲测试
fbinfo          - 显示帧缓冲信息（分辨率、颜色深度、物理地址）
edid            - 显示 EDID 信息（制造商、原生分辨率）
gfxdemo         - 运行图形演示（自适应当前分辨率）
```

---

## 限制和未来改进

### 当前限制

1. 仅支持 Multiboot 提供的图形模式
2. 不支持运行时模式切换
3. 不支持硬件加速
4. 字体渲染简单（无抗锯齿）
5. 无 2D/3D 加速

### 未来改进

1. **模式切换**
   - 实现 V86 模式调用 VBE BIOS
   - 支持运行时分辨率切换

2. **ATI 原生驱动**
   - 直接操作 Radeon 寄存器
   - 实现 2D 加速（矩形填充、位块传输）
   - 实现硬件光标

3. **GUI 框架**
   - 窗口管理器
   - 鼠标支持
   - 控件库

4. **高级渲染**
   - 抗锯齿字体（FreeType）
   - Alpha 混合
   - 图片格式支持（BMP、PNG）

---

## 文件清单

### 新增文件

- `src/include/drivers/pci.h` - PCI 总线驱动头文件
- `src/drivers/pci.c` - PCI 总线驱动实现
- `src/include/drivers/framebuffer.h` - 帧缓冲驱动头文件
- `src/drivers/framebuffer.c` - 帧缓冲驱动实现
- `src/include/drivers/edid.h` - EDID 读取驱动头文件
- `src/drivers/edid.c` - EDID 读取驱动实现
- `src/include/drivers/radeon.h` - ATI Radeon 驱动头文件（可选）
- `src/drivers/radeon.c` - ATI Radeon 驱动实现（可选）

### 修改文件

- `src/boot/multiboot.asm` - 添加图形模式请求（支持多分辨率）
- `src/include/kernel/multiboot.h` - 添加帧缓冲相关字段
- `src/kernel/kernel.c` - 集成图形初始化和 EDID 检测
- `grub.cfg` - 配置多分辨率图形模式
- `Makefile` - 添加新文件编译规则

---

## 参考资料

1. [OSDev - PCI](https://wiki.osdev.org/PCI)
2. [OSDev - VESA Video Modes](https://wiki.osdev.org/VESA_Video_Modes)
3. [OSDev - Drawing In Protected Mode](https://wiki.osdev.org/Drawing_In_Protected_Mode)
4. [Multiboot Specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html)
5. [ATI Radeon Register Reference](https://www.x.org/docs/AMD/)
