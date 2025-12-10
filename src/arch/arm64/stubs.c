/**
 * @file stubs.c
 * @brief ARM64 Minimal Kernel Implementation
 * 
 * Provides a minimal kernel implementation for ARM64 that boots and
 * outputs to serial console. This is a starting point for ARM64 support.
 * 
 * Requirements: 4.1
 */

#include <types.h>
#include <mm/mm_types.h>
#include <drivers/arm/serial.h>

/* ============================================================================
 * Task Management Stubs
 * ========================================================================== */

/**
 * @brief Stub for task_exit - called when a kernel thread returns
 * 
 * In a full implementation, this would clean up the task and schedule
 * another task. For now, we just halt.
 * 
 * @param exit_code Exit code from the task
 */
void task_exit(int exit_code) {
    (void)exit_code;
    serial_puts("task_exit called - halting\n");
    while (1) {
        __asm__ volatile("wfi");
    }
}

/* ============================================================================
 * System Call Stubs
 * ========================================================================== */

/**
 * @brief Stub for syscall_dispatcher
 * 
 * In a full implementation, this would dispatch to the appropriate
 * system call handler. For now, we just print the syscall number
 * and return an error.
 * 
 * @param syscall_num System call number
 * @param p1-p5 System call arguments
 * @param frame Stack frame pointer
 * @return -1 (ENOSYS - function not implemented)
 */
uint32_t syscall_dispatcher(uint32_t syscall_num, uint32_t p1, uint32_t p2,
                            uint32_t p3, uint32_t p4, uint32_t p5, uint32_t *frame) {
    (void)p1; (void)p2; (void)p3; (void)p4; (void)p5; (void)frame;
    
    serial_puts("syscall_dispatcher: syscall ");
    serial_put_hex64(syscall_num);
    serial_puts(" (not implemented)\n");
    
    return (uint32_t)-38;  /* -ENOSYS */
}

/* ============================================================================
 * Kernel Main Entry Point
 * ========================================================================== */

/* Forward declarations for HAL functions */
extern void hal_cpu_init(void);
extern void hal_interrupt_init(void);
extern void hal_interrupt_enable(void);

/* Forward declarations for DTB functions */
#include "include/dtb.h"

/**
 * @brief ARM64 Kernel Main Entry Point
 * 
 * Called from start.S after MMU is enabled and we're in high-half kernel.
 * 
 * @param dtb_addr Device Tree Blob address (passed by bootloader)
 */
void kernel_main(void *dtb_addr) {
    /* Early serial output for debugging */
    serial_puts("\n");
    serial_puts("========================================\n");
    serial_puts("  CastorOS ARM64 Kernel\n");
    serial_puts("========================================\n");
    serial_puts("\n");
    
    serial_puts("Boot successful!\n");
    serial_puts("DTB address: ");
    serial_put_hex64((uint64_t)dtb_addr);
    serial_puts("\n\n");
    
    serial_puts("Kernel virtual base: ");
    serial_put_hex64(0xFFFF000000000000ULL);
    serial_puts("\n\n");
    
    /* Parse Device Tree Blob */
    serial_puts("Parsing Device Tree...\n");
    dtb_info_t *dtb_info = dtb_parse(dtb_addr);
    if (dtb_info) {
        dtb_print_info();
    } else {
        serial_puts("WARNING: Failed to parse DTB\n\n");
    }
    
    /* Initialize HAL subsystems */
    serial_puts("Initializing HAL subsystems...\n\n");
    
    /* Initialize CPU */
    hal_cpu_init();
    serial_puts("\n");
    
    /* Initialize interrupt handling (exception vectors + GIC) */
    hal_interrupt_init();
    serial_puts("\n");
    
    /* Enable interrupts */
    serial_puts("Enabling interrupts...\n");
    hal_interrupt_enable();
    
    serial_puts("\n");
    serial_puts("ARM64 initialization complete!\n");
    serial_puts("Exception handling is now active.\n");
    serial_puts("\n");
    serial_puts("Entering idle loop...\n");
    
    /* Idle loop */
    while (1) {
        __asm__ volatile("wfi");
    }
}

/* ============================================================================
 * Logging Stubs for ARM64 Minimal Build
 * 
 * These are stub implementations for the kernel logging functions.
 * They redirect to serial output.
 * ========================================================================== */

#include <lib/klog.h>
#include <stdarg.h>

static log_level_t current_log_level = LOG_INFO;
static log_target_t current_log_target = LOG_TARGET_SERIAL;

void klog_set_level(log_level_t level) {
    current_log_level = level;
}

log_level_t klog_get_level(void) {
    return current_log_level;
}

void klog_set_target(log_target_t target) {
    current_log_target = target;
}

log_target_t klog_get_target(void) {
    return current_log_target;
}

/**
 * @brief Simple integer to hex string conversion
 */
static void log_put_hex(uint64_t value) {
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t digit = (value >> i) & 0xF;
        if (digit < 10) {
            serial_putchar('0' + digit);
        } else {
            serial_putchar('a' + digit - 10);
        }
    }
}

/**
 * @brief Simple klog implementation for ARM64
 * 
 * This is a minimal implementation that only supports %s, %d, %x, %p, %u, %llu, %llx
 */
