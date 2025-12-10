/**
 * @file e1000.h
 * @brief Intel E1000 network driver header - architecture wrapper
 * 
 * This file includes the architecture-specific E1000 driver header.
 * E1000 is a PCI device, primarily used on x86.
 */

#ifndef _DRIVERS_E1000_H_
#define _DRIVERS_E1000_H_

#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <drivers/x86/e1000.h>
#else
// E1000 is x86-specific (PCI), provide empty stubs for other architectures
#include <types.h>

static inline void e1000_init(void) {}

#endif

#endif // _DRIVERS_E1000_H_
