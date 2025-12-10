/**
 * @file keyboard.h
 * @brief Keyboard driver header - architecture wrapper
 * 
 * This file includes the architecture-specific keyboard driver header.
 * PS/2 keyboard is x86-specific.
 */

#ifndef _DRIVERS_KEYBOARD_H_
#define _DRIVERS_KEYBOARD_H_

#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <drivers/x86/keyboard.h>
#else
// PS/2 keyboard is x86-specific, provide empty stubs for other architectures
#include <types.h>

static inline void keyboard_init(void) {}
static inline char keyboard_getchar(void) { return 0; }
static inline bool keyboard_has_key(void) { return false; }

#endif

#endif // _DRIVERS_KEYBOARD_H_
