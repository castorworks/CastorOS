/**
 * rtc.h - 实时时钟（RTC）驱动头文件
 * 
 * 提供读取 CMOS RTC 获取真实日期时间的功能
 */

#ifndef _DRIVERS_X86_RTC_H_
#define _DRIVERS_X86_RTC_H_

#include <types.h>

/**
 * 初始化 RTC 驱动
 * 打印当前日期时间
 */
void rtc_init(void);

/**
 * 获取当前时间
 * @param hours 输出小时（0-23）
 * @param minutes 输出分钟（0-59）
 * @param seconds 输出秒（0-59）
 */
void rtc_get_time(uint8_t *hours, uint8_t *minutes, uint8_t *seconds);

/**
 * 获取当前日期
 * @param year 输出年份（如 2024）
 * @param month 输出月份（1-12）
 * @param day 输出日期（1-31）
 */
void rtc_get_date(uint16_t *year, uint8_t *month, uint8_t *day);

/**
 * 获取星期几
 * @return 星期几（1=周日, 2=周一, ..., 7=周六）
 */
uint8_t rtc_get_weekday(void);

/**
 * 获取 Unix 时间戳
 * 将 RTC 日期时间转换为自 1970-01-01 00:00:00 UTC 以来的秒数
 * @return Unix 时间戳
 */
uint32_t rtc_get_unix_time(void);

#endif /* _DRIVERS_X86_RTC_H_ */

