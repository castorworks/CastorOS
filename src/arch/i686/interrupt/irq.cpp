// ============================================================================
// irq.c - 硬件中断请求处理 (i686)
// ============================================================================

#include <drivers/x86/lapic.h>
#include <kernel/task.h>
#include <kernel/smp.h>
#include <kernel/irq.h>
#include <kernel/idt.h>
#include <kernel/gdt.h>
#include <kernel/io.h>
#include <kernel/task.h>
#include <kernel/sync/spinlock.h>
#include <lib/klog.h>
#include <kernel/interrupt.h>
#include <kernel/user_irq.h>

/* PIC 端口 */
#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1

/* PIC 命令 */
#define PIC_EOI         0x20    // End of Interrupt

/* ICW (Initialization Command Words) */
#define ICW1_ICW4       0x01    // ICW4 需要
#define ICW1_INIT       0x10    // 初始化命令
#define ICW4_8086       0x01    // 8086/88 模式

/* IRQ 处理函数数组（仅用于硬件中断 0-15） */
static isr_handler_t irq_handlers[16] = {0};

/* Additional handlers for lines shared by several PCI devices. A level-triggered
 * line stays asserted until every device on it has been serviced, so each
 * interrupt calls all handlers registered for the line. */
#define IRQ_SHARED_MAX 3
static isr_handler_t irq_shared_handlers[16][IRQ_SHARED_MAX] = {{0}};

/* 保护 irq_handlers 数组的自旋锁
 * 注意：irq_handler() 在中断上下文中执行，因此必须使用 spinlock + IRQ save
 * 但由于 irq_handler 本身就在中断禁用状态下运行，所以读取时不需要额外保护
 * 只需要保护 irq_register_handler() 的写操作
 */
static sync::Spinlock irq_registry_lock;
static bool irq_registry_lock_initialized = false;

/* IRQ 统计计数器 */
static uint64_t irq_counts[16] = {0};

/* 定时器计数器（用于跟踪定时器中断次数） */
static volatile uint64_t timer_ticks = 0;

/**
 * IRQ 0 处理函数 - 定时器中断
 * PIT（可编程间隔定时器）默认约 18.2Hz 频率触发
 */
static void timer_handler(registers_t *regs) {
    (void)regs;  // 未使用
    timer_ticks = timer_ticks + 1;
    
    // 调用任务管理器的定时器处理函数
    // 用于更新任务运行时间、唤醒睡眠任务、时间片轮转等
    kernel::Scheduler::timer_tick();
}


/**
 * 重映射 PIC
 * 将 IRQ 0-15 映射到中断 32-47
 * 避免与 CPU 异常（0-31）冲突
 */
