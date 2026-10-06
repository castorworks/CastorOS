/**
 * @file smp.cpp
 * @brief 启动其余的 CPU（i686）
 *
 * 模型（一把内核锁、每个 CPU 自己的状态）见 kernel/smp.h，各部分怎么配合见
 * docs/reference/smp.md。这里是和架构有关的那些：我是哪个 CPU、把别的 CPU 叫醒、
 * 每个 CPU 自己的硬件状态（GDT/TSS、Local APIC 和它的定时器）。
 */

#include <hal/hal.h>
#include <kernel/smp.h>
#include <kernel/task.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <drivers/timer.h>
#include <drivers/x86/lapic.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/mm_types.h>
#include <lib/klog.h>
#include <lib/string.h>

/* ============================================================================
 * 我是哪个 CPU
 * ========================================================================== */

/* APIC ID -> CPU 编号。其余的 CPU 启动之前全是 0，所以在那之前问的（整个单 CPU 的启动
 * 过程）都是 0 号 */
static uint8_t cpu_of_apic[256];

uint32_t hal::Cpu::id() {
    return cpu_of_apic[drivers::Lapic::id() & 0xFF];
}

/* ============================================================================
 * 把别的 CPU 叫醒
 * ========================================================================== */

/* 启动代码运行的地方（见 boot/ap_trampoline.asm）：低端内存里的一页。PMM 从不把内核映像
 * 末尾之前的页帧分出去，所以它没有主人 */
#define AP_TRAMPOLINE_BASE  0x8000u

/* Local APIC 的寄存器映射到内核地址空间的最后几页之一。内核的直接映射区从
 * KERNEL_VIRTUAL_BASE 往上按内存大小铺；内存不到 2GB 时这里是空着的 */
#define LAPIC_VIRT_BASE     0xFFFFE000u

/* 和 ap_trampoline.asm 共用的布局 */
struct ap_trampoline_params {
    uint32_t cr3, cr4, cr0;
    uint32_t stack;
    uint32_t entry;
} __attribute__((packed));

extern "C" const uint8_t ap_trampoline_start[], ap_trampoline_end[], ap_trampoline_params[];
extern "C" void i686_ap_entry(void);
extern "C" uint32_t boot_page_directory[1024];

/** 把 Local APIC 的那一页寄存器映射进内核 */
static uintptr_t map_lapic(void) {
    if (hal::Mmu::query(HAL_ADDR_SPACE_CURRENT, LAPIC_VIRT_BASE, NULL, NULL)) {
        return 0;       // 内存大到直接映射区占了这里
    }
    if (!mm::Vmm::map_page_in_directory(mm::Vmm::kernel_page_directory(), LAPIC_VIRT_BASE, LAPIC_PHYS_BASE,
                                        PAGE_PRESENT | PAGE_WRITE | PAGE_CACHE_DISABLE)) {
        return 0;
    }
    return LAPIC_VIRT_BASE;
}

/**
 * 一个 CPU 刚启动时用的页目录：内核自己的那张，再把低端的 4MB 映射得和内核半区的开头一样。
 * 启动代码打开分页的那一刻还在物理地址上运行，而内核自己的页目录里那里没有映射
 * （boot.asm 进了高半区就把恒等映射撤掉了）。
 */
static paddr_t make_startup_directory(void) {
    paddr_t phys = mm::Pmm::alloc_frame();
    if (phys == PADDR_INVALID) {
        return 0;
    }
    uint32_t *startup = (uint32_t *)PHYS_TO_VIRT((uintptr_t)phys);
    memcpy(startup, boot_page_directory, PAGE_SIZE);
    startup[0] = boot_page_directory[KERNEL_VIRTUAL_BASE >> 22];
    return phys;
}

uint32_t hal::Cpu::start_secondaries() {
    if (!drivers::Lapic::available()) {
        return 0;
    }
    uintptr_t lapic = map_lapic();
    paddr_t startup_directory = make_startup_directory();
    if (!lapic || !startup_directory) {
        LOG_WARN_MSG("SMP: cannot set up the Local APIC, staying on one CPU\n");
        return 0;
    }
    drivers::Lapic::set_base(lapic);
    drivers::Lapic::enable(true);
    drivers::Lapic::timer_calibrate();

    // 把启动代码拷到低端内存，填上不变的那几个参数
    size_t size = (size_t)(ap_trampoline_end - ap_trampoline_start);
    uint8_t *trampoline = (uint8_t *)PHYS_TO_VIRT(AP_TRAMPOLINE_BASE);
    memcpy(trampoline, ap_trampoline_start, size);
    struct ap_trampoline_params *params =
        (struct ap_trampoline_params *)(trampoline + (ap_trampoline_params - ap_trampoline_start));
    uint32_t cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    params->cr3 = (uint32_t)startup_directory;
    params->cr4 = cr4;
    params->cr0 = cr0;
    params->entry = (uint32_t)i686_ap_entry;

    // 这里没有"机器上有哪些 CPU"的表可查（那是 ACPI 的事）：APIC ID 是连着编的，
    // 一个一个试，试到没人应为止
    uint32_t self = drivers::Lapic::id();
    uint32_t started = 0;
    for (uint32_t apic = 0; apic < MAX_CPUS && started + 1 < MAX_CPUS; apic++) {
        if (apic == self) {
            continue;
        }
        uint32_t cpu = started + 1;
        uintptr_t stack = kernel::Scheduler::prepare_idle(cpu);
        if (!stack) {
            break;
        }
        params->stack = (uint32_t)stack;
        cpu_of_apic[apic] = (uint8_t)cpu;

        uint32_t before = kernel::Smp::cpu_count();
        drivers::Lapic::start_cpu(apic, AP_TRAMPOLINE_BASE);

        // 一次一个（启动代码和参数区都只有一份）。新 CPU 要拿到内核锁才能报告自己起来了，
        // 而锁在我们手里：等的时候先放掉
        uint64_t deadline = drivers::Timer::get_uptime_ms() + 200;
        kernel::KernelLock::leave();
        while (kernel::Smp::cpu_count() == before && drivers::Timer::get_uptime_ms() < deadline) {
            __asm__ volatile("pause");
        }
        kernel::KernelLock::enter();
        if (kernel::Smp::cpu_count() == before) {
            cpu_of_apic[apic] = 0;      // 没人应：上一个就是最后一个
            break;
        }
        started++;
    }
    return started;
}

/* ============================================================================
 * 在新 CPU 上
 * ========================================================================== */

/* 启动代码把这个 CPU 带进保护模式、打开分页之后执行的第一段 C++ 代码。它在自己的 idle
 * 任务的栈上，用着启动用的页目录和启动代码里的那张 GDT */
void i686_ap_entry(void) {
    // 换成自己的 GDT 和 TSS、大家共用的 IDT，离开启动用的页目录
    gdt_init_all_with_tss((uint32_t)kernel::Scheduler::prepare_idle(hal::Cpu::id()), 0x10);
    idt_load();
    hal::Mmu::switch_space(mm::Vmm::kernel_page_directory());

    kernel::Smp::secondary_main();
}

void hal::Cpu::init_secondary() {
    __asm__ volatile("fninit");         // CR0/CR4 已经和启动 CPU 的一样了
    drivers::Lapic::enable(false);
    // PIT 的中断只到启动 CPU；这个 CPU 用自己 Local APIC 里的定时器计时
    drivers::Lapic::timer_start(drivers::Timer::get_frequency());
}
