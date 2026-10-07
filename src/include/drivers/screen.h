/**
 * @file screen.h
 * @brief Screen driver header - architecture wrapper
 *
 * x86 has a VGA text screen; on ARM64 the console is the serial port only,
 * so the screen is an empty class there.
 */

#ifndef _DRIVERS_SCREEN_H_
#define _DRIVERS_SCREEN_H_

#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <drivers/x86/screen.h>
#elif defined(ARCH_ARM64)

namespace drivers {

class Screen {
public:
    static void init() {}
    static void putchar(char) {}
};

} // namespace drivers

#else
#error "Unknown architecture for screen driver"
#endif

#endif // _DRIVERS_SCREEN_H_
