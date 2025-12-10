/**
 * @file acpi.h
 * @brief ACPI (Advanced Configuration and Power Interface) 驱动
 * 
 * 实现 ACPI 1.0/2.0 表解析，支持系统电源管理（关机、重启、睡眠）
 * 适用于 ThinkPad T41 等老旧 PC 硬件
 */

#ifndef _DRIVERS_X86_ACPI_H_
#define _DRIVERS_X86_ACPI_H_

#include <types.h>

/* ============================================================================
 * ACPI 表签名定义
 * ============================================================================ */

#define ACPI_SIG_RSDP   "RSD PTR "   // RSDP 签名（8字节）
#define ACPI_SIG_RSDT   "RSDT"       // Root System Description Table
#define ACPI_SIG_XSDT   "XSDT"       // Extended System Description Table
#define ACPI_SIG_FADT   "FACP"       // Fixed ACPI Description Table
#define ACPI_SIG_DSDT   "DSDT"       // Differentiated System Description Table
#define ACPI_SIG_MADT   "APIC"       // Multiple APIC Description Table

/* ============================================================================
 * ACPI 电源状态定义
 * ============================================================================ */

#define ACPI_STATE_S0   0   // 工作状态（全功率）
#define ACPI_STATE_S1   1   // 睡眠状态（CPU 停止，上下文保留）
#define ACPI_STATE_S2   2   // 睡眠状态（CPU 断电）
#define ACPI_STATE_S3   3   // 挂起到内存（STR/Suspend to RAM）
#define ACPI_STATE_S4   4   // 挂起到磁盘（Hibernate）
#define ACPI_STATE_S5   5   // 软关机（Soft Off）

/* SLP_EN - 睡眠使能位 (PM1_CNT 寄存器 bit 13) */
#define ACPI_SLP_EN     (1 << 13)

/* ============================================================================
 * ACPI 表结构定义
 * ============================================================================ */

/**
 * @brief ACPI 表头（所有 ACPI 表的通用头部）
 */
typedef struct {
    char signature[4];          // 表签名（如 "FACP", "DSDT"）
    uint32_t length;            // 整个表的长度（包括头部）
    uint8_t revision;           // ACPI 规范版本
    uint8_t checksum;           // 校验和（所有字节相加应为 0）
    char oem_id[6];             // OEM 标识
    char oem_table_id[8];       // OEM 表标识
    uint32_t oem_revision;      // OEM 版本
    char creator_id[4];         // 创建者 ID
    uint32_t creator_revision;  // 创建者版本
} __attribute__((packed)) acpi_sdt_header_t;

/**
 * @brief RSDP - Root System Description Pointer (ACPI 1.0)
 * 
 * RSDP 位于 BIOS 数据区域，是 ACPI 表的入口点
 */
typedef struct {
    char signature[8];          // "RSD PTR " (注意末尾空格)
    uint8_t checksum;           // ACPI 1.0 校验和（前 20 字节）
    char oem_id[6];             // OEM 标识
    uint8_t revision;           // ACPI 版本：0 = 1.0, 2 = 2.0+
    uint32_t rsdt_address;      // RSDT 物理地址
} __attribute__((packed)) acpi_rsdp_v1_t;

/**
 * @brief RSDP - Root System Description Pointer (ACPI 2.0+)
 * 
 * ACPI 2.0 扩展，包含 XSDT 地址
 */
typedef struct {
    // ACPI 1.0 部分
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    
    // ACPI 2.0+ 扩展
    uint32_t length;            // RSDP 结构长度
    uint64_t xsdt_address;      // XSDT 物理地址（64位）
    uint8_t extended_checksum;  // 扩展校验和
    uint8_t reserved[3];
} __attribute__((packed)) acpi_rsdp_v2_t;

/**
 * @brief RSDT - Root System Description Table
 * 
 * 包含指向其他 ACPI 表的 32 位指针数组
 */
typedef struct {
    acpi_sdt_header_t header;
    // 后面跟着 (header.length - sizeof(header)) / 4 个 32 位表地址
} __attribute__((packed)) acpi_rsdt_t;

/**
 * @brief Generic Address Structure (ACPI 2.0+)
 */
typedef struct {
    uint8_t address_space;      // 地址空间类型
    uint8_t bit_width;          // 寄存器位宽
    uint8_t bit_offset;         // 位偏移
    uint8_t access_size;        // 访问大小
    uint64_t address;           // 64 位地址
} __attribute__((packed)) acpi_generic_address_t;

/**
 * @brief FADT - Fixed ACPI Description Table (精简版)
 * 
 * 包含 ACPI 固定硬件的配置信息
 * ThinkPad T41 使用 ACPI 1.0/2.0，我们需要兼容两种版本
 */
