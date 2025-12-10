/**
 * @file font8x16.h
 * @brief 8x16 bitmap font data for ARM64
 * 
 * Standard VGA-compatible font, 8 pixels wide, 16 pixels tall.
 * This is architecture-independent font data, shared with x86.
 */

#ifndef _DRIVERS_ARM_FONT8X16_H_
#define _DRIVERS_ARM_FONT8X16_H_

#include <types.h>

/**
 * 8x16 font bitmap data
 * 256 characters × 16 bytes per character = 4096 bytes
 */
extern const uint8_t font8x16_data[4096];

#endif /* _DRIVERS_ARM_FONT8X16_H_ */
