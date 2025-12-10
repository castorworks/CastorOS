/**
 * @file ata.h
 * @brief ATA driver header - architecture wrapper
 * 
 * This file includes the architecture-specific ATA driver header.
 * ATA/IDE is x86-specific.
 */

#ifndef _DRIVERS_ATA_H_
#define _DRIVERS_ATA_H_

#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <drivers/x86/ata.h>
#else
// ATA is x86-specific, provide empty stubs for other architectures
#include <types.h>

static inline void ata_init(void) {}

#endif

#endif // _DRIVERS_ATA_H_
