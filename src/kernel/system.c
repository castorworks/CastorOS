#include <kernel/system.h>
#include <kernel/io.h>

static void system_halt_forever(void) __attribute__((noreturn));

static void system_halt_forever(void) {
    while (1) {
        __asm__ volatile ("hlt");
    }
}

void system_reboot(void) {
    uint8_t temp;

    __asm__ volatile ("cli");

    do {
        temp = inb(0x64);
        if (temp & 0x01) {
            inb(0x60);
        }
    } while (temp & 0x02);

    outb(0x64, 0xFE);

    __asm__ volatile ("int $0x00");

    system_halt_forever();
}

void system_poweroff(void) {
    __asm__ volatile ("cli");

    outw(0xB004, 0x2000);
    outw(0x604, 0x2000);
    outw(0x4004, 0x3400);

    system_halt_forever();
}


