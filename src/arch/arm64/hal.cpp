/**
 * @file hal.c
 * @brief ARM64 Hardware Abstraction Layer Implementation
 * 
 * This file implements the HAL interface for ARM64 (AArch64) architecture.
 * It provides unified initialization routines that dispatch to architecture-
 * specific subsystems.
 */

#include <hal/hal.h>
#include <drivers/timer.h>
#include <types.h>
#include "include/exception.h"
#include "include/gic.h"
#include "include/dtb.h"
#include <drivers/serial.h>
#include <lib/klog.h>
#include <lib/kprintf.h>

/* Forward declaration for serial output (defined in stubs.c) */
extern "C" void serial_puts(const char *str);
extern "C" void serial_put_hex64(uint64_t value);

/* ============================================================================
 * HAL Initialization State Tracking
 * ========================================================================== */

/** Flags to track initialization state */
static bool g_hal_cpu_initialized = false;
static bool g_hal_interrupt_initialized = false;
/* ============================================================================
 * CPU Initialization
 * ========================================================================== */

/**
 * @brief Initialize CPU architecture-specific features (ARM64)
 * 
 * Requirements: 1.1 - HAL initialization dispatch
 */
void hal::Cpu::init() {
    serial_puts("HAL: Initializing ARM64 CPU...\n");
    
    /* ARM64 CPU initialization:
     * - Exception level should already be EL1 (set by boot code)
     * - System registers configured by boot code
     */
    
    /* Enable FP/SIMD access for EL0 and EL1
     * CPACR_EL1.FPEN[21:20] = 0b11 enables FP/SIMD for both EL0 and EL1
     */
    uint64_t cpacr;
    __asm__ volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
    cpacr |= (3ULL << 20);  /* FPEN = 0b11 */
    __asm__ volatile("msr cpacr_el1, %0" : : "r"(cpacr));
    __asm__ volatile("isb");
    serial_puts("HAL: FP/SIMD enabled for EL0 and EL1\n");
    
    g_hal_cpu_initialized = true;
    serial_puts("HAL: ARM64 CPU initialization complete\n");
}

/**
 * @brief Halt the CPU until next interrupt
 */
void hal::Cpu::halt() {
    __asm__ volatile("wfi");
}

void hal::Cpu::idle() {
    /* wfi wakes up for a pending interrupt even while IRQs are masked;
     * unmasking afterwards lets the handler run */
    __asm__ volatile("wfi; msr daifclr, #2" ::: "memory");
}

/* ============================================================================
 * Interrupt Management
 * ========================================================================== */

/**
 * @brief Initialize interrupt system (ARM64)
 * 
 * Requirements: 1.1 - HAL initialization dispatch
 */
void hal::Interrupt::init() {
    serial_puts("HAL: Initializing ARM64 interrupt system...\n");
    
    /* Initialize exception vectors (VBAR_EL1) */
    arm64_exception_init();
    
    /* Initialize GIC (Generic Interrupt Controller) */
    gic_init();
    
    g_hal_interrupt_initialized = true;
    serial_puts("HAL: ARM64 interrupt system initialization complete\n");
}

/**
 * @brief Register an interrupt handler
 * @param irq IRQ number
 * @param handler Handler function
 * @param data User data
 */
void hal::Interrupt::register_handler(uint32_t irq, hal_interrupt_handler_t handler, void *data) {
    gic_register_handler(irq, handler, data);
    gic_enable_irq(irq);
}

/**
 * @brief Enable interrupts globally
 */
void hal::Interrupt::enable() {
    __asm__ volatile("msr daifclr, #0xf" ::: "memory");
}

bool hal::Interrupt::irq_is_free(uint32_t irq) {
    /* Only shared peripheral interrupts; SGIs/PPIs belong to the kernel */
    return irq >= 32 && irq < 1020 && !gic_has_handler(irq);
}

void hal::Interrupt::mask_irq(uint32_t irq) {
    gic_disable_irq(irq);
}

void hal::Interrupt::unmask_irq(uint32_t irq) {
    gic_enable_irq(irq);
}

/* ============================================================================
 * MMU Functions (delegated to mmu.c)
 * ========================================================================== */

/* Note: Most MMU functions are implemented in src/arch/arm64/mm/mmu.c */

/* ============================================================================
 * Timer Functions
 * ========================================================================== */

/** Timer tick counter (software counter incremented by timer IRQ) */
static volatile uint64_t g_timer_ticks = 0;

