// ============================================================================
// lapic.cpp - Local APIC：启动别的 CPU，和它们的时钟
// ============================================================================

#include <drivers/x86/lapic.h>
#include <drivers/timer.h>
#include <lib/klog.h>

/* 寄存器（相对基址的偏移，每个 32 位） */
#define REG_ID              0x020
#define REG_TPR             0x080       /* 任务优先级：0 = 不挡任何中断 */
#define REG_EOI             0x0B0
#define REG_SVR             0x0F0       /* 伪中断向量 + 软件使能位 */
#define REG_ICR_LOW         0x300       /* 处理器间中断：命令 */
#define REG_ICR_HIGH        0x310       /* 处理器间中断：目标 */
#define REG_LVT_TIMER       0x320
#define REG_LVT_LINT0       0x350
#define REG_LVT_LINT1       0x360
#define REG_TIMER_INIT      0x380
#define REG_TIMER_CURRENT   0x390
#define REG_TIMER_DIVIDE    0x3E0

#define SVR_ENABLE          (1u << 8)
#define LVT_MASKED          (1u << 16)
#define LVT_EXTINT          (7u << 8)   /* 直通外部（8259）中断 */
#define LVT_NMI             (4u << 8)
#define LVT_TIMER_PERIODIC  (1u << 17)
#define TIMER_DIVIDE_16     0x3

#define ICR_INIT            0x00000500u
#define ICR_STARTUP         0x00000600u
#define ICR_LEVEL_ASSERT    0x00004000u
#define ICR_LEVEL_TRIGGER   0x00008000u
#define ICR_PENDING         0x00001000u

static volatile uint32_t *regs = NULL;
static uint32_t timer_ticks_per_ms = 0;     /* 分频 16 之后，定时器每毫秒走多少 */

static uint32_t read(uint32_t reg) {
    return regs[reg / 4];
}

static void write(uint32_t reg, uint32_t value) {
    regs[reg / 4] = value;
}

/** 忙等 ms 毫秒。时钟是启动 CPU 的 PIT 中断在走，所以要开着中断 */
static void delay_ms(uint32_t ms) {
    uint64_t until = drivers::Timer::get_uptime_ms() + ms + 10;     /* 时钟一格是 10ms：多等一格 */
    while (drivers::Timer::get_uptime_ms() < until) {
        __asm__ volatile("pause");
    }
}

static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) : "a"(leaf), "c"(0));
}

namespace drivers {

bool Lapic::available() {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    return (edx & (1u << 9)) != 0;
}

uint32_t Lapic::id() {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    return ebx >> 24;
}

void Lapic::set_base(uintptr_t virt) {
    regs = (volatile uint32_t *)virt;
}

void Lapic::enable(bool boot_cpu) {
    write(REG_TPR, 0);
    // 8259 的中断线接在 LINT0 上：启动 CPU 让它直通，别的 CPU 屏蔽掉（设备中断只到启动 CPU）
    write(REG_LVT_LINT0, boot_cpu ? LVT_EXTINT : LVT_MASKED);
    write(REG_LVT_LINT1, boot_cpu ? LVT_NMI : LVT_MASKED);
    write(REG_LVT_TIMER, LVT_MASKED);
    write(REG_SVR, SVR_ENABLE | LAPIC_SPURIOUS_VECTOR);
}

/** 发一个处理器间中断，等它被送出去 */
static void send_ipi(uint32_t apic_id, uint32_t command) {
    write(REG_ICR_HIGH, apic_id << 24);
    write(REG_ICR_LOW, command);
    for (int i = 0; i < 100000 && (read(REG_ICR_LOW) & ICR_PENDING); i++) {
        __asm__ volatile("pause");
    }
}

void Lapic::start_cpu(uint32_t apic_id, uint32_t start_page) {
    // 规定的顺序：INIT（让它复位）、撤销 INIT、等一会儿，然后 SIPI，带着它该从哪一页开始执行；
    // SIPI 发两次，第一次没被接受时靠第二次（已经起来的 CPU 会忽略多出来的那次）
    send_ipi(apic_id, ICR_INIT | ICR_LEVEL_TRIGGER | ICR_LEVEL_ASSERT);
    send_ipi(apic_id, ICR_INIT | ICR_LEVEL_TRIGGER);
    delay_ms(10);
    for (int i = 0; i < 2; i++) {
        send_ipi(apic_id, ICR_STARTUP | (start_page >> 12));
        delay_ms(1);
    }
}

void Lapic::timer_calibrate() {
    // 让定时器从最大值往下数，数 50 毫秒（按 PIT 的时钟），看它走了多少
    write(REG_TIMER_DIVIDE, TIMER_DIVIDE_16);
    write(REG_LVT_TIMER, LVT_MASKED);
    uint64_t start = drivers::Timer::get_uptime_ms();
    while (drivers::Timer::get_uptime_ms() == start) {     /* 对齐到时钟的一格开头 */
        __asm__ volatile("pause");
    }
    start = drivers::Timer::get_uptime_ms();
    write(REG_TIMER_INIT, 0xFFFFFFFFu);
    while (drivers::Timer::get_uptime_ms() < start + 50) {
        __asm__ volatile("pause");
    }
    uint32_t elapsed = 0xFFFFFFFFu - read(REG_TIMER_CURRENT);
    write(REG_TIMER_INIT, 0);
    timer_ticks_per_ms = elapsed / 50;
    LOG_INFO_MSG("Local APIC timer: %u ticks per ms\n", timer_ticks_per_ms);
}

void Lapic::timer_start(uint32_t hz) {
    if (timer_ticks_per_ms == 0 || hz == 0) {
        return;
    }
    write(REG_TIMER_DIVIDE, TIMER_DIVIDE_16);
    write(REG_LVT_TIMER, LAPIC_TIMER_VECTOR | LVT_TIMER_PERIODIC);
    write(REG_TIMER_INIT, timer_ticks_per_ms * (1000 / hz));
}

void Lapic::eoi() {
    write(REG_EOI, 0);
}

} // namespace drivers
