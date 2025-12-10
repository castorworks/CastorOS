/**
 * @file rtc.h
 * @brief ARM64 RTC driver header
 * 
 * Placeholder for ARM64 RTC driver.
 * ARM64 typically uses PL031 RTC or reads time from DTB/UEFI.
 */

#ifndef _DRIVERS_ARM_RTC_H_
#define _DRIVERS_ARM_RTC_H_

#include <types.h>

/**
 * RTC time structure
 */
typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t weekday;
} rtc_time_t;

/**
 * Initialize ARM64 RTC
 */
void rtc_init(void);

/**
 * Read current time from RTC
 * @param time Output time structure
 */
void rtc_read_time(rtc_time_t *time);

/**
 * Get Unix timestamp
 * @return Seconds since Unix epoch
 */
uint32_t rtc_get_unix_time(void);

#endif /* _DRIVERS_ARM_RTC_H_ */