/** Timer frequency in Hz (requested frequency) */
static uint32_t g_timer_frequency = 0;

/** User timer callback */
static hal_timer_callback_t g_timer_callback = NULL;

/** 非安全物理定时器的 GIC 中断号。来自设备树；30 (PPI 14) 是架构建议的值，几乎所有平台都用它 */
static uint32_t g_timer_irq = 30;

/**
 * @brief Internal timer IRQ handler
 * @param data User data (unused)
 */
static void hal_timer_irq_handler(void *data) {
    (void)data;
    
    /* Increment software tick counter */
    g_timer_ticks = g_timer_ticks + 1;
    
    /* Let the timer driver handle the hardware FIRST: it reloads the timer for the
     * next tick (which clears the interrupt condition), advances its own tick
     * counter. */
    drivers::Timer::irq_handler();
    
    /* Call user callback if registered */
    if (g_timer_callback) {
        g_timer_callback();
    }
}

/**
 * @brief Initialize system timer (ARM64)
 * @param freq_hz Timer frequency in Hz
 * @param callback Timer callback function
 * 
 * Configures the ARM Generic Timer (physical timer) to generate
 * periodic interrupts at the specified frequency.
 */
void hal::Timer::init(uint32_t freq_hz, hal_timer_callback_t callback) {
    g_timer_frequency = freq_hz;
    g_timer_callback = callback;

    /* The HAL owns the IRQ registration and the scheduler callback;
     * the driver programs and enables the timer itself. */
    hal::Interrupt::register_handler(g_timer_irq, hal_timer_irq_handler, NULL);
    drivers::Timer::init(freq_hz);
}

/* ============================================================================
 * I/O Operations
 * ========================================================================== */

/* Note: MMIO functions are defined as inline in hal.h */

/* ============================================================================
 * Architecture Information
 * ========================================================================== */

/**
 * @brief Get architecture name string
 * @return "arm64"
 */
const char *hal_arch_name(void) {
    return "arm64";
}

/* ============================================================================
 * 用设备树里的值配置平台设备
 * ========================================================================== */

/** 内核的高半区映射能访问到的设备地址上限（引导页表映射了前 4GB） */
#define DEVICE_MAP_LIMIT    0x100000000ULL

/**
 * @brief 让串口、中断控制器、定时器的驱动使用设备树描述的地址和中断号
 *
 * 在 hal::Interrupt::init() 和 hal::Timer::init() 之前调用。驱动自带 QEMU virt 上的
 * 默认值，设备树里没有某个设备（或者值没法用）时保持默认并说明。
 *
 * @return 这个平台能不能用：中断控制器不是驱动支持的型号时返回 false
 */
bool arm64_configure_from_device_tree(const dtb_info_t *dtb) {
    if (dtb->uart_found && dtb->uart_base < DEVICE_MAP_LIMIT) {
        drivers::Serial::set_base(dtb->uart_base);
    } else {
        LOG_WARN_MSG("no PL011 in the device tree, staying on the early console\n");
    }

    if (!dtb->gic.found) {
        LOG_WARN_MSG("no GIC in the device tree, assuming the QEMU virt addresses\n");
    } else if (dtb->gic.version != 2) {
        // 驱动只会 GICv2：v3 的 CPU 接口是系统寄存器，照 v2 的方式去写内存什么都不会发生
        kprintf("PANIC: the device tree describes a GICv%u, only GICv2 is supported\n", dtb->gic.version);
        return false;
    } else if (dtb->gic.distributor_base < DEVICE_MAP_LIMIT && dtb->gic.cpu_interface_base < DEVICE_MAP_LIMIT) {
        gic_set_bases(dtb->gic.distributor_base, dtb->gic.cpu_interface_base);
    }

    if (dtb->timer_found) {
        g_timer_irq = dtb->timer_irq;
    } else {
        LOG_WARN_MSG("no timer in the device tree, assuming IRQ %u\n", g_timer_irq);
    }

    LOG_INFO_MSG("platform: PL011 at 0x%llx, GIC at 0x%llx / 0x%llx, timer IRQ %u\n",
                 (unsigned long long)drivers::Serial::base(),
                 (unsigned long long)gic_distributor_base(),
                 (unsigned long long)gic_cpu_interface_base(), g_timer_irq);
    return true;
}

/** 定时器用的中断号 */
uint32_t arm64_timer_irq(void) {
    return g_timer_irq;
}
