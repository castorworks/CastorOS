/**
 * @file serial.h
 * @brief Serial driver header - architecture wrapper
 * 
 * This file includes the architecture-specific serial driver header.
 */

#ifndef _DRIVERS_SERIAL_H_
#define _DRIVERS_SERIAL_H_

#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <drivers/x86/serial.h>
#elif defined(ARCH_ARM64)
#include <drivers/arm/serial.h>
#else
#error "Unknown architecture for serial driver"
#endif

#endif // _DRIVERS_SERIAL_H_