void klog(log_level_t level, const char *fmt, ...) {
    if (level < current_log_level) {
        return;
    }
    
    const char *prefix;
    switch (level) {
        case LOG_DEBUG: prefix = "[DEBUG] "; break;
        case LOG_INFO:  prefix = "[INFO]  "; break;
        case LOG_WARN:  prefix = "[WARN]  "; break;
        case LOG_ERROR: prefix = "[ERROR] "; break;
        default:        prefix = "[????]  "; break;
    }
    
    serial_puts(prefix);
    
    va_list args;
    va_start(args, fmt);
    
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            
            /* Handle 'll' modifier */
            bool is_long_long = false;
            if (*fmt == 'l' && *(fmt+1) == 'l') {
                is_long_long = true;
                fmt += 2;
            }
            
            switch (*fmt) {
                case 's': {
                    const char *s = va_arg(args, const char *);
                    serial_puts(s ? s : "(null)");
                    break;
                }
                case 'd':
                case 'i': {
                    int64_t val;
                    if (is_long_long) {
                        val = va_arg(args, int64_t);
                    } else {
                        val = va_arg(args, int);
                    }
                    if (val < 0) {
                        serial_putchar('-');
                        val = -val;
                    }
                    /* Simple decimal output */
                    char buf[21];
                    int i = 20;
                    buf[i] = '\0';
                    do {
                        buf[--i] = '0' + (val % 10);
                        val /= 10;
                    } while (val > 0);
                    serial_puts(&buf[i]);
                    break;
                }
                case 'u': {
                    uint64_t val;
                    if (is_long_long) {
                        val = va_arg(args, uint64_t);
                    } else {
                        val = va_arg(args, unsigned int);
                    }
                    char buf[21];
                    int i = 20;
                    buf[i] = '\0';
                    do {
                        buf[--i] = '0' + (val % 10);
                        val /= 10;
                    } while (val > 0);
                    serial_puts(&buf[i]);
                    break;
                }
                case 'x':
                case 'X': {
                    uint64_t val;
                    if (is_long_long) {
                        val = va_arg(args, uint64_t);
                    } else {
                        val = va_arg(args, unsigned int);
                    }
                    log_put_hex(val);
                    break;
                }
                case 'p': {
                    void *ptr = va_arg(args, void *);
                    log_put_hex((uint64_t)ptr);
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    serial_putchar(c);
                    break;
                }
                case '%':
                    serial_putchar('%');
                    break;
                default:
                    serial_putchar('%');
                    serial_putchar(*fmt);
                    break;
            }
        } else {
            serial_putchar(*fmt);
        }
        fmt++;
    }
    
    va_end(args);
}

/* ============================================================================
 * PMM Stubs for ARM64 Minimal Build
 * 
 * These are stub implementations for the Physical Memory Manager functions
 * that are required by the MMU code. In a full implementation, these would
 * be replaced by the actual PMM from src/mm/pmm.c.
 * ========================================================================== */

/**
 * @brief Simple bump allocator for page tables
 * 
 * This is a very simple allocator that just bumps a pointer.
 * It's only suitable for early boot and testing.
 */
static paddr_t next_free_frame = 0x42000000ULL;  /* Start at 1GB + 32MB */
static paddr_t frame_pool_end = 0x44000000ULL;   /* End at 1GB + 64MB */

/**
 * @brief Allocate a physical page frame (stub)
 * @return Physical address of allocated frame, or PADDR_INVALID on failure
 */
paddr_t pmm_alloc_frame(void) {
    if (next_free_frame >= frame_pool_end) {
        serial_puts("pmm_alloc_frame: Out of memory!\n");
        return PADDR_INVALID;
    }
    
    paddr_t frame = next_free_frame;
    next_free_frame += PAGE_SIZE;
    
    /* Clear the frame */
    void *virt = (void*)PADDR_TO_KVADDR(frame);
    for (size_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++) {
        ((uint64_t*)virt)[i] = 0;
    }
    
    return frame;
}

/**
 * @brief Free a physical page frame (stub - no-op)
 * @param frame Physical address of frame to free
 */
void pmm_free_frame(paddr_t frame) {
    (void)frame;
    /* Stub: no-op in minimal build */
}

/**
 * @brief Increment reference count for a frame (stub)
 * @param frame Physical address of frame
 * @return New reference count (always 1 in stub)
 */
uint32_t pmm_frame_ref_inc(paddr_t frame) {
    (void)frame;
    return 1;  /* Stub: always return 1 */
}

/**
 * @brief Decrement reference count for a frame (stub)
 * @param frame Physical address of frame
 * @return New reference count (always 0 in stub)
 */
uint32_t pmm_frame_ref_dec(paddr_t frame) {
    (void)frame;
    return 0;  /* Stub: always return 0 */
}

/**
 * @brief Get reference count for a frame (stub)
 * @param frame Physical address of frame
 * @return Reference count (always 1 in stub)
 */
uint32_t pmm_frame_get_refcount(paddr_t frame) {
    (void)frame;
    return 1;  /* Stub: always return 1 */
}
