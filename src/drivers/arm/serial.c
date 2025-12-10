/**
 * @file serial.c
 * @brief ARM64 PL011 UART Serial Driver
 * 
 * This driver implements serial communication using the ARM PL011 UART
 * controller, commonly found in ARM-based systems including QEMU's virt
 * machine.
 * 
 * The PL011 is a full-featured UART with:
 * - Configurable baud rate
 * - 16-byte transmit and receive FIFOs
 * - Hardware flow control (RTS/CTS)
 * - Programmable FIFO trigger levels
 * - Interrupt support
 * 
 * For QEMU virt machine, the UART is at 0x09000000 with IRQ 33.
 * 
 * Requirements: 9.3 - ARM64 device discovery and drivers
 */

#include <drivers/arm/serial.h>
#include <types.h>

/* ============================================================================
 * PL011 UART Register Definitions
 * 
 * Based on ARM PrimeCell UART (PL011) Technical Reference Manual
 * ========================================================================== */

/** Default UART base address for QEMU virt machine */
#define PL011_DEFAULT_BASE  0x09000000ULL

/** UART base address (can be updated from DTB) */
static volatile uint8_t *uart_base = (volatile uint8_t *)PL011_DEFAULT_BASE;

/* Register offsets from base address */
#define PL011_DR        0x000   /**< Data Register */
#define PL011_RSR       0x004   /**< Receive Status Register / Error Clear Register */
#define PL011_FR        0x018   /**< Flag Register */
#define PL011_ILPR      0x020   /**< IrDA Low-Power Counter Register */
#define PL011_IBRD      0x024   /**< Integer Baud Rate Register */
#define PL011_FBRD      0x028   /**< Fractional Baud Rate Register */
#define PL011_LCR_H     0x02C   /**< Line Control Register */
#define PL011_CR        0x030   /**< Control Register */
#define PL011_IFLS      0x034   /**< Interrupt FIFO Level Select Register */
#define PL011_IMSC      0x038   /**< Interrupt Mask Set/Clear Register */
#define PL011_RIS       0x03C   /**< Raw Interrupt Status Register */
#define PL011_MIS       0x040   /**< Masked Interrupt Status Register */
#define PL011_ICR       0x044   /**< Interrupt Clear Register */
#define PL011_DMACR     0x048   /**< DMA Control Register */

/* Flag Register (FR) bits */
#define PL011_FR_RI     (1 << 8)    /**< Ring indicator */
#define PL011_FR_TXFE   (1 << 7)    /**< Transmit FIFO empty */
#define PL011_FR_RXFF   (1 << 6)    /**< Receive FIFO full */
#define PL011_FR_TXFF   (1 << 5)    /**< Transmit FIFO full */
#define PL011_FR_RXFE   (1 << 4)    /**< Receive FIFO empty */
#define PL011_FR_BUSY   (1 << 3)    /**< UART busy */
#define PL011_FR_DCD    (1 << 2)    /**< Data carrier detect */
#define PL011_FR_DSR    (1 << 1)    /**< Data set ready */
#define PL011_FR_CTS    (1 << 0)    /**< Clear to send */

/* Line Control Register (LCR_H) bits */
#define PL011_LCR_H_SPS     (1 << 7)    /**< Stick parity select */
#define PL011_LCR_H_WLEN_8  (3 << 5)    /**< 8 bits word length */
#define PL011_LCR_H_WLEN_7  (2 << 5)    /**< 7 bits word length */
#define PL011_LCR_H_WLEN_6  (1 << 5)    /**< 6 bits word length */
#define PL011_LCR_H_WLEN_5  (0 << 5)    /**< 5 bits word length */
#define PL011_LCR_H_FEN     (1 << 4)    /**< FIFO enable */
#define PL011_LCR_H_STP2    (1 << 3)    /**< Two stop bits select */
#define PL011_LCR_H_EPS     (1 << 2)    /**< Even parity select */
#define PL011_LCR_H_PEN     (1 << 1)    /**< Parity enable */
#define PL011_LCR_H_BRK     (1 << 0)    /**< Send break */

/* Control Register (CR) bits */
#define PL011_CR_CTSEN  (1 << 15)   /**< CTS hardware flow control enable */
#define PL011_CR_RTSEN  (1 << 14)   /**< RTS hardware flow control enable */
#define PL011_CR_OUT2   (1 << 13)   /**< Complement of Out2 modem status output */
#define PL011_CR_OUT1   (1 << 12)   /**< Complement of Out1 modem status output */
#define PL011_CR_RTS    (1 << 11)   /**< Request to send */
#define PL011_CR_DTR    (1 << 10)   /**< Data transmit ready */
#define PL011_CR_RXE    (1 << 9)    /**< Receive enable */
#define PL011_CR_TXE    (1 << 8)    /**< Transmit enable */
#define PL011_CR_LBE    (1 << 7)    /**< Loopback enable */
#define PL011_CR_SIRLP  (1 << 2)    /**< SIR low-power IrDA mode */
#define PL011_CR_SIREN  (1 << 1)    /**< SIR enable */
#define PL011_CR_UARTEN (1 << 0)    /**< UART enable */

