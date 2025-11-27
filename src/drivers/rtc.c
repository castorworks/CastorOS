/**
 * rtc.c - 实时时钟（RTC）驱动实现
 * 
 * 通过 CMOS I/O 端口读取 RTC 寄存器获取真实日期时间
 */

#include <drivers/rtc.h>
#include <kernel/io.h>
#include <lib/klog.h>

// CMOS I/O 端口
#define CMOS_ADDR   0x70    // CMOS 地址端口
#define CMOS_DATA   0x71    // CMOS 数据端口

// RTC 寄存器地址
#define RTC_SECONDS     0x00    // 秒
#define RTC_MINUTES     0x02    // 分
#define RTC_HOURS       0x04    // 时
#define RTC_WEEKDAY     0x06    // 星期几
#define RTC_DAY         0x07    // 日
#define RTC_MONTH       0x08    // 月
#define RTC_YEAR        0x09    // 年（2位）
#define RTC_CENTURY     0x32    // 世纪（部分 RTC 支持）
#define RTC_STATUS_A    0x0A    // 状态寄存器 A
#define RTC_STATUS_B    0x0B    // 状态寄存器 B

// 状态寄存器 A 标志
#define RTC_UPDATE_IN_PROGRESS  0x80    // 正在更新

// 状态寄存器 B 标志
#define RTC_24_HOUR_MODE        0x02    // 24 小时模式
#define RTC_BINARY_MODE         0x04    // 二进制模式（否则为 BCD）

// ============================================================================
// 内部辅助函数
// ============================================================================

/**
 * 读取 CMOS 寄存器
 */
static uint8_t cmos_read(uint8_t reg) {
    // 禁用 NMI（bit 7 = 1）并选择寄存器
    outb(CMOS_ADDR, (1 << 7) | reg);
    return inb(CMOS_DATA);
}

/**
 * 等待 RTC 更新完成
 * RTC 每秒更新一次，更新期间读取的值可能不一致
 * 注意：只需等待更新完成，无需等待更新开始（否则最坏需等待1秒）
 */
static void rtc_wait_update(void) {
    // 只等待更新完成（如果正在更新的话）
    while (cmos_read(RTC_STATUS_A) & RTC_UPDATE_IN_PROGRESS) {
        // 等待
    }
}

/**
 * BCD 转二进制
 * BCD 格式：高 4 位表示十位，低 4 位表示个位
 */
