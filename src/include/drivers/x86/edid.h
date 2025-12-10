/**
 * @file edid.h
 * @brief EDID (Extended Display Identification Data) 读取驱动
 * 
 * 用于读取显示器 EDID 信息，获取原生分辨率等参数
 */

#ifndef _DRIVERS_X86_EDID_H_
#define _DRIVERS_X86_EDID_H_

#include <types.h>

/* ============================================================================
 * EDID 常量定义
 * ============================================================================ */

/** EDID 标准块大小（字节） */
#define EDID_BLOCK_SIZE 128

/** EDID 头标识 */
#define EDID_HEADER_0 0x00
#define EDID_HEADER_1 0xFF
#define EDID_HEADER_2 0xFF
#define EDID_HEADER_3 0xFF
#define EDID_HEADER_4 0xFF
#define EDID_HEADER_5 0xFF
#define EDID_HEADER_6 0xFF
#define EDID_HEADER_7 0x00

/* ============================================================================
 * EDID 信息结构
 * ============================================================================ */

/**
 * @brief EDID 解析后的信息结构
 */
typedef struct edid_info {
    bool valid;                      ///< EDID 是否有效
    char manufacturer[4];            ///< 制造商 ID (3字符 + null)
    uint16_t product_code;           ///< 产品代码
    uint32_t serial_number;          ///< 序列号
    uint8_t  week;                   ///< 生产周
    uint16_t year;                   ///< 生产年份
    uint8_t  version;                ///< EDID 版本
    uint8_t  revision;               ///< EDID 修订
    uint16_t preferred_width;        ///< 首选宽度（原生分辨率）
    uint16_t preferred_height;       ///< 首选高度
    uint8_t  preferred_refresh;      ///< 首选刷新率
    uint8_t  max_horiz_size_cm;      ///< 最大水平尺寸 (cm)
    uint8_t  max_vert_size_cm;       ///< 最大垂直尺寸 (cm)
    bool is_digital;                 ///< 是否为数字显示器
    uint8_t raw[EDID_BLOCK_SIZE];    ///< 原始 EDID 数据
} edid_info_t;

/* ============================================================================
 * 函数声明
 * ============================================================================ */

/**
 * @brief 从 Radeon 显卡读取 EDID
 * @param mmio_base MMIO 基地址
 * @param info 输出 EDID 信息
 * @return 成功返回 0，失败返回负数错误码
 */
int edid_read_from_radeon(volatile uint32_t *mmio_base, edid_info_t *info);

/**
 * @brief 验证 EDID 数据有效性
 * @param data 原始 EDID 数据
 * @return 有效返回 true
 */
bool edid_validate(const uint8_t *data);

/**
 * @brief 解析 EDID 数据
 * @param data 原始 EDID 数据
 * @param info 输出解析后的信息
 * @return 成功返回 0，失败返回负数错误码
 */
int edid_parse(const uint8_t *data, edid_info_t *info);

/**
 * @brief 打印 EDID 信息
 * @param info EDID 信息
 */
void edid_print_info(const edid_info_t *info);

#endif /* _DRIVERS_X86_EDID_H_ */

