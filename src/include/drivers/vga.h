/**
 * @file vga.h
 * @brief VGA driver header - architecture wrapper
 * 
 * This file includes the architecture-specific VGA driver header.
 * VGA is x86-specific.
 */

#ifndef _DRIVERS_VGA_H_
#define _DRIVERS_VGA_H_

#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <drivers/x86/vga.h>
#else
// VGA is x86-specific, provide empty stubs for other architectures
#include <types.h>

typedef uint8_t vga_color_t;

static inline void vga_init(void) {}
static inline void vga_clear(void) {}
static inline void vga_putchar(char c) { (void)c; }
static inline void vga_print(const char *str) { (void)str; }
static inline void vga_set_color(vga_color_t fg, vga_color_t bg) { (void)fg; (void)bg; }

#endif

#endif // _DRIVERS_VGA_H_
