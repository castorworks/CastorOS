/**
 * @file serial.h
 * @brief ARM64 PL011 UART Serial Driver Header
 * 
 * This header defines the interface for the ARM PL011 UART serial driver.
 * The PL011 is a full-featured UART commonly found in ARM-based systems
 * including QEMU's virt machine.
 * 
 * Requirements: 9.3 - ARM64 device discovery and drivers
 */

#ifndef _DRIVERS_ARM_SERIAL_H_
#define _DRIVERS_ARM_SERIAL_H_

#include <types.h>

/**
 * @brief Output a null-terminated string (alias for serial_print)
 * @param str String to output
 */
extern "C" void serial_puts(const char *str);

/**
 * @brief Output a 64-bit value in hexadecimal
 * @param value Value to output
 */
extern "C" void serial_put_hex64(uint64_t value);

/** PL011 UART IRQ number on QEMU virt machine (SPI 1 = 32 + 1 = 33) */
#define PL011_IRQ   33

/* ============================================================================
 * PL011 UART IRQ Number (for QEMU virt machine)
 * ========================================================================== */

namespace drivers {

/**
 * @brief 串口驱动（x86: 16550 UART；ARM64: PL011）
 */
class Serial {
public:
    /* ============================================================================
     * Initialization
     * ========================================================================== */

    /**
     * @brief Initialize the PL011 UART
     * 
     * Configures the UART for 115200 baud, 8N1 (8 data bits, no parity, 1 stop bit).
     * Must be called before using any other serial functions.
     */
    static void init();

    /**
     * @brief Check if serial port is initialized
     * @return true if initialized, false otherwise
     */
    static bool is_initialized();

    /**
     * @brief Set the UART base address
     * 
     * This should be called before serial_init() if the UART is not at
     * the default address (e.g., when parsed from DTB).
     * 
     * @param base Physical base address of the PL011 UART
     */
    static void set_base(uint64_t base);

    /**
     * @brief Get the current UART base address
     * @return Current UART base address
     */
    static uint64_t get_base();

    /* ============================================================================
     * Character I/O
     * ========================================================================== */

    /**
     * @brief Output a single character
     * 
     * Waits for the transmit FIFO to have space, then writes the character.
     * 
     * @param c Character to output
     */
    static void putchar(char c);

    /**
     * @brief Output a null-terminated string
     * 
     * Automatically converts '\n' to '\r\n' for proper line endings.
     * 
     * @param msg String to output
     */
    static void print(const char *msg);

    /**
     * @brief Read a character from the serial port (blocking)
     * 
     * Waits until a character has been received. Other tasks and
     * interrupts keep running meanwhile; before the scheduler runs it
     * simply polls.
     * 
     * @return Character read from serial port
     */
    static char getchar();

    /**
     * @brief Check if a character is available to read
     * @return true if a character is available, false otherwise
     */
    static bool has_char();

    /**
     * @brief Read a character without blocking
     * @return Character read, or -1 if no character available
     */
    static int getchar_nonblock();

    /**
     * @brief Queue a character as if it had been received
     * 
     * Puts @p c into the receive buffer, behind anything already queued.
     * Used by tests and by anything that wants to feed console input.
     */
    static void rx_inject(char c);

    /**
     * @brief Flush the transmit FIFO
     * 
     * Waits until all pending transmissions are complete.
     */
    static void flush();

    /* ============================================================================
     * Hex/Decimal Output Helpers
     * ========================================================================== */

    /**
     * @brief Output a 32-bit value in hexadecimal
     * @param value Value to output
     */
    static void put_hex32(uint32_t value);

    /**
     * @brief Output a decimal number
     * @param value Value to output
     */
    static void put_dec(uint64_t value);

    /* ============================================================================
     * Interrupt Support
     * ========================================================================== */

    /**
     * @brief Enable receive interrupt
     */
    static void enable_rx_interrupt();

    /**
     * @brief Disable receive interrupt
     */
    static void disable_rx_interrupt();

    /**
     * @brief Clear pending interrupts
     */
    static void clear_interrupts();

    /**
     * @brief Get masked interrupt status
     * @return Masked interrupt status register value
     */
    static uint32_t get_interrupt_status();
};

} // namespace drivers

#endif /* _DRIVERS_ARM_SERIAL_H_ */
