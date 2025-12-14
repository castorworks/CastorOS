/**
 * @file font8x16.h
 * @brief 8x16 bitmap font data for ARM64
 * 
 * Standard VGA-compatible font, 8 pixels wide, 16 pixels tall.
 * Include the x86 font data directly since it's architecture-independent.
 */

#ifndef _DRIVERS_ARM_FONT8X16_H_
#define _DRIVERS_ARM_FONT8X16_H_

/* Font data is architecture-independent, reuse x86 definition */
#include <drivers/x86/font8x16.h>

#endif /* _DRIVERS_ARM_FONT8X16_H_ */
