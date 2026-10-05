#ifndef _DRIVERS_ARM_SERIAL_H_
#define _DRIVERS_ARM_SERIAL_H_

#include <types.h>

/* Early/debug output helpers, callable from assembly */
extern "C" void serial_puts(const char *str);
extern "C" void serial_put_hex64(uint64_t value);

namespace drivers {

/**
 * Kernel console: PL011 UART
 */
class Serial {
public:
    /**
     * Program the UART. Until set_base() is called this is the UART at the QEMU virt
     * address: an early console, so that the kernel can report problems (including
     * problems with the device tree) before it knows where anything is.
     */
    static void init();
    /** Move the console to the UART the device tree describes (physical address) and program it */
    static void set_base(uint64_t phys);
    /** Physical address of the UART in use */
    static uint64_t base();

    static void putchar(char c);

    /** Print a NUL-terminated string ('\n' becomes "\r\n") */
    static void print(const char *msg);

};

} // namespace drivers

#endif /* _DRIVERS_ARM_SERIAL_H_ */
