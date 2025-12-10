/**
 * @file acpi.h
 * @brief ACPI driver header - architecture wrapper
 * 
 * This file includes the architecture-specific ACPI driver header.
 * ACPI is primarily used on x86, but ARM64 also supports ACPI.
 */

#ifndef _DRIVERS_ACPI_H_
#define _DRIVERS_ACPI_H_

#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <drivers/x86/acpi.h>
#elif defined(ARCH_ARM64)
// ARM64 ACPI support (placeholder)
#include <types.h>

static inline void acpi_init(void) {}
static inline void acpi_shutdown(void) {}
static inline void acpi_reboot(void) {}

#else
#error "Unknown architecture for ACPI driver"
#endif

#endif // _DRIVERS_ACPI_H_
