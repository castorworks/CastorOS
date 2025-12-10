/**
 * @file rtc.h
 * @brief RTC driver header - architecture wrapper
 * 
 * This file includes the architecture-specific RTC driver header.
 * CMOS RTC is x86-specific.
 */

#ifndef _DRIVERS_RTC_H_
#define _DRIVERS_RTC_H_

#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <drivers/x86/rtc.h>
#elif defined(ARCH_ARM64)
#include <drivers/arm/rtc.h>
#else
#error "Unknown architecture for RTC driver"
#endif

#endif // _DRIVERS_RTC_H_
