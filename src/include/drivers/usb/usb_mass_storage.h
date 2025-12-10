/**
 * @file usb_mass_storage.h
 * @brief USB mass storage driver header - architecture wrapper
 * 
 * This file includes the architecture-specific USB mass storage driver header.
 */

#ifndef _DRIVERS_USB_MASS_STORAGE_H_
#define _DRIVERS_USB_MASS_STORAGE_H_

#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <drivers/x86/usb/usb_mass_storage.h>
#else
// USB mass storage is x86-specific for now, provide empty stubs
#include <types.h>

static inline void usb_mass_storage_init(void) {}

#endif

#endif // _DRIVERS_USB_MASS_STORAGE_H_