/* Interrupt bits (for IMSC, RIS, MIS, ICR) */
#define PL011_INT_OE    (1 << 10)   /**< Overrun error interrupt */
#define PL011_INT_BE    (1 << 9)    /**< Break error interrupt */
#define PL011_INT_PE    (1 << 8)    /**< Parity error interrupt */
#define PL011_INT_FE    (1 << 7)    /**< Framing error interrupt */
#define PL011_INT_RT    (1 << 6)    /**< Receive timeout interrupt */
#define PL011_INT_TX    (1 << 5)    /**< Transmit interrupt */
#define PL011_INT_RX    (1 << 4)    /**< Receive interrupt */
#define PL011_INT_DSR   (1 << 3)    /**< DSR modem interrupt */
#define PL011_INT_DCD   (1 << 2)    /**< DCD modem interrupt */
#define PL011_INT_CTS   (1 << 1)    /**< CTS modem interrupt */
#define PL011_INT_RI    (1 << 0)    /**< RI modem interrupt */

/* ============================================================================
 * Register Access Helpers
 * ========================================================================== */

/**
 * @brief Read a 32-bit register
 */
static inline uint32_t pl011_read(uint32_t offset) {
    return *(volatile uint32_t *)(uart_base + offset);
}

/**
 * @brief Write a 32-bit register
 */
static inline void pl011_write(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(uart_base + offset) = value;
}

/* ============================================================================
 * Initialization State
 * ========================================================================== */

static bool serial_initialized = false;

/* ============================================================================
 * Public API Implementation
 * ========================================================================== */

/**
 * @brief Set the UART base address
 * 
 * This should be called before serial_init() if the UART is not at
 * the default address (e.g., when parsed from DTB).
 * 
 * @param base Physical base address of the PL011 UART
 */
void serial_set_base(uint64_t base) {
    uart_base = (volatile uint8_t *)base;
}

/**
 * @brief Get the current UART base address
 * @return Current UART base address
 */
uint64_t serial_get_base(void) {
    return (uint64_t)uart_base;
}

/**
 * @brief Initialize the PL011 UART
 * 
 * Configures the UART for 115200 baud, 8N1 (8 data bits, no parity, 1 stop bit).
 * QEMU's PL011 emulation doesn't require baud rate configuration, but we
 * set it anyway for compatibility with real hardware.
 */
void serial_init(void) {
    /* Disable UART while configuring */
    pl011_write(PL011_CR, 0);
    
    /* Wait for any pending transmissions to complete */
    while (pl011_read(PL011_FR) & PL011_FR_BUSY) {
        __asm__ volatile("nop");
    }
    
    /* Clear all pending interrupts */
    pl011_write(PL011_ICR, 0x7FF);
    
    /* Disable all interrupts */
    pl011_write(PL011_IMSC, 0);
    
    /*
     * Set baud rate to 115200
     * 
     * For QEMU virt machine, the UART clock is typically 24MHz.
     * Baud rate divisor = UARTCLK / (16 * Baud Rate)
     * For 115200 baud with 24MHz clock:
     *   Divisor = 24000000 / (16 * 115200) = 13.0208...
     *   IBRD = 13
     *   FBRD = round(0.0208 * 64) = 1
     * 
     * Note: QEMU ignores these values, but real hardware needs them.
     */
    pl011_write(PL011_IBRD, 13);
    pl011_write(PL011_FBRD, 1);
    
    /*
     * Configure line control:
     * - 8 data bits
     * - No parity
     * - 1 stop bit
     * - Enable FIFOs
     */
    pl011_write(PL011_LCR_H, PL011_LCR_H_WLEN_8 | PL011_LCR_H_FEN);
    
    /*
     * Enable UART:
     * - Enable UART
     * - Enable transmit
     * - Enable receive
     */
    pl011_write(PL011_CR, PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE);
    
    serial_initialized = true;
}

/**
 * @brief Check if serial port is initialized
 * @return true if initialized, false otherwise
 */
bool serial_is_initialized(void) {
    return serial_initialized;
}

