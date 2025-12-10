/**
 * @file usb.h
 * @brief USB driver header - architecture wrapper
 * 
 * This file includes the architecture-specific USB driver header.
 * USB host controller drivers are architecture-specific.
 */

#ifndef _DRIVERS_USB_USB_H_
#define _DRIVERS_USB_USB_H_

#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <drivers/x86/usb/usb.h>
#else
// USB is x86-specific for now, provide empty stubs for other architectures
#include <types.h>

static inline void usb_init(void) {}

#endif

#endif // _DRIVERS_USB_USB_H_
