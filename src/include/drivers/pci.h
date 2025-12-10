/**
 * @file pci.h
 * @brief PCI driver header - architecture wrapper
 * 
 * This file includes the architecture-specific PCI driver header.
 * PCI configuration space access is x86-specific (port I/O).
 */

#ifndef _DRIVERS_PCI_H_
#define _DRIVERS_PCI_H_

#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <drivers/x86/pci.h>
#else
// PCI port I/O is x86-specific, provide empty stubs for other architectures
// ARM64 uses ECAM (memory-mapped) for PCI access
#include <types.h>

static inline void pci_init(void) {}

#endif

#endif // _DRIVERS_PCI_H_
