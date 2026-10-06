/**
 * @file hal.c
 * @brief ARM64 Hardware Abstraction Layer Implementation
 * 
 * This file implements the HAL interface for ARM64 (AArch64) architecture.
 * It provides unified initialization routines that dispatch to architecture-
 * specific subsystems.
 */

#include <mm/vmm.h>
#include <kernel/task.h>
#include <kernel/smp.h>
#include <hal/hal.h>
#include <drivers/timer.h>
#include <types.h>
#include "include/exception.h"
#include "include/gic.h"
#include "include/dtb.h"
#include <drivers/serial.h>
#include <lib/klog.h>
#include <lib/string.h>
#include <kernel/syscall.h>
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

/**
 * 设备树里 compatible 列表中有 info->compatible 这一项的第 info->index 个设备
 */
bool hal::Platform::find_device(struct device_info *info) {
    const dtb_info_t *dtb = dtb_get_info();
    if (!dtb) {
        return false;
    }
    uint32_t seen = 0;
    for (uint32_t i = 0; i < dtb->num_devices; i++) {
        const dtb_device_t *dev = &dtb->devices[i];
        if (!dtb_device_is(dev, info->compatible) || seen++ != info->index) {
            continue;
        }
        info->base = dev->base_addr;
        info->size = dev->size;
        info->irq = dev->irq;
        info->has_irq = dev->has_irq;
        memcpy(info->name, dev->name, sizeof(info->name));
        info->name[sizeof(info->name) - 1] = '\0';
        return true;
    }
    return false;
}

/* ARM64 has no I/O ports: device registers are memory (map_device) */
bool hal::Platform::port_read(uint16_t port, uint32_t width, uint32_t *value) {
    (void)port; (void)width; (void)value;
    return false;
}

bool hal::Platform::port_write(uint16_t port, uint32_t width, uint32_t value) {
    (void)port; (void)width; (void)value;
    return false;
}

void hal::Platform::set_user_ports(const struct hw_range *allowed, uint32_t count, bool allow) {
    (void)allowed; (void)count; (void)allow;
}

/* ============================================================================
 * Multiple CPUs
 * ========================================================================== */

/* The CPU index lives in TPIDR_EL1: start.S sets it to 0 on the boot CPU,
 * secondary_entry to the index start_secondaries() chose */
uint32_t hal::Cpu::id() {
    uint64_t id;
    __asm__ volatile("mrs %0, tpidr_el1" : "=r"(id));
    return (uint32_t)id;
}

/* Parameters for the CPU being started; layout shared with secondary_entry in start.S */
struct secondary_boot_args {
    uint64_t mair, tcr, ttbr0, ttbr1, sctlr;
    uint64_t stack_top;
    uint64_t cpu;
};
extern "C" struct secondary_boot_args secondary_boot;
extern "C" uint64_t secondary_entry_address;    /* physical: the boot code is linked there */
extern "C" void arm64_secondary_start(void);

#define PSCI_CPU_ON     0xC4000003ULL       /* SMC64 function id */

/** Call the firmware (PSCI). The device tree says which instruction traps into it */
static int64_t psci_call(uint32_t method, uint64_t function, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    register uint64_t x0 __asm__("x0") = function;
    register uint64_t x1 __asm__("x1") = arg1;
    register uint64_t x2 __asm__("x2") = arg2;
    register uint64_t x3 __asm__("x3") = arg3;
    if (method == DTB_PSCI_SMC) {
        __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) : :
                         "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory");
    } else {
        __asm__ volatile("hvc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) : :
                         "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory");
    }
    return (int64_t)x0;
}

uint32_t hal::Cpu::start_secondaries() {
    const dtb_info_t *dtb = dtb_get_info();
    if (!dtb || dtb->psci_method == DTB_PSCI_NONE || dtb->num_cpus < 2) {
        return 0;
    }

    uint64_t self;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(self));
    self &= 0xFF00FFFFFFULL;            /* the affinity fields */

    /* Every CPU runs with the same translation setup as this one. TTBR0 is the
     * kernel's own table: it still has the identity mapping the boot code needs
     * for the instant between turning the MMU on and jumping to the high half. */
    __asm__ volatile("mrs %0, mair_el1" : "=r"(secondary_boot.mair));
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(secondary_boot.tcr));
    __asm__ volatile("mrs %0, ttbr1_el1" : "=r"(secondary_boot.ttbr1));
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(secondary_boot.sctlr));
    secondary_boot.ttbr0 = mm::Vmm::kernel_page_directory();

    uint32_t started = 0;
    uint32_t next_index = 1;
    for (uint32_t i = 0; i < dtb->num_cpus && next_index < MAX_CPUS; i++) {
        if (dtb->cpu_mpidr[i] == self) {
            continue;
        }
        uint32_t cpu = next_index;
        uintptr_t stack = kernel::Scheduler::prepare_idle(cpu);
        if (!stack) {
            break;
        }
        secondary_boot.stack_top = stack;
        secondary_boot.cpu = cpu;
        __asm__ volatile("dsb sy" ::: "memory");    /* the new CPU reads this with its caches off */

        uint32_t before = kernel::Smp::cpu_count();
        int64_t result = psci_call(dtb->psci_method, PSCI_CPU_ON, dtb->cpu_mpidr[i],
                                   secondary_entry_address, 0);
        if (result != 0) {
            LOG_WARN_MSG("SMP: firmware refused to start CPU %u (PSCI error %lld)\n", cpu, (long long)result);
            continue;
        }

        /* One at a time (there is one secondary_boot). The new CPU needs the kernel
         * lock to announce itself, and we hold it: let go while we wait. */
        uint64_t deadline = drivers::Timer::get_uptime_ms() + 1000;
        kernel::KernelLock::leave();
        while (kernel::Smp::cpu_count() == before && drivers::Timer::get_uptime_ms() < deadline) {
            __asm__ volatile("yield");
        }
        kernel::KernelLock::enter();
        if (kernel::Smp::cpu_count() == before) {
            LOG_WARN_MSG("SMP: CPU %u did not come up\n", cpu);
            continue;
        }
        started++;
        next_index++;
    }
    return started;
}

/* First C code on a CPU that secondary_entry has brought into the high half */
void arm64_secondary_start(void) {
    kernel::Smp::secondary_main();
}

void hal::Cpu::init_secondary() {
    /* The same per-CPU setup the boot CPU went through in Cpu::init,
     * Interrupt::init and Timer::init */
    uint64_t cpacr;
    __asm__ volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
    cpacr |= (3ULL << 20);              /* FP/SIMD for EL0 and EL1 */
    __asm__ volatile("msr cpacr_el1, %0" : : "r"(cpacr));
    __asm__ volatile("isb");

    arm64_exception_init();             /* exception vectors */
    gic_init_secondary();
    gic_enable_irq(g_timer_irq);        /* the timer interrupt is per CPU */
    drivers::Timer::start_on_this_cpu();
}
