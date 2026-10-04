#ifndef _DRIVERS_ARM_SERIAL_H_
#define _DRIVERS_ARM_SERIAL_H_

#include <types.h>

/* Early/debug output helpers, callable from assembly */
extern "C" void serial_puts(const char *str);
extern "C" void serial_put_hex64(uint64_t value);

namespace drivers {

/**
 * Kernel console: PL011 UART (QEMU virt)
 */
class Serial {
public:
    static void init();

    static void putchar(char c);

    /** Print a NUL-terminated string ('\n' becomes "\r\n") */
    static void print(const char *msg);

};

} // namespace drivers

#endif /* _DRIVERS_ARM_SERIAL_H_ */