/**
 * @brief Output a single character
 * 
 * Waits for the transmit FIFO to have space, then writes the character.
 * 
 * @param c Character to output
 */
void serial_putchar(char c) {
    /* Wait until transmit FIFO is not full */
    while (pl011_read(PL011_FR) & PL011_FR_TXFF) {
        __asm__ volatile("nop");
    }
    
    /* Write character to data register */
    pl011_write(PL011_DR, (uint32_t)c);
}

/**
 * @brief Output a null-terminated string
 * 
 * Automatically converts '\n' to '\r\n' for proper line endings.
 * 
 * @param msg String to output
 */
void serial_print(const char *msg) {
    if (!msg) {
        return;
    }
    
    while (*msg) {
        if (*msg == '\n') {
            serial_putchar('\r');
        }
        serial_putchar(*msg++);
    }
}

/**
 * @brief Output a null-terminated string (alias for serial_print)
 * @param str String to output
 */
void serial_puts(const char *str) {
    serial_print(str);
}

/**
 * @brief Read a character from the serial port
 * 
 * Waits for a character to be available in the receive FIFO.
 * 
 * @return Character read from serial port
 */
char serial_getchar(void) {
    /* Wait until receive FIFO is not empty */
    while (pl011_read(PL011_FR) & PL011_FR_RXFE) {
        __asm__ volatile("nop");
    }
    
    /* Read character from data register */
    return (char)(pl011_read(PL011_DR) & 0xFF);
}

/**
 * @brief Check if a character is available to read
 * @return true if a character is available, false otherwise
 */
bool serial_has_char(void) {
    return !(pl011_read(PL011_FR) & PL011_FR_RXFE);
}

/**
 * @brief Read a character without blocking
 * @return Character read, or -1 if no character available
 */
int serial_getchar_nonblock(void) {
    if (pl011_read(PL011_FR) & PL011_FR_RXFE) {
        return -1;
    }
    return (int)(pl011_read(PL011_DR) & 0xFF);
}

/**
 * @brief Flush the transmit FIFO
 * 
 * Waits until all pending transmissions are complete.
 */
void serial_flush(void) {
    /* Wait until transmit FIFO is empty and UART is not busy */
    while (!(pl011_read(PL011_FR) & PL011_FR_TXFE) ||
           (pl011_read(PL011_FR) & PL011_FR_BUSY)) {
        __asm__ volatile("nop");
    }
}

/* ============================================================================
 * Hex Output Helpers (for debugging)
 * ========================================================================== */

/**
 * @brief Output a single hex digit
 */
static void serial_put_hex_digit(uint8_t digit) {
    if (digit < 10) {
        serial_putchar('0' + digit);
    } else {
        serial_putchar('a' + digit - 10);
    }
}

/**
 * @brief Output a 32-bit value in hexadecimal
 * @param value Value to output
 */
void serial_put_hex32(uint32_t value) {
    serial_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        serial_put_hex_digit((value >> i) & 0xF);
    }
}

/**
 * @brief Output a 64-bit value in hexadecimal
 * @param value Value to output
 */
void serial_put_hex64(uint64_t value) {
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_put_hex_digit((value >> i) & 0xF);
    }
}

/**
 * @brief Output a decimal number
 * @param value Value to output
 */
void serial_put_dec(uint64_t value) {
    char buf[21];  /* Max 20 digits for 64-bit number + null */
    int i = 20;
    buf[i] = '\0';
    
    if (value == 0) {
        serial_putchar('0');
        return;
    }
    
    while (value > 0) {
        buf[--i] = '0' + (value % 10);
        value /= 10;
    }
    
    serial_puts(&buf[i]);
}

/* ============================================================================
 * Interrupt Support (for future use)
 * ========================================================================== */

/**
 * @brief Enable receive interrupt
 */
void serial_enable_rx_interrupt(void) {
    uint32_t imsc = pl011_read(PL011_IMSC);
    imsc |= PL011_INT_RX | PL011_INT_RT;
    pl011_write(PL011_IMSC, imsc);
}

/**
 * @brief Disable receive interrupt
 */
void serial_disable_rx_interrupt(void) {
    uint32_t imsc = pl011_read(PL011_IMSC);
    imsc &= ~(PL011_INT_RX | PL011_INT_RT);
    pl011_write(PL011_IMSC, imsc);
}

/**
 * @brief Clear pending interrupts
 */
void serial_clear_interrupts(void) {
    pl011_write(PL011_ICR, 0x7FF);
}

/**
 * @brief Get masked interrupt status
 * @return Masked interrupt status register value
 */
uint32_t serial_get_interrupt_status(void) {
    return pl011_read(PL011_MIS);
}
