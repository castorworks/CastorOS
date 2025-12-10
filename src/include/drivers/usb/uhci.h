/**
 * @file uhci.h
 * @brief UHCI USB host controller driver header - architecture wrapper
 * 
 * This file includes the architecture-specific UHCI driver header.
 * UHCI is a PCI device, primarily used on x86.
 */

#ifndef _DRIVERS_USB_UHCI_H_
#define _DRIVERS_USB_UHCI_H_

#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <drivers/x86/usb/uhci.h>
#else
// UHCI is x86-specific (PCI), provide empty stubs for other architectures
#include <types.h>

static inline void uhci_init(void) {}

#endif

#endif // _DRIVERS_USB_UHCI_H_
