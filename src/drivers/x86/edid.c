/**
 * @file edid.c
 * @brief EDID 读取驱动实现
 * 
 * 通过 I2C/DDC 接口读取显示器 EDID 信息
 */

#include <drivers/edid.h>
#include <lib/kprintf.h>
#include <lib/string.h>
#include <lib/klog.h>

/* ============================================================================
 * EDID 标准头
 * ============================================================================ */

static const uint8_t edid_header[] = {
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
};

/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

/**
 * @brief 解析制造商 ID
 * 
 * EDID 使用 PNP ID 编码：3 个字母压缩到 2 字节
 * 每个字母使用 5 位编码（A=1, B=2, ...）
 */
static void edid_parse_manufacturer(uint16_t raw, char *manufacturer) {
    manufacturer[0] = ((raw >> 10) & 0x1F) + 'A' - 1;
    manufacturer[1] = ((raw >> 5) & 0x1F) + 'A' - 1;
    manufacturer[2] = (raw & 0x1F) + 'A' - 1;
    manufacturer[3] = '\0';
}

/**
 * @brief 解析详细时序描述符，提取首选分辨率
 * 
 * 详细时序描述符格式（18 字节）：
 * - 偏移 0-1: 像素时钟（×10 kHz）
 * - 偏移 2: 水平活动像素低 8 位
 * - 偏移 3: 水平消隐低 8 位
 * - 偏移 4: 高 4 位=水平活动高 4 位, 低 4 位=水平消隐高 4 位
 * - 偏移 5: 垂直活动行低 8 位
 * - 偏移 6: 垂直消隐低 8 位
 * - 偏移 7: 高 4 位=垂直活动高 4 位, 低 4 位=垂直消隐高 4 位
 */
static void edid_parse_detailed_timing(const uint8_t *dtd, edid_info_t *info) {
    // 检查是否为有效的时序描述符（像素时钟 != 0）
    uint16_t pixel_clock = dtd[0] | (dtd[1] << 8);
    if (pixel_clock == 0) {
        return;  // 不是时序描述符（可能是显示器名称等）
    }
    
    // 解析水平分辨率
    info->preferred_width = dtd[2] | ((dtd[4] & 0xF0) << 4);
    
    // 解析垂直分辨率
    info->preferred_height = dtd[5] | ((dtd[7] & 0xF0) << 4);
    
    // 计算刷新率
    // 总像素 = 活动像素 + 消隐
    uint16_t h_blank = dtd[3] | ((dtd[4] & 0x0F) << 8);
    uint16_t v_blank = dtd[6] | ((dtd[7] & 0x0F) << 8);
    uint32_t h_total = info->preferred_width + h_blank;
    uint32_t v_total = info->preferred_height + v_blank;
    
    if (h_total > 0 && v_total > 0) {
        // pixel_clock 单位是 10 kHz
        // 刷新率 = 像素时钟 / (水平总数 × 垂直总数)
        uint32_t pixel_clock_hz = pixel_clock * 10000UL;
        info->preferred_refresh = pixel_clock_hz / (h_total * v_total);
    }
}

/* ============================================================================
 * 公共函数
 * ============================================================================ */

bool edid_validate(const uint8_t *data) {
    if (!data) {
        return false;
    }
    
    // 检查 EDID 头
    if (memcmp(data, edid_header, sizeof(edid_header)) != 0) {
        return false;
    }
    
    // 校验和检查：所有字节之和应为 0
    uint8_t sum = 0;
    for (int i = 0; i < EDID_BLOCK_SIZE; i++) {
        sum += data[i];
    }
    
    return sum == 0;
}

int edid_parse(const uint8_t *data, edid_info_t *info) {
    if (!data || !info) {
        return -1;
    }
    
    // 清空结构
    memset(info, 0, sizeof(edid_info_t));
    
    // 验证 EDID
    if (!edid_validate(data)) {
        info->valid = false;
        return -2;
    }
    
    // 保存原始数据
    memcpy(info->raw, data, EDID_BLOCK_SIZE);
    
    // 解析制造商 ID（偏移 8-9，大端序）
    uint16_t mfg_raw = (data[8] << 8) | data[9];
    edid_parse_manufacturer(mfg_raw, info->manufacturer);
    
    // 产品代码（偏移 10-11，小端序）
    info->product_code = data[10] | (data[11] << 8);
    
    // 序列号（偏移 12-15，小端序）
    info->serial_number = data[12] | (data[13] << 8) | 
                          (data[14] << 16) | (data[15] << 24);
    
    // 生产日期（偏移 16-17）
    info->week = data[16];
    info->year = data[17] + 1990;
    
    // EDID 版本（偏移 18-19）
    info->version = data[18];
    info->revision = data[19];
    
    // 视频输入定义（偏移 20）
    // 最高位 = 1 表示数字显示器
    info->is_digital = (data[20] & 0x80) != 0;
    
    // 物理尺寸（偏移 21-22）
    info->max_horiz_size_cm = data[21];
    info->max_vert_size_cm = data[22];
    
    // 解析首选分辨率（第一个详细时序描述符，偏移 54-71）
    edid_parse_detailed_timing(&data[54], info);
    
    info->valid = true;
    return 0;
}

int edid_read_from_radeon(volatile uint32_t *mmio_base, edid_info_t *info) {
    if (!mmio_base || !info) {
        return -1;
    }
    
    // 注意：完整的 I2C/DDC 实现需要位操作 Radeon 的 GPIO 寄存器
    // 这是一个简化的实现，实际硬件可能需要更复杂的时序
    
    // Radeon GPIO DDC 寄存器偏移
    // GPIO_DVI_DDC = 0x0064 用于内置 LCD
    // GPIO_VGA_DDC = 0x0060 用于 VGA 输出
    
    // 由于 I2C bit-bang 实现较复杂，这里返回错误
    // 实际应用中应通过 GRUB 的 EDID 信息或预设值
    LOG_WARN_MSG("edid: I2C DDC read not implemented\n");
    
    memset(info, 0, sizeof(edid_info_t));
    info->valid = false;
    
    return -3;  // 未实现
}

void edid_print_info(const edid_info_t *info) {
    if (!info || !info->valid) {
        kprintf("EDID: Invalid or not available\n");
        return;
    }
    
    kprintf("\n===== EDID Information =====\n");
    kprintf("Manufacturer: %s\n", info->manufacturer);
    kprintf("Product Code: 0x%04X\n", info->product_code);
    kprintf("Serial Number: %u\n", info->serial_number);
    kprintf("Manufactured: Week %d, %d\n", info->week, info->year);
    kprintf("EDID Version: %d.%d\n", info->version, info->revision);
    kprintf("Display Type: %s\n", info->is_digital ? "Digital" : "Analog");
    kprintf("Physical Size: %d x %d cm\n", 
            info->max_horiz_size_cm, info->max_vert_size_cm);
    kprintf("Native Resolution: %dx%d @ %dHz\n", 
            info->preferred_width, info->preferred_height, info->preferred_refresh);
    kprintf("============================\n");
}