static uint8_t bcd_to_bin(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

/**
 * 判断是否为闰年
 */
static bool is_leap_year(uint16_t year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/**
 * 获取某月的天数
 */
static uint8_t days_in_month(uint16_t year, uint8_t month) {
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    if (month < 1 || month > 12) {
        return 0;
    }
    
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    
    return days[month - 1];
}

// ============================================================================
// 公共接口
// ============================================================================

/**
 * 获取当前时间
 */
void rtc_get_time(uint8_t *hours, uint8_t *minutes, uint8_t *seconds) {
    if (!hours || !minutes || !seconds) {
        return;
    }
    
    // 等待 RTC 更新完成
    rtc_wait_update();
    
    // 读取状态寄存器 B 以确定数据格式
    uint8_t status_b = cmos_read(RTC_STATUS_B);
    bool is_bcd = !(status_b & RTC_BINARY_MODE);
    bool is_24h = (status_b & RTC_24_HOUR_MODE);
    
    // 读取时间
    *seconds = cmos_read(RTC_SECONDS);
    *minutes = cmos_read(RTC_MINUTES);
    *hours = cmos_read(RTC_HOURS);
    
    // 处理 PM 标志（12 小时模式时，bit 7 表示 PM）
    bool is_pm = (*hours & 0x80) != 0;
    
    // 转换 BCD 到二进制
    if (is_bcd) {
        *seconds = bcd_to_bin(*seconds);
        *minutes = bcd_to_bin(*minutes);
        *hours = bcd_to_bin(*hours & 0x7F);  // 清除 PM 标志
    } else {
        *hours = *hours & 0x7F;  // 清除 PM 标志
    }
    
    // 转换 12 小时制到 24 小时制
    if (!is_24h) {
        if (*hours == 12) {
            *hours = is_pm ? 12 : 0;  // 12 AM = 0, 12 PM = 12
        } else if (is_pm) {
            *hours += 12;  // PM 时间加 12
        }
    }
}

/**
 * 获取当前日期
 */
void rtc_get_date(uint16_t *year, uint8_t *month, uint8_t *day) {
    if (!year || !month || !day) {
        return;
    }
    
    // 等待 RTC 更新完成
    rtc_wait_update();
    
    // 读取状态寄存器 B 以确定数据格式
    uint8_t status_b = cmos_read(RTC_STATUS_B);
    bool is_bcd = !(status_b & RTC_BINARY_MODE);
    
    // 读取日期
    *day = cmos_read(RTC_DAY);
    *month = cmos_read(RTC_MONTH);
    uint8_t year_low = cmos_read(RTC_YEAR);
    
    // 尝试读取世纪寄存器（部分 RTC 支持）
    uint8_t century = cmos_read(RTC_CENTURY);
    
    // 转换 BCD 到二进制
    if (is_bcd) {
        *day = bcd_to_bin(*day);
        *month = bcd_to_bin(*month);
        year_low = bcd_to_bin(year_low);
        if (century != 0 && century != 0xFF) {
            century = bcd_to_bin(century);
        }
    }
    
    // 计算完整年份
    if (century >= 19 && century <= 21) {
        // 世纪寄存器有效
        *year = century * 100 + year_low;
    } else {
        // 假设 21 世纪（2000-2099）
        // 如果年份 >= 70，假设是 1970-1999（兼容旧系统）
        if (year_low >= 70) {
            *year = 1900 + year_low;
        } else {
            *year = 2000 + year_low;
        }
    }
}

/**
 * 获取星期几
 */
uint8_t rtc_get_weekday(void) {
    rtc_wait_update();
    
    uint8_t status_b = cmos_read(RTC_STATUS_B);
    bool is_bcd = !(status_b & RTC_BINARY_MODE);
    
    uint8_t weekday = cmos_read(RTC_WEEKDAY);
    
    if (is_bcd) {
        weekday = bcd_to_bin(weekday);
    }
    
    return weekday;
}

/**
 * 获取 Unix 时间戳
 * 将 RTC 日期时间转换为自 1970-01-01 00:00:00 UTC 以来的秒数
 */
uint32_t rtc_get_unix_time(void) {
    uint16_t year;
    uint8_t month, day, hours, minutes, seconds;
    
    rtc_get_date(&year, &month, &day);
    rtc_get_time(&hours, &minutes, &seconds);
    
    // 计算从 1970 年到现在的天数
    uint32_t days = 0;
    
    // 加上完整年份的天数
    for (uint16_t y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }
    
    // 加上完整月份的天数
    for (uint8_t m = 1; m < month; m++) {
        days += days_in_month(year, m);
    }
    
    // 加上当月的天数（日期从 1 开始，所以减 1）
    days += day - 1;
    
    // 转换为秒
    uint32_t timestamp = days * 86400;  // 86400 = 24 * 60 * 60
    timestamp += hours * 3600;
    timestamp += minutes * 60;
    timestamp += seconds;
    
    return timestamp;
}

/**
 * 初始化 RTC 驱动
 */
void rtc_init(void) {
    LOG_INFO_MSG("RTC: Initializing real-time clock driver...\n");
    
    uint16_t year;
    uint8_t month, day, hours, minutes, seconds;
    
    rtc_get_date(&year, &month, &day);
    rtc_get_time(&hours, &minutes, &seconds);
    
    // 星期几的名称
    static const char *weekday_names[] = {
        "", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    uint8_t weekday = rtc_get_weekday();
    const char *weekday_name = (weekday >= 1 && weekday <= 7) ? weekday_names[weekday] : "???";
    
    LOG_INFO_MSG("RTC: Current time: %04u-%02u-%02u (%s) %02u:%02u:%02u\n",
                 year, month, day, weekday_name, hours, minutes, seconds);
    
    // 显示 Unix 时间戳
    uint32_t unix_time = rtc_get_unix_time();
    LOG_INFO_MSG("RTC: Unix timestamp: %u\n", unix_time);
    
    LOG_INFO_MSG("RTC: Driver initialized\n");
}

