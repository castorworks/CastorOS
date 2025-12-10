/**
 * @file font8x16.h
 * @brief Font data header - architecture wrapper
 * 
 * This file includes the architecture-specific font data header.
 * Font data is used by framebuffer drivers.
 */

#ifndef _DRIVERS_FONT8X16_H_
#define _DRIVERS_FONT8X16_H_

#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <drivers/x86/font8x16.h>
#elif defined(ARCH_ARM64)
#include <drivers/arm/font8x16.h>
#else
#error "Unknown architecture for font data"
#endif

#endif // _DRIVERS_FONT8X16_H_