static void pic_remap(void) {
    uint8_t mask1, mask2;

    /* 保存当前的中断屏蔽字 */
    mask1 = inb(PIC1_DATA);
    mask2 = inb(PIC2_DATA);

    /* 开始初始化序列（级联模式） */
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

    /* ICW2: 设置中断向量偏移 */
    outb(PIC1_DATA, 32);    // 主 PIC: IRQ 0-7 → 中断 32-39
    outb(PIC2_DATA, 40);    // 从 PIC: IRQ 8-15 → 中断 40-47

    /* ICW3: 设置级联 */
    outb(PIC1_DATA, 0x04);  // 主 PIC: 从 PIC 在 IRQ 2
    outb(PIC2_DATA, 0x02);  // 从 PIC: 级联标识为 2

    /* ICW4: 设置模式 */
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    /* 恢复中断屏蔽字 */
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

/**
 * 发送 EOI（End of Interrupt）信号
 * 通知 PIC 中断处理完成
 */
static void pic_send_eoi(uint8_t irq) {
    /* 如果是从 PIC 的中断（IRQ 8-15），需要同时发送 EOI 到两个 PIC */
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    /* 总是发送 EOI 到主 PIC */
    outb(PIC1_COMMAND, PIC_EOI);
}

/**
 * IRQ 处理函数
 * 由汇编 IRQ 存根调用
 */
/* Kernel code runs with the kernel lock held (kernel/smp.h): taken here on the way in.
 * Going back to user mode it is released whatever the nesting says; going back into
 * the kernel (which already held it) only our own enter() is undone. */
extern "C" void irq_lapic_timer(void);
extern "C" void irq_lapic_spurious(void);
extern "C" void irq_lapic_kick(void);

static void irq_handler_locked(registers_t *regs);

extern "C" void irq_handler(registers_t *regs);
void irq_handler(registers_t *regs) {
    bool from_user = (regs->cs & 0x3) == 3;
    kernel::KernelLock::enter();
    irq_handler_locked(regs);
    if (from_user) {
        kernel::KernelLock::release();
    } else {
        kernel::KernelLock::leave();
    }
}

static void irq_handler_locked(registers_t *regs) {
    /* 启动 CPU 之外的 CPU 的时钟：来自它自己的 Local APIC，不是 PIC */
    if (regs->int_no == LAPIC_TIMER_VECTOR) {
        kernel::Scheduler::timer_tick();
        drivers::Lapic::eoi();
        schedule_from_irq((regs->cs & 0x3) == 3);
        return;
    }

    /* 别的 CPU 叫我们"过来一趟"（hal::Cpu::kick_others）。到了就行，没有别的事要做：
     * 闲着的 CPU 已经被叫醒；在用户态的 CPU 经过下面的抢占点，待处理的 kill 在那里生效 */
    if (regs->int_no == LAPIC_KICK_VECTOR) {
        drivers::Lapic::eoi();
        schedule_from_irq((regs->cs & 0x3) == 3);
        return;
    }

    /* 计算 IRQ 号（中断号 - 32） */
    uint8_t irq = regs->int_no - 32;

    /* 统计 IRQ 次数 */
    if (irq < 16) {
        irq_counts[irq]++;
    }

    /* 如果注册了处理函数，调用它 */
    if (irq < 16 && irq_handlers[irq] != 0) {
        isr_handler_t handler = irq_handlers[irq];
        handler(regs);
        for (int i = 0; i < IRQ_SHARED_MAX; i++) {
            if (irq_shared_handlers[irq][i] != 0) {
                irq_shared_handlers[irq][i](regs);
            }
        }
    } else if (!kernel::UserIrq::raise(irq)) {
        /* 内核和用户态驱动都没有认领的 IRQ */
        LOG_WARN_MSG("Unhandled IRQ %u (interrupt %u)\n", irq, regs->int_no);
    }
    /* 发送 EOI 信号 */
    pic_send_eoi(irq);

    /* EOI 之后再尝试触发调度，避免阻塞 PIC */
    schedule_from_irq((regs->cs & 0x3) == 3);
}

/**
 * 注册 IRQ 处理函数
 * 线程安全：使用 spinlock + IRQ save 保护
 */
void irq_register_handler(uint8_t irq, isr_handler_t handler) {
    if (irq >= 16) {
        return;
    }
    
    /* 确保锁已初始化 */
    if (!irq_registry_lock_initialized) {
        irq_registry_lock.init();
        irq_registry_lock_initialized = true;
    }
    
    /* 使用 IRQ save 版本，防止在注册过程中被中断打断 */
    sync::SpinlockIrqGuard guard(irq_registry_lock);
    irq_handlers[irq] = handler;
}

bool irq_has_handler(uint8_t irq) {
    return irq < 16 && irq_handlers[irq] != 0;
}

static inline uint16_t irq_get_port(uint8_t irq) {
    return (irq < 8) ? PIC1_DATA : PIC2_DATA;
}

void irq_disable_line(uint8_t irq) {
    if (irq >= 16) {
        return;
    }

    uint16_t port = irq_get_port(irq);
    if (irq >= 8) {
        irq -= 8;
    }

    uint8_t value = inb(port);
    value |= (uint8_t)(1u << irq);
    outb(port, value);
}

void irq_enable_line(uint8_t irq) {
    if (irq >= 16) {
        return;
    }

    uint16_t port = irq_get_port(irq);
    if (irq >= 8) {
        irq -= 8;
    }

    uint8_t value = inb(port);
    value &= (uint8_t)~(1u << irq);
    outb(port, value);
}


/**
 * 初始化 IRQ
 */
void irq_init(void) {
    LOG_INFO_MSG("Initializing IRQ...\n");

    /* 初始化 IRQ 注册表锁 */
    irq_registry_lock.init();
    irq_registry_lock_initialized = true;
    LOG_DEBUG_MSG("  IRQ registry lock initialized\n");

    /* 重映射 PIC */
    pic_remap();
    LOG_DEBUG_MSG("  PIC remapped (IRQ 0-15 -> INT 32-47)\n");

    /* 注册所有 IRQ 处理程序（32-47） */
    idt_set_gate(32, (uint32_t)irq0,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(33, (uint32_t)irq1,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(34, (uint32_t)irq2,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(35, (uint32_t)irq3,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(36, (uint32_t)irq4,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(37, (uint32_t)irq5,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(38, (uint32_t)irq6,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(39, (uint32_t)irq7,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(40, (uint32_t)irq8,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(41, (uint32_t)irq9,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(42, (uint32_t)irq10, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(43, (uint32_t)irq11, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(44, (uint32_t)irq12, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(45, (uint32_t)irq13, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(46, (uint32_t)irq14, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(47, (uint32_t)irq15, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);

    /* Local APIC 的两个向量：有别的 CPU 在运行时才用得上 */
    idt_set_gate(LAPIC_TIMER_VECTOR, (uint32_t)irq_lapic_timer, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(LAPIC_KICK_VECTOR, (uint32_t)irq_lapic_kick, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(LAPIC_SPURIOUS_VECTOR, (uint32_t)irq_lapic_spurious, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);

    /* 注册定时器处理函数（IRQ 0） */
    irq_register_handler(0, timer_handler);
    LOG_DEBUG_MSG("  Timer handler registered (IRQ 0)\n");

    /* 启用中断 */
    __asm__ volatile("sti");

    LOG_INFO_MSG("IRQ initialized successfully (16 hardware interrupts)\n");
    LOG_DEBUG_MSG("  Interrupts enabled\n");
}

