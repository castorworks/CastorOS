/**
 * @file smp64.cpp
 * @brief Starting the other CPUs on x86_64
 *
 * See kernel/smp.h for the model (one kernel lock, per-CPU state) and
 * docs/reference/smp.md for how the pieces fit. What is architecture-specific:
 * finding out which CPU we are, waking the others, and the per-CPU hardware state
 * (GDT/TSS, system call stack, Local APIC and its timer).
 */

#include <hal/hal.h>
#include <kernel/smp.h>
#include <kernel/task.h>
#include <drivers/timer.h>
#include <drivers/x86/lapic.h>
#include <mm/vmm.h>
#include <lib/klog.h>
#include <lib/string.h>
#include <gdt64.h>
#include <idt64.h>
#include <smp64.h>

/* ============================================================================
 * Which CPU is this
 * ========================================================================== */

/* APIC ID -> CPU index. All zero until the other CPUs are started, so everything
 * that asks before then (the whole single-CPU boot) is CPU 0. */
static uint8_t cpu_of_apic[256];

uint32_t hal::Cpu::id() {
    return cpu_of_apic[drivers::Lapic::id() & 0xFF];
}

/* ============================================================================
 * Waking the others
 * ========================================================================== */

/* Where the trampoline runs (see ap_trampoline.asm): a page of low memory. The PMM
 * never hands out frames below the end of the kernel image, so nobody owns it. */
#define AP_TRAMPOLINE_BASE  0x8000u

/* Layout shared with ap_trampoline.asm */
struct ap_trampoline_params {
    uint32_t cr3, cr4, cr0, efer;
    uint64_t stack;
    uint64_t entry;
} __attribute__((packed));

extern "C" const uint8_t ap_trampoline_start[], ap_trampoline_end[], ap_trampoline_params[];
extern "C" void x86_64_ap_entry(void);
extern "C" void syscall_init_cpu(void);

#define MSR_EFER    0xC0000080u
#define EFER_LMA    (1u << 10)      /* read-only status bit: not ours to write */

static uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

uint32_t hal::Cpu::start_secondaries() {
    if (!drivers::Lapic::available()) {
        return 0;
    }
    uintptr_t lapic = paging64_map_device(LAPIC_PHYS_BASE);
    paddr_t startup_table = paging64_make_startup_table();
    if (!lapic || !startup_table) {
        LOG_WARN_MSG("SMP: cannot set up the Local APIC, staying on one CPU\n");
        return 0;
    }
    drivers::Lapic::set_base(lapic);
    drivers::Lapic::enable(true);
    drivers::Lapic::timer_calibrate();

    /* Copy the trampoline to low memory and fill in what never changes */
    size_t size = (size_t)(ap_trampoline_end - ap_trampoline_start);
    uint8_t *trampoline = (uint8_t *)PADDR_TO_KVADDR(AP_TRAMPOLINE_BASE);
    memcpy(trampoline, ap_trampoline_start, size);
    struct ap_trampoline_params *params =
        (struct ap_trampoline_params *)(trampoline + (ap_trampoline_params - ap_trampoline_start));
    uint64_t cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    params->cr3 = (uint32_t)startup_table;
    params->cr4 = (uint32_t)cr4;
    params->cr0 = (uint32_t)cr0;
    params->efer = (uint32_t)rdmsr(MSR_EFER) & ~EFER_LMA;
    params->entry = (uint64_t)x86_64_ap_entry;

    /* There is no table of CPUs to consult here (that would be ACPI): APIC IDs are
     * handed out consecutively, so try them in order until one does not answer. */
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
        params->stack = stack;
        cpu_of_apic[apic] = (uint8_t)cpu;

        uint32_t before = kernel::Smp::cpu_count();
        drivers::Lapic::start_cpu(apic, AP_TRAMPOLINE_BASE);

        /* One at a time (one trampoline, one parameter block). The new CPU needs the
         * kernel lock to announce itself, and we hold it: let go while we wait. */
        uint64_t deadline = drivers::Timer::get_uptime_ms() + 200;
        kernel::KernelLock::leave();
        while (kernel::Smp::cpu_count() == before && drivers::Timer::get_uptime_ms() < deadline) {
            __asm__ volatile("pause");
        }
        kernel::KernelLock::enter();
        if (kernel::Smp::cpu_count() == before) {
            cpu_of_apic[apic] = 0;      /* nobody there: that was the last one */
            break;
        }
        started++;
    }
    return started;
}

/* ============================================================================
 * On the new CPU
 * ========================================================================== */

/* First C++ code on a CPU the trampoline has brought into long mode. It is on its
 * idle task's stack, on the start-up page table, with the trampoline's GDT. */
void x86_64_ap_entry(void) {
    /* Our own GDT and TSS, the shared IDT, and off the start-up page table */
    gdt64_init_with_tss(kernel::Scheduler::prepare_idle(hal::Cpu::id()));
    idt64_load();
    hal::Mmu::switch_space(mm::Vmm::kernel_page_directory());

    kernel::Smp::secondary_main();
}

void hal::Cpu::init_secondary() {
    __asm__ volatile("fninit");         /* CR0/CR4 already match the boot CPU's */
    syscall_init_cpu();                 /* SYSCALL MSRs and the per-CPU entry stack */
    drivers::Lapic::enable(false);
    /* The PIT only interrupts the boot CPU; this one keeps time with its own timer */
    drivers::Lapic::timer_start(drivers::Timer::get_frequency());
}
