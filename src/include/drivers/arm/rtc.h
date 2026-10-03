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

namespace drivers {

/**
 * @brief 实时时钟
 */
class Rtc {
public:
    /**
     * Initialize ARM64 RTC
     */
    static void init();

    /**
     * Read current time from RTC
     * @param time Output time structure
     */
    static void read_time(rtc_time_t *time);

    /**
     * Get Unix timestamp
     * @return Seconds since Unix epoch
     */
    static uint32_t get_unix_time();
};

} // namespace drivers

#endif /* _DRIVERS_ARM_RTC_H_ */
