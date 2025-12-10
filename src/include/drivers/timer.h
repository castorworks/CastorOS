/**
 * @file timer.h
 * @brief Timer driver header - architecture wrapper
 * 
 * This file includes the architecture-specific timer driver header.
 */

#ifndef _DRIVERS_TIMER_H_
#define _DRIVERS_TIMER_H_

#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <drivers/x86/timer.h>
#elif defined(ARCH_ARM64)
#include <drivers/arm/timer.h>
#else
#error "Unknown architecture for timer driver"
#endif

#endif // _DRIVERS_TIMER_H_
