/**
 * @file framebuffer.h
 * @brief Framebuffer driver header - architecture wrapper
 * 
 * This file includes the architecture-specific framebuffer driver header.
 * Multiboot framebuffer is x86-specific.
 */

#ifndef _DRIVERS_FRAMEBUFFER_H_
#define _DRIVERS_FRAMEBUFFER_H_

#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <drivers/x86/framebuffer.h>
#elif defined(ARCH_ARM64)
#include <drivers/arm/framebuffer.h>
#else
#error "Unknown architecture for framebuffer driver"
#endif

#endif // _DRIVERS_FRAMEBUFFER_H_
