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

/* ============================================================================
 * Initialization
 * ========================================================================== */

/**
 * @brief Initialize the PL011 UART
 * 
 * Configures the UART for 115200 baud, 8N1 (8 data bits, no parity, 1 stop bit).
 * Must be called before using any other serial functions.
 */
void serial_init(void);

/**
 * @brief Check if serial port is initialized
 * @return true if initialized, false otherwise
 */
bool serial_is_initialized(void);

/**
 * @brief Set the UART base address
 * 
 * This should be called before serial_init() if the UART is not at
 * the default address (e.g., when parsed from DTB).
 * 
 * @param base Physical base address of the PL011 UART
 */
void serial_set_base(uint64_t base);

/**
 * @brief Get the current UART base address
 * @return Current UART base address
 */
uint64_t serial_get_base(void);

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
void serial_putchar(char c);

/**
 * @brief Output a null-terminated string
 * 
 * Automatically converts '\n' to '\r\n' for proper line endings.
 * 
 * @param msg String to output
 */
void serial_print(const char *msg);

/**
 * @brief Output a null-terminated string (alias for serial_print)
 * @param str String to output
 */
void serial_puts(const char *str);

/**
 * @brief Read a character from the serial port (blocking)
 * 
 * Waits for a character to be available in the receive FIFO.
 * 
 * @return Character read from serial port
 */
char serial_getchar(void);

/**
 * @brief Check if a character is available to read
 * @return true if a character is available, false otherwise
 */
bool serial_has_char(void);

/**
 * @brief Read a character without blocking
 * @return Character read, or -1 if no character available
 */
int serial_getchar_nonblock(void);

/**
 * @brief Flush the transmit FIFO
 * 
 * Waits until all pending transmissions are complete.
 */
void serial_flush(void);

/* ============================================================================
 * Hex/Decimal Output Helpers
 * ========================================================================== */

/**
 * @brief Output a 32-bit value in hexadecimal
 * @param value Value to output
 */
void serial_put_hex32(uint32_t value);

/**
 * @brief Output a 64-bit value in hexadecimal
 * @param value Value to output
 */
void serial_put_hex64(uint64_t value);

/**
 * @brief Output a decimal number
 * @param value Value to output
 */
void serial_put_dec(uint64_t value);

/* ============================================================================
 * Interrupt Support
 * ========================================================================== */

/**
 * @brief Enable receive interrupt
 */
void serial_enable_rx_interrupt(void);

/**
 * @brief Disable receive interrupt
 */
void serial_disable_rx_interrupt(void);

/**
 * @brief Clear pending interrupts
 */
void serial_clear_interrupts(void);

/**
 * @brief Get masked interrupt status
 * @return Masked interrupt status register value
 */
uint32_t serial_get_interrupt_status(void);

/* ============================================================================
 * PL011 UART IRQ Number (for QEMU virt machine)
 * ========================================================================== */

/** PL011 UART IRQ number on QEMU virt machine (SPI 1 = 32 + 1 = 33) */
#define PL011_IRQ   33

#endif /* _DRIVERS_ARM_SERIAL_H_ */
