/**
 * @file edid.h
 * @brief EDID driver header - architecture wrapper
 * 
 * This file includes the architecture-specific EDID driver header.
 * EDID parsing is x86-specific (VBE/VESA).
 */

#ifndef _DRIVERS_EDID_H_
#define _DRIVERS_EDID_H_

#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <drivers/x86/edid.h>
#else
// EDID is x86-specific, provide empty stubs for other architectures
#include <types.h>

#endif

#endif // _DRIVERS_EDID_H_