typedef struct {
    acpi_sdt_header_t header;
    
    uint32_t firmware_ctrl;     // FACS 物理地址
    uint32_t dsdt;              // DSDT 物理地址
    
    uint8_t reserved1;          // ACPI 1.0: INT_MODEL, 2.0+: reserved
    uint8_t preferred_pm_profile;
    uint16_t sci_int;           // SCI 中断号
    uint32_t smi_cmd;           // SMI 命令端口
    uint8_t acpi_enable;        // 启用 ACPI 的 SMI 命令
    uint8_t acpi_disable;       // 禁用 ACPI 的 SMI 命令
    uint8_t s4bios_req;         // S4BIOS 请求命令
    uint8_t pstate_cnt;         // P-State 控制
    
    uint32_t pm1a_evt_blk;      // PM1a 事件块端口
    uint32_t pm1b_evt_blk;      // PM1b 事件块端口
    uint32_t pm1a_cnt_blk;      // PM1a 控制块端口 *** 关机用 ***
    uint32_t pm1b_cnt_blk;      // PM1b 控制块端口 *** 关机用 ***
    uint32_t pm2_cnt_blk;       // PM2 控制块端口
    uint32_t pm_tmr_blk;        // PM 定时器块端口
    uint32_t gpe0_blk;          // GPE0 块端口
    uint32_t gpe1_blk;          // GPE1 块端口
    
    uint8_t pm1_evt_len;        // PM1 事件块长度
    uint8_t pm1_cnt_len;        // PM1 控制块长度
    uint8_t pm2_cnt_len;        // PM2 控制块长度
    uint8_t pm_tmr_len;         // PM 定时器块长度
    uint8_t gpe0_blk_len;       // GPE0 块长度
    uint8_t gpe1_blk_len;       // GPE1 块长度
    uint8_t gpe1_base;          // GPE1 基址
    uint8_t cst_cnt;            // C-State 控制
    uint16_t p_lvl2_lat;        // P_LVL2 延迟
    uint16_t p_lvl3_lat;        // P_LVL3 延迟
    uint16_t flush_size;        // 缓存刷新大小
    uint16_t flush_stride;      // 缓存刷新步长
    uint8_t duty_offset;        // Duty Cycle 偏移
    uint8_t duty_width;         // Duty Cycle 宽度
    uint8_t day_alrm;           // RTC 日期报警
    uint8_t mon_alrm;           // RTC 月份报警
    uint8_t century;            // RTC 世纪
    
    uint16_t boot_arch_flags;   // 引导架构标志 (ACPI 2.0+)
    uint8_t reserved2;
    uint32_t flags;             // 固定特性标志
    
    // ACPI 2.0+ 扩展的 Generic Address 结构
    // 我们先不使用这些，因为 ThinkPad T41 可能使用 ACPI 1.0
    acpi_generic_address_t reset_reg;
    uint8_t reset_value;
    uint8_t reserved3[3];
    
    uint64_t x_firmware_ctrl;   // 64 位 FACS 地址
    uint64_t x_dsdt;            // 64 位 DSDT 地址
    
    acpi_generic_address_t x_pm1a_evt_blk;
    acpi_generic_address_t x_pm1b_evt_blk;
    acpi_generic_address_t x_pm1a_cnt_blk;
    acpi_generic_address_t x_pm1b_cnt_blk;
    acpi_generic_address_t x_pm2_cnt_blk;
    acpi_generic_address_t x_pm_tmr_blk;
    acpi_generic_address_t x_gpe0_blk;
    acpi_generic_address_t x_gpe1_blk;
} __attribute__((packed)) acpi_fadt_t;

/**
 * @brief ACPI 信息结构（运行时使用）
 */
typedef struct {
    bool initialized;           // ACPI 是否已初始化
    uint8_t revision;           // ACPI 版本
    
    // 表指针
    acpi_rsdp_v1_t *rsdp;       // RSDP 指针
    acpi_rsdt_t *rsdt;          // RSDT 指针
    acpi_fadt_t *fadt;          // FADT 指针
    acpi_sdt_header_t *dsdt;    // DSDT 指针
    
    // PM 控制端口
    uint32_t pm1a_cnt_blk;      // PM1a 控制块端口
    uint32_t pm1b_cnt_blk;      // PM1b 控制块端口
    uint16_t pm1_cnt_len;       // PM1 控制块长度
    
    // S5 (软关机) 状态值
    uint16_t slp_typa;          // SLP_TYPa 值
    uint16_t slp_typb;          // SLP_TYPb 值
    bool s5_valid;              // S5 值是否有效
    
    // SCI 中断
    uint16_t sci_int;           // SCI 中断号
} acpi_info_t;

/* ============================================================================
 * 函数声明
 * ============================================================================ */

/**
 * @brief 初始化 ACPI 子系统
 * 
 * 搜索 RSDP，解析 RSDT/FADT/DSDT，提取电源管理信息
 * 
 * @return 0 成功，-1 失败
 */
int acpi_init(void);

/**
 * @brief 检查 ACPI 是否已初始化
 * 
 * @return true 已初始化，false 未初始化
 */
bool acpi_is_initialized(void);

/**
 * @brief 获取 ACPI 信息结构
 * 
 * @return ACPI 信息结构指针
 */
acpi_info_t *acpi_get_info(void);

/**
 * @brief 通过 ACPI 关机
 * 
 * 使用 ACPI PM1 控制寄存器触发 S5（软关机）状态
 * 
 * @return 如果失败返回 -1，成功则不返回
 */
int acpi_poweroff(void);

/**
 * @brief 通过 ACPI 重启
 * 
 * 使用 ACPI Reset 寄存器触发重启（如果支持）
 * 
 * @return 如果失败返回 -1，成功则不返回
 */
int acpi_reset(void);

/**
 * @brief 启用 ACPI 模式
 * 
 * 向 SMI 命令端口发送 ACPI_ENABLE 命令
 * 
 * @return 0 成功，-1 失败
 */
int acpi_enable(void);

/**
 * @brief 打印 ACPI 信息
 */
void acpi_print_info(void);

#endif /* _DRIVERS_X86_ACPI_H_ */

